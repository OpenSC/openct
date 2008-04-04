/**
 * PertoSmart card reader driver (for readers using ACS AC-1030 chipset).
 *
 * Copyright 2005, Carlos Henrique Bauer <carlos.bauer@smartcon.com.br>
 */

#include "internal.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "atr.h"
#include "usb-descriptors.h"

#ifndef NULL
#define NULL 0
#endif

#ifndef USB_RECIP_ENDPOINT
#define USB_RECIP_ENDPOINT 0x02
#endif

#ifndef USB_TYPE_VENDOR
#define USB_TYPE_VENDOR (0x02 << 5)
#endif

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

typedef enum PS_INSTRUCTIION {
	PS_GET_ACR_STAT = 0x01,
	PS_SELECT_CARD_TYPE = 0x02,
	PS_SET_PROTOCOL = 0x03,
	PS_SET_NOTIFICATION = 0x06,
	PS_SET_OPTION = 0x07,
	PS_RESET = 0x80,
	PS_EXCHANGE_APDU = 0xa0,
	PS_EXCHANGE_T1_FRAME = 0xa1,
	PS_POWER_OFF = 0x81
} ps_instruction_t;

typedef enum PS_TRANSMISSION_STATE {
	IDLE = 0,
	WAITING_TO_SEND,
	WAITING_TO_RECEIVE,
	FINISHED,
	ERROR
} ps_transmission_state_t;

typedef enum PS_CARD_TYPE {
	PS_DEFAULT_CARD_TYPE = 0x00,
	PS_T0_CARD_TYPE = 0x0c,
	PS_T1_CARD_TYPE = 0x0d,
	PS_2WIRE_CARD_TYPE = 0x06,
	PS_3WIRE_CARD_TYPE = 0x05,
	PS_I2C_CARD_TYPE = 0x02
} ps_card_type_t;

typedef struct ps_stat {
	unsigned char internal[10];
	unsigned char max_c;
	unsigned char max_r;
	unsigned short c_type;
	unsigned char c_sel;
	unsigned char c_stat;
} ps_stat_t;

typedef struct ps_device_data {
	/* current reader status */
	ps_stat_t stat;
	/* state of the serial or usb interface */
	ps_transmission_state_t if_state;
	/* current protocol (negotiated by the reader during card powering up). */
	int cur_icc_proto;
	long dev_timeout;
	/* for USB readers */
	ifd_usb_capture_t *capture;
	struct timeval begin;
	long if_timeout;
} ps_device_data_t;

typedef struct ps_baud_rate {
	unsigned int bps;
	unsigned char code;
} ps_baudrate_t;

static const ps_baudrate_t ps_baudrate_table[] = {
	{9600, 0x12},
	{14400, 0x03},
	{19200, 0x11},
	{28800, 0x02},
	{38400, 0x10},
	{57600, 0x01},
	{115200, 0x00}
};

#define PS_MAX_SEND_LEN         65535
#define PS_HEADER_IDX           0
#define PS_INSTRUCTION_IDX      1

#define PS_HEADER               0x01

#define PS_COMMAND_LENGTH0_IDX  2
#define PS_COMMAND_LENGTH1_IDX  3
#define PS_COMMAND_LENGTH2_IDX  4

#define PS_RESPONSE_LENGTH0_IDX 3
#define PS_RESPONSE_LENGTH1_IDX 4
#define PS_RESPONSE_LENGTH2_IDX 5

#define PS_SW1_IDX              1
#define PS_SW2_IDX              2

#define PS_USB_INTERFACE_INDEX            0x00
#define PS_USB_INTERRUPT_ENDPOINT_ADDRESS 0x81
#define PS_USB_INTERRUPT_URB_DATA_SIZE    0x08

#define PS_STX                  0x02
#define PS_ETX                  0x03

#define PS_SET_NOTIFICATION_TRANSMIT 1
#define PS_SET_NOTIFICATION_DONT_TRANSMIT 2

#define PS_OPTION_9600_TO_96000 0
#define PS_OPTION_9600_ONLY     1
#define PS_OPTION_EMV_MODE_OFF  0
#define PS_OPTION_EMV_MODE_ON   (1 << 4)

/* read timeout
 * we must wait enough so that the card can finish its calculation */
static const long PS_TIMEOUT = 30000;

/* reader names */
static const char PS_USB_READER_NAME[] = "PertoSmart (AC1030, USB)";
static const char PS_SERIAL_READER_NAME[] = "PertoSmart (AC1030, Serial)";

typedef int complete_fn_t(const void *, size_t);

static size_t ps_calculate_tx_len(int proto, size_t slen)
{
	/* be didactic */
	size_t tx_len = 1 /* STX */  +
	    2 * (1 /* header */  +
		 1 /* command */  +
		 1 /* len */  +
		 1 /* checksum */ ) +
	    1 /* ETX */ ;

	/* room for more 2 len bytes */
	tx_len += 2 * (slen >= 0xff);
	tx_len += 2 * slen;

	return tx_len;
}

/*
 * Look for ETX.
 *
 * Return 0 if the transmission is not complete or the number of the bytes
 * in the packet which are part of the transmission, including the ETX.
 */
static int ps_complete_transmission(const void *p, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (((unsigned char *)p)[i] == PS_ETX) {
			if (ct_config.debug >= 4)
				ct_debug("ps_complete_transmission: ETX found");
			return i + 1;
		}
	}

	return 0;
}

#ifdef not_yet
static unsigned char ps_if_get_baudrate_code(int baudrate)
{

	int i;

	for (i = 0;
	     i < (sizeof(ps_baudrate_table) / sizeof(ps_baudrate_table)[0]);
	     i++) {
		if (ps_baudrate_table[i].bps == baudrate) {
			return ps_baudrate_table[i].code;
		}
	}

	return ps_baudrate_table[0].code;
}
#endif

/*
 * Send USB control message, and receive data via
 * Interrupt URBs.
 */
static int ps_if_transmission_start(ifd_device_t * dev, long timeout)
{
	int rc;
	ps_device_data_t *device_data;

	if (ct_config.debug >= 1)
		ct_debug("ps_if_transmission_start: called");

	device_data = (ps_device_data_t *) dev->user_data;

	if (device_data->if_state != IDLE && device_data->if_state != ERROR) {
		ct_error("ps_if_transmission_start: can't start "
			 "transmission: device not idle");
		return IFD_ERROR_LOCKED;
	}

	device_data->if_timeout = (timeout < 0) ? dev->timeout : timeout;

	if (dev->type == IFD_DEVICE_TYPE_USB) {
		rc = ifd_usb_begin_capture(dev,
					   IFD_USB_URB_TYPE_INTERRUPT,
					   PS_USB_INTERRUPT_ENDPOINT_ADDRESS,
					   PS_USB_INTERRUPT_URB_DATA_SIZE,
					   &(device_data->capture));
		if (rc != IFD_SUCCESS) {
			ct_error("ps_if_transmission_start: failed: %i", rc);
			device_data->capture = NULL;
			device_data->if_state = ERROR;
		}
	} else {
		rc = IFD_SUCCESS;
	}

	device_data->if_state = (rc == IFD_SUCCESS) ? WAITING_TO_SEND : ERROR;

	return rc;
}

static int
ps_if_transmission_send(ifd_device_t * dev, const void *sbuf, size_t slen)
{
	int rc;
	ps_device_data_t *device_data;

	device_data = (ps_device_data_t *) dev->user_data;

	if (ct_config.debug >= 4)
		ct_debug("ps_if_transmission_send: sent %u bytes: %s",
			 slen, ct_hexdump(sbuf, slen));
	else
		if (ct_config.debug >= 1)
			ct_debug("ps_if_transmission_send: called");



	if (device_data->if_state != WAITING_TO_SEND) {
		ct_error
		    ("ps_if_transmission_send: invalid transmission state %i.",
		     device_data->if_state);
		return IFD_ERROR_GENERIC;
	}

	gettimeofday(&(device_data->begin), NULL);

	if (dev->type == IFD_DEVICE_TYPE_USB) {
		rc = ifd_usb_control(dev,
				     IFD_USB_ENDPOINT_OUT |
				     IFD_USB_TYPE_VENDOR  |
				     IFD_USB_RECIP_DEVICE,
				     0, 0, 0, (void *)sbuf, slen,
				     device_data->if_timeout);
	} else {
		ifd_device_flush(dev);
		rc = ifd_device_send(dev, sbuf, slen);
	}

	if (rc < 0) {
		ct_error("ps_if_transmission_send: failed: %i", rc);
	}

	device_data->if_state = (rc < 0) ? ERROR : WAITING_TO_RECEIVE;

	return rc;
}

static int
ps_if_transmission_receive(ifd_device_t * dev, const void *rbuf, size_t rlen)
{

	int rc = IFD_SUCCESS;
	ps_device_data_t *device_data;
	size_t received;

	device_data = (ps_device_data_t *) dev->user_data;
	received = 0;

	if (device_data->if_state != WAITING_TO_RECEIVE) {
		ct_error
		    ("ps_if_transmission_receive: invalid transmission state %i.",
		     device_data->if_state);
		return IFD_ERROR_GENERIC;
	}

	if (rlen < PS_USB_INTERRUPT_URB_DATA_SIZE) {
		ct_error("ps_if_transmission_receive: buffer too small for "
			 "receiving interrupt urbs: %i", rlen);
		return IFD_ERROR_GENERIC;
	}

	if (rlen % PS_USB_INTERRUPT_URB_DATA_SIZE) {
		rlen =
		    (rlen / PS_USB_INTERRUPT_URB_DATA_SIZE) * PS_USB_INTERRUPT_URB_DATA_SIZE;
	}

	/* Capture URBs or read from the serial until we have a complete answer */
	for (;;) {
		unsigned char packet_buf[PS_USB_INTERRUPT_URB_DATA_SIZE];
		const int packet_buf_len = sizeof(packet_buf);
		long wait;

		wait =
		    device_data->if_timeout -
		    ifd_time_elapsed(&(device_data->begin));

		if (wait <= 0) {
			ct_error("ps_if_transmission_receive: timeout");
			rc = IFD_ERROR_TIMEOUT;
		} else {
			if (IFD_DEVICE_TYPE_USB == dev->type) {
				rc = ifd_usb_capture(dev, device_data->capture,
						     packet_buf, packet_buf_len,
						     wait);
			} else {
				ct_config.suppress_errors++;
				rc = ifd_device_recv(dev, packet_buf, 1, wait);
				ct_config.suppress_errors--;
			}
		}

		if (rc > 0) {

			int last_packet_len = 0;

			last_packet_len =
			    ps_complete_transmission(packet_buf, rc);

			if (last_packet_len) {
				rc = last_packet_len;
			}

			memcpy((caddr_t) rbuf + received, packet_buf, rc);

			received += rc;

			if (last_packet_len) {
				device_data->if_state = FINISHED;
				break;
			}

			if (received >= rlen) {
				break;
			}

		} else if (rc < 0) {
			device_data->if_state = ERROR;
			break;
		}
	}

	if (rc >= 0) {
		rc = received;
		if (ct_config.debug >= 4)
			ct_debug("ps_if_transmission_receive: received %u bytes:%s", rc,
			 	 ct_hexdump(rbuf, rc));
	} else {
		ct_error("ps_if_transmission_receive: failed: %i", rc);
	}

	return rc;
}

static int ps_if_transmission_flush_reader_output_buffer(ifd_device_t * dev)
{
	int rc = 0;
	ps_device_data_t *device_data;
	unsigned char packet_buf[256];
	const size_t packet_buf_len = sizeof(packet_buf);
	const long timeout = 20;

	if (ct_config.debug >= 1)
		ct_debug("ps_if_transmission_flush_reader_output_buffer: called");

	device_data = (ps_device_data_t *) dev->user_data;

	do {
		memset(packet_buf, 0, packet_buf_len);

		if (dev->type == IFD_DEVICE_TYPE_USB) {
			rc = ifd_usb_capture(dev, device_data->capture,
					     packet_buf, packet_buf_len,
					     timeout);
		} else {
			ct_config.suppress_errors++;
			rc = ifd_device_recv(dev, packet_buf, packet_buf_len,
					     timeout);
			ct_config.suppress_errors--;
		}

		if (rc <= 0) {
			break;
		}

	} while (!ps_complete_transmission(packet_buf, rc));

	return IFD_SUCCESS;
}

static int ps_if_transmission_end(ifd_device_t * dev)
{
	ps_device_data_t *device_data;

	if (ct_config.debug >= 1)
		ct_debug("ps_if_transmission_end: called");

	device_data = (ps_device_data_t *) dev->user_data;

	switch (device_data->if_state) {

	case WAITING_TO_RECEIVE:
	case WAITING_TO_SEND:
	case FINISHED:
	case ERROR:
		if (dev->type == IFD_DEVICE_TYPE_USB) {
			ifd_usb_end_capture(dev, device_data->capture);
			device_data->capture = NULL;
		}
		device_data->if_state = IDLE;

		break;

	default:
	case IDLE:
		break;
	}

	return IFD_SUCCESS;
}

static unsigned char PS_ASCII_TO_HEX(unsigned char a)
{
	return ((a) + (((a) >= 0x0a) ? 'A' - 0x0a : '0'));
}

static unsigned char PS_HEX_TO_ASCII(unsigned char h)
{
	return h - ((h >= 'A') ?
		    (((h >= 'a') ? 'a' : 'A') - (unsigned char)0x0a) : '0');
}

static unsigned char PS_VALID_HEX(unsigned char h)
{
	return (('0' <= (h) && (h) <= '9') ||
		('A' <= (h) && (h) <= 'F') || ('a' <= (h) && (h) <= 'f'));
}

/*
 * Encode a buffer to will be sent to the reader (to ASCII-HEX)
 */
static int
ps_encode_ascii_hex(unsigned char *out, size_t out_len,
		    const unsigned char *in, size_t in_len)
{
	int i, k;

	if (ct_config.debug >= 4)
		ct_debug("ps_encode_ascii_hex: called");

	if (out_len < (2 * in_len)) {
		ct_error("ps_encode_ascii_hex: output buffer too small.");
		return -1;
	}

	for (i = 0, k = 0; i < in_len; i++) {
		/* convert the most significant nibble */
		out[k++] = PS_ASCII_TO_HEX(in[i] >> 4);
		/* convert the less significant nibble */
		out[k++] = PS_ASCII_TO_HEX(in[i] & 0x0f);
	}

	/* return the number of byte copied to output buffer */
	return k;
}

/*
 * Decode a buffer received from the reader (from ASCII-HEX)
 */
static int
ps_decode_ascii_hex(unsigned char *out, size_t out_len,
		    const unsigned char *in, size_t in_len)
{
	size_t i, k;

	if (ct_config.debug >= 4)
		ct_debug("ps_decode_ascii_hex: called");

	if (in_len % 2) {
		ct_error
		    ("ps_decode_ascii_hex: input buffer len is not a power of 2.");
		return IFD_ERROR_GENERIC;
	}

	if (out_len < (in_len > 1)) {
		ct_error("ps_decode_ascii_hex: output buffer too small.");
		return IFD_ERROR_BUFFER_TOO_SMALL;
	}

	for (i = 0, k = 0; (i + 1) < in_len && k < out_len; k++) {

		if (!(PS_VALID_HEX(in[i]))) {
			ct_error
			    ("ps_decode_ascii_hex: invalid ascii code hex value: %#x = %c.",
			     i, in[i]);
			return IFD_ERROR_GENERIC;
		}

		if (!(PS_VALID_HEX(in[i + 1]))) {
			ct_error
			    ("ps_decode_ascii_hex: invalid ascii code hex value: %#x = %c.",
			     i + 1, in[i + 1]);
			return IFD_ERROR_GENERIC;
		}

		out[k] = PS_HEX_TO_ASCII(in[i++]) << 4;
		out[k] |= PS_HEX_TO_ASCII(in[i++]);
	}

	return k;
}

static unsigned char ps_checksum(unsigned char iv,
			  const unsigned char *buf, size_t len)
{
	unsigned char checksum = iv;
	int mylen = len;

	if (buf != NULL) {
		while (mylen) {
			checksum ^= buf[--mylen];
		}
	}

	return checksum;
}

/*
 * Send command to IFD
 */
static int
ps_send_to_ifd(ifd_reader_t * reader, ps_instruction_t instruction,
	       const unsigned char *sbuf, size_t slen)
{
	int rc;
	unsigned char buffer[1024];
	unsigned char protocol_bytes[5]; /* 1 header byte      +
					    1 instruction byte +
					    3 size bytes         */
	unsigned char *buffer_start = buffer;
	unsigned char *p;
	unsigned char checksum;
	size_t buffer_len = sizeof(buffer);
	size_t size_tmp;
	ifd_device_t *dev;
	ps_device_data_t *device_data;
	size_t tx_len;

	if (ct_config.debug >= 4)
		ct_debug("ps_send_to_ifd: sending %u bytes:%s", slen,
		 	 ct_hexdump(sbuf, slen));
	else if (ct_config.debug >= 1)
		ct_debug("ps_send_to_ifd: called");
	

	dev = reader->device;
	device_data = (ps_device_data_t *) dev->user_data;

	if (slen > PS_MAX_SEND_LEN) {
		ct_error ("ps_apdu_send: transmission is larger "
				"than maximum allowed: %i", slen);
		return IFD_ERROR_GENERIC;
	}

	tx_len = ps_calculate_tx_len(device_data->cur_icc_proto, slen);

	if (tx_len > sizeof(buffer)) {
		ct_error
		    ("ps_send_to_ifd: failed: transmission is too large (%i bytes) "
		     "for drivers's transmission buffer (%i bytes)",
		     (int)tx_len, sizeof(buffer));
		return IFD_ERROR_NO_MEMORY;
	}

	p = buffer;

	/* add STX */
	*p++ = PS_STX;

	/* add protocol bytes */
	protocol_bytes[PS_HEADER_IDX] = PS_HEADER;
	protocol_bytes[PS_INSTRUCTION_IDX] = instruction;

	/* add data length */
	if (slen < 0xff) {
		/* normal command */
		protocol_bytes[PS_COMMAND_LENGTH0_IDX] = (unsigned char)slen;
		size_tmp = PS_COMMAND_LENGTH0_IDX + 1;
	} else {
		/* extended command */
		protocol_bytes[PS_COMMAND_LENGTH0_IDX] = 0xff;
		protocol_bytes[PS_COMMAND_LENGTH1_IDX] =
		    (unsigned char)(slen >> 8);
		protocol_bytes[PS_COMMAND_LENGTH2_IDX] =
		    (unsigned char)(slen & 0xff);
		size_tmp = PS_COMMAND_LENGTH2_IDX + 1;
	}

	rc = ps_encode_ascii_hex(p,
				 buffer_len - (p - buffer_start),
				 protocol_bytes, size_tmp);

	checksum = ps_checksum(0, protocol_bytes, size_tmp);

	if (rc < 0) 
		goto out;

	p += rc;

	/* add data */
	rc = ps_encode_ascii_hex(p, buffer_len - (p - buffer_start), sbuf,
				 slen);

	if (rc < 0) 
		goto out;

	p += rc;

	checksum = ps_checksum(checksum, sbuf, slen);

	/* add checksum */
	rc = ps_encode_ascii_hex(p, buffer_len - (p - buffer_start),
				 &checksum, sizeof(checksum));

	p += rc;

	/* add ETX */
	*p++ = PS_ETX;

	/* start the transmission */
	rc = ps_if_transmission_start(dev, dev->timeout);

	if (rc != IFD_SUCCESS) 
		goto out;

	rc = ps_if_transmission_flush_reader_output_buffer(dev);

	if (rc != IFD_SUCCESS) 
		goto out;

	/* send the data */
	rc = ps_if_transmission_send(dev, buffer_start, p - buffer_start);

      out:

	if (buffer != buffer_start && buffer != NULL) {
		free(buffer_start);
	}

	if (rc < 0) {
		ct_error("ps_send_to_ifd: failed: %i", rc);
		ps_if_transmission_end(dev);
	} 

	return rc;
}

static int
ps_receive_from_ifd(ifd_reader_t * reader, unsigned char *rbuf, size_t rlen)
{
	int rc;
	unsigned char protocol_bytes[6]; /* 1 STX    byte  +
					    2 status bytes +
					    3 size bytes */
	unsigned char checksum;
	unsigned char expected_checksum;
	unsigned char sw1 = 0;
	unsigned char sw2 = 0;
	unsigned char buffer[536];
	unsigned char *p = buffer;
	const size_t buffer_len = sizeof(buffer);
	size_t tmp_length;
	size_t data_length;
	size_t remaining_data_length;
	size_t rbuf_offset;
	size_t encoded_data_slice_len;
	int rcvd_len;
	ifd_device_t *dev;
	ps_device_data_t *device_data;

	if (ct_config.debug >= 1)
		ct_debug("ps_receive_from_ifd: called");

	dev = reader->device;
	device_data = (ps_device_data_t *) dev->user_data;
	data_length = 0;

	/**
	 * rbuf == NULL && rlen == 0 IS VALID,
	 * it means receive the reader status, but no data
	 */
	if(rbuf == NULL && rlen > 0) {
		ct_error("ps_receive_from_ifd: NULL == rbuf && rlen > 0");
		rc = IFD_ERROR_GENERIC;
		goto out;
	}

	memset(rbuf, 0, rlen);

	/* receive transmission */
	rc = ps_if_transmission_receive(dev, buffer, buffer_len);

	if (rc < 0) 
		goto out;

	rcvd_len = rc;

	p = buffer;

	/* must start with a STX, send error? */
	if (*p != PS_STX) {
		ct_error("ps_receive_from_ifd: missing STX");
		rc = IFD_ERROR_COMM_ERROR;
		goto out;
	}

	p++;

	/* decode the "protocol bytes", i.e. header, SW1, SW1 and data length */
		/* 8 = 2 * (header + sw1 + sw2 + data_length) */
		/* make sure it's even size */
	rc = ps_decode_ascii_hex(protocol_bytes, sizeof(protocol_bytes), p,
			 min(8,(rcvd_len - (p - buffer)) & ~((size_t) 1)));

	if (rc < 0) 
		goto out;

	/* calculate checksum of the decoded data */
	checksum = ps_checksum(0, protocol_bytes, rc);

	/* header is present */
	if (protocol_bytes[PS_HEADER_IDX] != PS_HEADER) {
		/* receive error */
		rc = IFD_ERROR_COMM_ERROR;
		goto out;
	}

	/* status word */
	sw1 = protocol_bytes[PS_SW1_IDX];
	sw2 = protocol_bytes[PS_SW2_IDX];

	if (ct_config.debug >= 4)
		ct_debug("ps_receive_from_ifd: sw1 = %#02x, sw2 = %#02x", sw1, sw2);

	if (sw1 != 0x90) {
		if (sw1 == 0x60 && sw2 == 0x02) {
			rc = IFD_ERROR_NO_CARD;
		} else {
			rc = IFD_ERROR_GENERIC;
		}
		goto out;
	}

	/* skip already decoded data */
	p += 2 * rc;

	if (ct_config.debug >= 4)
		ct_debug("ps_receive_from_ifd: "
			 "protocol_bytes[PS_RESPONSE_LENGTH0_IDX]: %i",
	     		 protocol_bytes[PS_RESPONSE_LENGTH0_IDX]);

	/* decode the length of the received data */
	if (protocol_bytes[PS_RESPONSE_LENGTH0_IDX] == 0xff) {
		/* it's an extended response...
		   next two encoded bytes are the data length */
		if ((rcvd_len - (p - buffer)) < 4) {
			/* did't read enough bytes for size data */
			rc = IFD_ERROR_COMM_ERROR;
			goto out;
		}

		rc = ps_decode_ascii_hex(&protocol_bytes
					 [PS_RESPONSE_LENGTH1_IDX],
					 sizeof(protocol_bytes) -
					 PS_RESPONSE_LENGTH1_IDX + 1, p, 4);

		if (rc < 0 ) 
			goto out;

		/* calculate checksum of the decoded data */
		checksum =
		    ps_checksum(checksum,
				&protocol_bytes[PS_RESPONSE_LENGTH1_IDX], rc);

		if (ct_config.debug >= 4) {
			ct_debug("ps_receive_from_ifd: "
				 "protocol_bytes[PS_RESPONSE_LENGTH1_IDX]: %i",
		     		 protocol_bytes[PS_RESPONSE_LENGTH1_IDX]);
			ct_debug("ps_receive_from_ifd: "
				 "protocol_bytes[PS_RESPONSE_LENGTH2_IDX]: %i",
		     		 protocol_bytes[PS_RESPONSE_LENGTH2_IDX]);
		}
		
		data_length = protocol_bytes[PS_RESPONSE_LENGTH1_IDX] << 8;
		data_length |= protocol_bytes[PS_RESPONSE_LENGTH2_IDX];

		/* skip decoded data */
		p += 2 * rc;

	} else {
		/* one byte for data length */
		data_length = protocol_bytes[PS_RESPONSE_LENGTH0_IDX];
	}

	if (rlen < data_length) {
		ct_error("ps_receive_from_ifd: output buffer too small (%i), "
			 "%i bytes are needed", rlen, data_length);
		return IFD_ERROR_GENERIC;
	}

	rbuf_offset = 0;
	remaining_data_length = data_length;

	/* while has data to decode */
	while (1) {
		tmp_length = rcvd_len - (p - buffer);

		/* if has data to send to the output */
		if (remaining_data_length > 0) {
			/* must be even number */
			encoded_data_slice_len =
			    min(remaining_data_length, tmp_length >> 1) << 1;

			if (device_data->if_state == FINISHED) {
				if ((remaining_data_length << 1) !=
				    encoded_data_slice_len) {
					/* something got wrong */
					ct_error
					    ("ps_receive_from_ifd: data length is diferent "
					     "from data length reported by reader.");
					goto out;
				}
			}

			/* decode slice */
			rc = ps_decode_ascii_hex(rbuf + rbuf_offset,
						 rlen - rbuf_offset, p,
						 encoded_data_slice_len);

			if (rc < 0) 
				goto out;

			/* calculate checksum of the decode data */
			checksum =
			    ps_checksum(checksum, rbuf + rbuf_offset, rc);

			p += 2 * rc;
			remaining_data_length -= rc;
			rbuf_offset = data_length - remaining_data_length;
		}

		if (device_data->if_state == FINISHED) 
			break;
		

		/* move buffer tail to beginning */
		tmp_length = rcvd_len - (p - buffer);

		if (tmp_length > 0) 
			memmove(buffer, p, tmp_length);

		/* point p to end of data */
		p = buffer + tmp_length;

		/* append the next slice */
		rc = ps_if_transmission_receive(dev, p, buffer_len);

		if (rc < 0) {
			goto out;
		}

		/* point p to the beginning of the buffer */
		p = buffer;

		rcvd_len = tmp_length + rc;
	}

	/* decode checksum */
	rc = ps_decode_ascii_hex(&expected_checksum,
				 sizeof(expected_checksum), p, 2);

	if (checksum != expected_checksum) {
		ct_error("ps_receive_from_ifd: failed checksum.");
		rc = IFD_ERROR_COMM_ERROR;
		goto out;
	}

	p += 2 * rc;

	/* last byte is a ETX? */
	if (*p != PS_ETX) {
		ct_error("ps_receive_from_ifd: missing ETX.");
		rc = IFD_ERROR_COMM_ERROR;
		goto out;
	}

	rc = data_length;

      out:

	ps_if_transmission_end(dev);

	if (rc < 0) {
		ct_error("ps_receive_from_ifd: failed: %i", rc);
	} else {
		if (ct_config.debug >= 4) {
			ct_debug("ps_receive_from_ifd: received: %i: %s", rc,
			 	 ct_hexdump(rbuf, rc));
		}
	}

	return rc;
}

static int
ps_transceive_instruction(ifd_reader_t * reader,
			  ps_instruction_t instruction,
			  const void *sbuf,
			  size_t slen, void *rbuf, size_t rlen)
{
	int rc;

	if (ct_config.debug >= 1)
		ct_debug("ps_transceive_instruction: called");

	rc = ps_send_to_ifd(reader, instruction, sbuf, slen);

	if (rc < 0) {
		ct_error("ps_transceive_instruction: failed: %i", rc);
	} else {
		rc = ps_receive_from_ifd(reader, rbuf, rlen);
	}

	return rc;
}

/*
 * Power up the card slot
 */
static int ps_activate(ifd_reader_t * reader)
{
	
	if (ct_config.debug >= 1)
		ct_debug("ps_activate: called");

	return IFD_SUCCESS;
}

static int ps_deactivate(ifd_reader_t * reader)
{
	int rc;

	
        if (ct_config.debug >= 1)
		ct_debug("ps_deactivate: called");

	rc = ps_transceive_instruction(reader, PS_POWER_OFF, NULL, 0, NULL, 0);

	if (rc < 0) 
		ct_error("ps_deactivate: failed: %i", rc);

	return rc;
}

static int ps_get_stat(ifd_reader_t * reader, ps_stat_t * stat)
{
	int rc;
	unsigned char buffer[16];
	unsigned char *p;

        if (ct_config.debug >= 1)
		ct_debug("ps_get_stat: called");

	rc = ps_transceive_instruction(reader, PS_GET_ACR_STAT,
				       NULL, 0, buffer, sizeof(buffer));

	if (rc < 0) 
		goto out;

	if (sizeof(buffer) > rc) {
		rc = IFD_ERROR_COMM_ERROR;
		goto out;
	}

	for (p = buffer; p < (buffer + sizeof(stat->internal)); p++) {
		stat->internal[p - buffer] = *p;
	}

	stat->max_c = *p++;
	stat->max_r = *p++;
	stat->c_type = *p++ << 8;
	stat->c_type |= *p++;
	stat->c_sel = *p++;
	stat->c_stat = *p++;

      out:

	if (rc < 0) 
		ct_error("ps_get_stat: failed: %i", rc);

	return (0 <= rc) ? IFD_SUCCESS : rc;
}

static int ps_card_status(ifd_reader_t * reader, int slot, int *status)
{
	int rc;
	ifd_device_t *dev;
	ps_device_data_t *device_data;
	unsigned char c_stat;

        if (ct_config.debug >= 1)
		ct_debug("ps_card_status: called");

	if (slot != 0) {
		ct_error("ps_card_status: bad slot index %u", slot);
		return IFD_ERROR_INVALID_SLOT;
	}

	dev = reader->device;

	device_data = (ps_device_data_t *) dev->user_data;

	c_stat = device_data->stat.c_stat;

	rc = ps_get_stat(reader, &(device_data->stat));

	if (rc == IFD_SUCCESS) {
		*status = (device_data->stat.c_stat) ? IFD_CARD_PRESENT : 0;

		if (c_stat != device_data->stat.c_stat) {
			*status |= IFD_CARD_STATUS_CHANGED;
		}
	} else {
		ct_error("ps_card_status: failed: %i", rc);
	}

	return rc;
}

/*
 * Reset and reset the protocol
 */
static int
ps_card_reset_select_protocol(ifd_reader_t * reader, int nslot,
			      void *atr, size_t size, int new_icc_proto)
{
	int rc;
	int atr_len;
	unsigned char sbuf[1];
	ifd_device_t *dev;
	ps_device_data_t *device_data;
	ifd_slot_t *slot;
	ifd_atr_info_t atr_info;

        if (ct_config.debug >= 1)
		ct_debug("ps_card_reset_select_protocol: called");

	if (nslot != 0) {
		ct_error("ps_card_reset_select_protocol: bad slot index %u",
			 nslot);
		return IFD_ERROR_INVALID_SLOT;
	}

	dev = reader->device;
	slot = &reader->slot[nslot];
	device_data = (ps_device_data_t *) dev->user_data;

	/* power of the card */
	rc = ps_transceive_instruction(reader, PS_POWER_OFF, NULL, 0, NULL, 0);

	if (rc != IFD_SUCCESS) {
		ct_error
		    ("ps_card_reset_select_protocol: failed (PS_POWER_OF): %i",
		     rc);
		return rc;
	}

	if (slot->proto == NULL || device_data->cur_icc_proto != new_icc_proto) {
		switch (new_icc_proto) {

		case IFD_PROTOCOL_DEFAULT:
		        if (ct_config.debug >= 1)
				ct_debug("ps_card_reset_select_protocol: "
					 "using automatic protocol selecting");
			sbuf[0] = PS_DEFAULT_CARD_TYPE;
			break;

		case IFD_PROTOCOL_T0:
		        if (ct_config.debug >= 1)
				ct_debug("ps_card_reset_select_protocol: "
					 "selecting protocol T0");
			sbuf[0] = PS_T0_CARD_TYPE;
			break;

		case IFD_PROTOCOL_T1:
		        if (ct_config.debug >= 1)
				ct_debug("ps_card_reset_select_protocol: "
					 "selecting protocol T1");
			sbuf[0] = PS_T1_CARD_TYPE;
			break;

		default:
			ct_error("ps_card_reset_select_protocol: "
				 "unknow protocol %i", new_icc_proto);
			return IFD_ERROR_NOT_SUPPORTED;
		}

		rc = ps_transceive_instruction(reader, PS_SELECT_CARD_TYPE,
					       sbuf, sizeof(sbuf), NULL, 0);

		if (rc != IFD_SUCCESS) {
			ct_error("ps_card_reset_select_protocol: "
				 "error selecting card type %#02x", sbuf[0]);
			return rc;
		}
	}

	/* power up the card */
	rc = ps_transceive_instruction(reader, PS_RESET, NULL, 0, atr, size);

	if (rc < 0) {
		ct_error("ps_card_reset_select_protocol: failed (PS_RESET): %i",
			 rc);
		return rc;
	}

	atr_len = rc;

	/* the reader does PPS negotiation with the card
	   parse the atr to check the protocol negotiated by the reader */

	rc = ifd_atr_parse(&atr_info, atr, atr_len);

	if (rc < 0) {
		ct_error("ps_card_reset_select_protocol: %s: Bad ATR",
			 reader->name);
		return rc;
	}

	if (atr_info.TA[1] != -1) {
		/* specific mode */
	        if (ct_config.debug >= 1)
			ct_debug("ps_card_reset_select_protocol: "
				 "card in specific mode %#02x",
		     		 atr_info.TA[1] & 0x0f);
		new_icc_proto = atr_info.TA[1] & 0x0f;
	} else {
		if (new_icc_proto == IFD_PROTOCOL_DEFAULT) {
			new_icc_proto = atr_info.default_protocol;
		}
	}

	if (slot->proto == NULL || device_data->cur_icc_proto != new_icc_proto) {

		if (slot->proto != NULL) {
			ifd_protocol_free(slot->proto);
		}

		slot->proto =
		    ifd_protocol_new(new_icc_proto, reader, slot->dad);

		if (slot->proto == NULL) {
			ct_error("ps_cart_reset_select_protocol: "
				 "ifd_protocol_new");
			return IFD_ERROR_GENERIC;
		}

		/* set protocol parameters */
		switch (new_icc_proto) {

		case IFD_PROTOCOL_T0:
		        if (ct_config.debug >= 1)
				ct_debug("ps_card_reset_select_protocol: "
					 "using protocol T0");
			ifd_protocol_set_parameter(slot->proto,
						   IFD_PROTOCOL_BLOCK_ORIENTED,
						   1);
			break;

		case IFD_PROTOCOL_T1:
		        if (ct_config.debug >= 1)
				ct_debug("ps_card_reset_select_protocol: "
					 "using protocol T1");
			ifd_protocol_set_parameter(slot->proto,
						   IFD_PROTOCOL_BLOCK_ORIENTED,
						   1);
			ifd_protocol_set_parameter(slot->proto,
						   IFD_PROTOCOL_T1_IFSC,
						   (atr_info.TA[2] !=
						    -1) ? atr_info.
						   TA[2] : 0x20);
			ifd_protocol_set_parameter(slot->proto,
						   IFD_PROTOCOL_T1_IFSD, 254);
			break;

		default:
			ct_error("ps_card_reset_select_protocol: "
				 "protocol not supported %#02x",
			     	 atr_info.default_protocol);
			return IFD_ERROR_NOT_SUPPORTED;
		}

		/* save protocol info */
		device_data->cur_icc_proto = new_icc_proto;
	}

	return atr_len;
}

static int
ps_card_reset(ifd_reader_t * reader, int slot, void *atr, size_t size)
{
	ifd_device_t *dev;
	ps_device_data_t *device_data;

	if (ct_config.debug >= 1)
		ct_debug("ps_card_reset: called");

	dev = reader->device;
	device_data = (ps_device_data_t *) dev->user_data;

	return ps_card_reset_select_protocol(reader, slot,
					     atr, size,
					     device_data->cur_icc_proto);
}

/*
 * Select a protocol for communication with the ICC.
 *
 */
static int ps_set_protocol(ifd_reader_t * reader, int nslot, int proto)
{
	int rc;
	ifd_device_t *dev;
	ps_device_data_t *device_data;
	ifd_slot_t *slot;

        if (ct_config.debug >= 1)
		ct_debug("ps_set_protocol: called");

	dev = reader->device;
	device_data = (ps_device_data_t *) dev->user_data;
	slot = &reader->slot[nslot];

	if (slot->proto == NULL || device_data->cur_icc_proto != proto) {

		/* the reader negotiates the protocol during the card power up
		   must power down and reset the card to change it */
		rc = ps_card_reset_select_protocol(reader, nslot,
						   slot->atr, sizeof(slot->atr),
						   proto);

		if (rc >= IFD_SUCCESS) {
			slot->atr_len = rc;
			rc = IFD_SUCCESS;
		} else {
			memset(slot->atr, 0, sizeof(slot->atr));
			slot->atr_len = 0;
		}

	} else {
		rc = IFD_SUCCESS;
	}

	return rc;
}

static int
ps_apdu_send(ifd_reader_t * reader, unsigned int dad,
	     const unsigned char *sbuf, size_t slen)
{
	int rc;
	int cse;
	ifd_device_t *dev;
	ps_device_data_t *device_data;
	ps_instruction_t instruction;
	unsigned char t0_buffer[260];

        if (ct_config.debug >= 3)
		ct_debug("ps_apdu_send: sending %i: %s", 
		         slen, ct_hexdump(sbuf, slen));
	else
		if (ct_config.debug >= 1)
			ct_debug("ps_apdu_send: called");


	dev = reader->device;
	device_data = (ps_device_data_t *) dev->user_data;

	switch (device_data->cur_icc_proto) {

	case IFD_PROTOCOL_T0:

	        if (ct_config.debug >= 1)
			ct_debug("ps_apdu_send: using EXCHANGE_APDU");

		instruction = PS_EXCHANGE_APDU;

		/* must have room for le=0 or lc=0 */
		if (sizeof(t0_buffer) <= slen) {
			ct_error("ps_apdu_send: apdu size not supported: "
				 "%i bytes (max: %i)",
			     	 slen, sizeof(t0_buffer) - 1);
			return IFD_ERROR_NO_MEMORY;
		}

		cse = ifd_apdu_case(sbuf, slen);

		switch (cse) {

		case IFD_APDU_CASE_1:

			if (ct_config.debug >= 1)
				ct_debug("ps_apdu_send: T0 case 1");

			/* lc = 0 at the end of sbuf
			   must add le = 0 at end */

			/* fall through */

		case IFD_APDU_CASE_3S:
			
			if (ct_config.debug >= 1 && IFD_APDU_CASE_3S == cse) 
				ct_debug("ps_apdu_send: T0 case 3S");

			/* lc is in the expected placed
			   must add le = 0  at end */

			memcpy(t0_buffer, sbuf, slen);

			t0_buffer[slen] = 0;

			break;

		case IFD_APDU_CASE_2S:

			if (ct_config.debug >= 1)
				ct_debug("ps_apdu_send: T0 case 2S");

			/* le is at the end of sbuf
			   must insert lc = 0 before le */

			memcpy(t0_buffer, sbuf, slen);

			t0_buffer[slen] = sbuf[slen - 1];
			t0_buffer[slen - 1] = 0;

			break;

		case IFD_ERROR_GENERIC:
			ct_error("ps_apdu_send: ifd_apdu_case failed");
			return IFD_ERROR_GENERIC;

		default:
			ct_error("ps_apdu_send: apdu case not supported %i",
				 cse);
			return IFD_ERROR_NOT_SUPPORTED;
		}

		rc = ps_send_to_ifd(reader, instruction, t0_buffer, slen + 1);

		break;

	case IFD_PROTOCOL_T1:
		if (ct_config.debug >= 1)		
			ct_debug("ps_apdu_send: using EXCHANGE_T1 FRAME");
		instruction = PS_EXCHANGE_T1_FRAME;
		rc = ps_send_to_ifd(reader, instruction, sbuf, slen);
		break;

	default:
		if (ct_config.debug >= 1)
			ct_debug("ps_apdu_send: unknow protocol");
		return IFD_ERROR_GENERIC;
	}

	if (rc < 0) 
		ct_error("ps_apdu_send: error %i", rc);

	return rc;
}

static int
ps_apdu_recv(ifd_reader_t * reader, unsigned int dad, unsigned char *buffer,
	     size_t len, long timeout)
{
	int rc;

	if (ct_config.debug >= 1)
		ct_debug("ps_apdu_recv: called");

	rc = ps_receive_from_ifd(reader, buffer, len);

	if (rc < 0) {
		ct_error("ps_apdu_recv: failed");
	} else {
		if (ct_config.debug >= 3)
			ct_debug("ps_apdu_recv: received %i bytes: %s", rc,
				 ct_hexdump(buffer, rc));
	}

	return rc;
}

/*
 * Initialize the device
 */
static int ps_open(ifd_reader_t * reader, const char *device_name)
{
	int rc;
	ifd_device_t *dev;
	ps_device_data_t *device_data;
	ifd_device_params_t params;

	unsigned char sbuf[2];

	if (ct_config.debug >= 1)
		ct_debug("ps_open: called: device name =%s", device_name);

	dev = ifd_device_open(device_name);

	if (dev == NULL) {
		ct_error("ps_open: failed to open device: %", device_name);
		return IFD_ERROR_GENERIC;
	}

	switch (dev->type) {

	case IFD_DEVICE_TYPE_USB:
		
		reader->name = PS_USB_READER_NAME;
		
		params = dev->settings;
		params.usb.interface = PS_USB_INTERFACE_INDEX;
		params.usb.ep_intr = PS_USB_INTERRUPT_ENDPOINT_ADDRESS;
		
		rc = ifd_device_set_parameters(dev, &params);
		
		if (rc != IFD_SUCCESS) {
			ct_error("ps_open: ifd_device_set_parameters "
			"returned error %i", rc);
			return rc;
		}
		break;

	case IFD_DEVICE_TYPE_SERIAL:
		reader->name = PS_SERIAL_READER_NAME;
		break;

	default:
		ifd_device_close(dev);
		ct_error("ps_open: unknow device type %i", dev->type);
		return IFD_ERROR_GENERIC;
	}

	sleep(1);

	ifd_device_flush(dev);

	device_data = (ps_device_data_t *) calloc(1, sizeof(*device_data));

	if (NULL == device_data) {
		ifd_device_close(dev);
		ct_error("ps_open: not enough memory");
		return IFD_ERROR_NO_MEMORY;
	}

	memset(device_data, 0, sizeof(*device_data));

	device_data->if_state = IDLE;
	device_data->cur_icc_proto = IFD_PROTOCOL_DEFAULT;

	reader->nslots = 1;
	reader->device = dev;
	reader->device->user_data = device_data;
	reader->device->timeout = PS_TIMEOUT;

	sbuf[0] = PS_SET_NOTIFICATION_DONT_TRANSMIT;

	/* disable reader notifications */
	ps_transceive_instruction(reader, PS_SET_NOTIFICATION, sbuf, 1, NULL,
				  0);

#ifdef not_yet

	if (dev->type == IFD_DEVICE_TYPE_SERIAL) {
		int rc;
		ifd_device_params_t params;
		unsigned char rbuf[2];

		/* try to to use 115200 */

		rc = ifd_device_get_parameters(dev, &params);

		if (0 > rc) {
			return rc;
		}

		sbuf[0] = 0;	/* delay */
		sbuf[1] = ps_if_get_baudrate_code(115200);

		rc = ps_transceive_instruction(reader, PS_SET_PROTOCOL,
					       sbuf, 2, rbuf, sizeof(rbuf));

		if (rc => 0) {
			params.serial.speed = 115200;
			rc = ifd_device_set_parameters(dev, &params);

			if (rc < 0) 
				return rc;
		
			if (ct_config.debug >= 1)	
				ct_debug("ps_open: baudrate changed to 115200");
		}
	}
#endif

	sbuf[0] = PS_OPTION_9600_TO_96000 | PS_OPTION_EMV_MODE_OFF;

	rc = ps_transceive_instruction(reader, PS_SET_OPTION, sbuf, 1, NULL, 0);

	if (rc != IFD_SUCCESS) {
		ct_error("ps_open: error setting reader option");
		return rc;
	}

	return IFD_SUCCESS;
}

static int ps_close(ifd_reader_t * reader)
{
	ifd_device_t *dev;
	ps_device_data_t *device_data;

	if (ct_config.debug >= 1)
		ct_debug("ps_open: called");

	dev = reader->device;
	device_data = (ps_device_data_t *) dev->user_data;

	ps_deactivate(reader);

	free(device_data);

	ifd_device_close(dev);

	return 0;
}

/*
 * Initialize this module
 */
void ifd_pertosmart_ac1030_register(void)
{

	static struct ifd_driver_ops perto_smart_driver;

	perto_smart_driver.open = ps_open;
	perto_smart_driver.close = ps_close;
	perto_smart_driver.activate = ps_activate;
	perto_smart_driver.deactivate = ps_deactivate;
	perto_smart_driver.card_status = ps_card_status;
	perto_smart_driver.card_reset = ps_card_reset;
	perto_smart_driver.set_protocol = ps_set_protocol;
	perto_smart_driver.send = ps_apdu_send;
	perto_smart_driver.recv = ps_apdu_recv;

	ifd_driver_register("pertosmart1030", &perto_smart_driver);
}
