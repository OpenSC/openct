/**
 * PertoSmart card reader driver (for readers using ACS AC-1038 chipset).
 *
 * Copyright 2005, Carlos Henrique Bauer <carlos.bauer@smartcon.com.br>
 */

#include "internal.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "atr.h"

#ifndef NULL
#define NULL 0
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

/**
 * Reader USB Interface info
 * 
 *   Endpoint    Address      Function              Direction     Packet size
 * Bulk out        0x02    Command               host -> reader     64 bytes
 * Bulk in         0x82    Response              host <- reader     64 bytes
 * Interrupt in    0x81    Card status message   host <- reader      8 bytes
 */

#define PS_USB_INTERFACE_INDEX              0x00
#define PS_USB_BULK_OUTPUT_ENDPOINT_ADDRESS 0x02
#define PS_USB_BULK_INPUT_ENDPOINT_ADDRESS  0x82
#define PS_USB_INTERRUPT_ENDPOINT_ADDRESS   0x81

typedef enum PS_INSTRUCTIION {
	PS_GET_ACR_STAT = 0x01,
	PS_SELECT_CARD_TYPE = 0x02,
	PS_SET_OPTION = 0x07,
	PS_RESET = 0x80,
	PS_EXCHANGE_TPDU_T0 = 0xa0,
	PS_EXCHANGE_TPDU_T1 = 0xa1,
	PS_POWER_OFF = 0x81,
	PS_SET_CARD_PPS = 0x0a,
	PS_SET_READER_PPS = 0x0b,
} ps_instruction_t;

static const unsigned char PS_CARD_INSERTED_NOTIFICATION[] = {
	0x01, 0xc1, 0x00, 0x00
};

static const unsigned char PS_CARD_REMOVED_NOTIFICATION[] = {
	0x01, 0xc0, 0x00, 0x00
};

typedef enum PS_CARD_TYPE {
	PS_AUTO_T0_OR_T1_CARD_TYPE = 0x00,	/* automatic T=0 or T=1
						   protocol selection for
						   MCU cards                   */
	PS_T0_CARD_TYPE = 0x0c,	/* T=0 protocol for MCU cards  */
	PS_T1_CARD_TYPE = 0x0d,	/* T=1 protocol for MCU cards  */
	PS_I2C_CARD_TYPE_1K_16K_CARD_TYPE = 0x01,	/* I2C memory card:
							   1k, 2k, 4k, 8k and 16k    */
	PS_I2C_CARD_TYPE_32K_1024K_CARD_TYPE = 0x02,	/* I2C memory card:
							   32k, 64k, 128k, 256k,
							   512k and 1024k            */
	PS_AT88SC153_CARD_TYPE = 0x03,	/* ATMEL AT88SC153 secure
					   memory card                 */
	PS_AT88SC1608_CARD_TYPE = 0x04,	/* ATMEL AT88SC1608 secure
					   memory card                 */
	PS_3WIRE_CARD_TYPE = 0x05,	/* SLE4418 or SLE4428 card     */
	PS_2WIRE_CARD_TYPE = 0x06,	/* SLE4432 or SLE4442 cards    */
} ps_card_type_t;

typedef enum PS_RESET_VOLTAGE_SELECTION {
	PS_RESET_SELECT_AUTO_V = 0x00,
	PS_RESET_SELECT_5V = 0x01,
	PS_RESET_SELECT_3V = 0x02,
	PS_RESET_SELECT_1V8 = 0x03
} ps_reset_voltage_selection_t;

typedef enum PS_RESPONSE_STATUS_CODE {
	PS_SUCCESS = 0x00,
	PS_SLOTERROR_PROCEDURE_BYTE_CONFLICT = 0xf4,
	PS_SLOTERROR_BAD_LENGTH = 0xf6,
	PS_SLOTERROR_BAD_FIDI = 0xf7,
	PS_SLOTERROR_BAD_ATR_IS = 0xf8,
	PS_SLOTERROR_ICC_NOT_POWERED_UP = 0xf9,
	PS_SLOTERROR_ICC_NOT_INSERTED = 0xfa,
	PS_SLOTERROR_HW_ERROR = 0xfb,
	PS_SLOTERROR_XFE_OVERRUN = 0xfc,
	PS_SLOTERROR_XFE_PARITY_ERROR = 0xfd,
	PS_SLOTERROR_ICC_MUTE = 0xfe,
	PS_SLOTERROR_CMD_ABORTED = 0xff,
} ps_response_status_code_t;

typedef enum PS_TRANSMISSION_STATE {
	IDLE = 0,
	WAITING_TO_SEND,
	SENDING,
	FINISHED,
	RECEIVING,
	ERROR
} ps_transmission_state_t;

typedef enum PS_CARD_STATUS {
	PS_CARD_UNKNOWN = -1,
	PS_CARD_NOT_INSERTED = 0,
	PS_CARD_INSERTED = 1
} ps_card_status_t;

typedef struct ps_stat {
	unsigned char internal[10];
	unsigned char max_c;
	unsigned char max_r;
	unsigned short c_type;
	unsigned char c_sel;
	unsigned char c_stat;
} ps_stat_t;

typedef struct ps_device_data {
	/* current card status */
	ps_card_status_t card_status;
	/* state of the serial or usb interface */
	ps_transmission_state_t if_state;
	/* current protocol (negotiated by the reader during card powering up). */
	int cur_icc_proto;
	long dev_timeout;
	struct timeval begin;
	long if_timeout;
	ifd_usb_capture_t *capture;
} ps_device_data_t;

#define PS_MAX_SEND_LEN         65535

#define PS_HEADER               0x01

#define PS_HEADER_IDX           0
#define PS_INSTRUCTION_IDX      1
#define PS_COMMAND_LENGTH0_IDX  2
#define PS_COMMAND_LENGTH1_IDX  3

#define PS_STATUS_IDX           1
#define PS_RESPONSE_LENGTH0_IDX 2
#define PS_RESPONSE_LENGTH1_IDX 3

#define PS_COMMAND_HEADER_SIZE   4
#define PS_RESPONSE_HEADER_SIZE  4
#define PS_RESPONSE_DATA_IDX     PS_RESPONSE_HEADER_SIZE

#define PS_INTERRUPT_URB_DATA_SIZE 0x08
#define PS_ENDPOINT                0x81

#define PS_BULK_SEND_PACKET_SIZE    64
#define PS_BULK_RECEIVE_PACKET_SIZE 64

#define PS_OPTION_EMV_MODE_ON    (1 << 4)	/* done */
#define PS_OPTION_MEMORY_CARD_ON (1 << 5)	/* done */

#define PS_DEFAULT_T1_IFSC 0x20
#define PS_MAX_T1_IFSD     0xfe

/* read timeout
 * we must wait enough so that the card can finish its calculation */
static const long PS_BULK_TIMEOUT = 30000;
static const long PS_INTERRUPT_TIMEOUT = 100;

/* reader names */
static const char PS_USB_READER_NAME[] = "PertoSmart EMV (AC1038, USB)";

typedef int complete_fn_t(const void *, size_t);

typedef struct PS_RESPONSE_STATUS_CODE_MAP_ENTRY {
	ps_response_status_code_t status_code;
	char *status_str;
} ps_response_status_code_map_entry_t;

/*
 * Convert reader status byte to a string.
 */
static const char *ps_get_status_string(ps_response_status_code_t status_code)
{
	static const ps_response_status_code_map_entry_t
	    ps_response_status_code_map[] = {
		{PS_SUCCESS, "Success"},
		{PS_SLOTERROR_PROCEDURE_BYTE_CONFLICT,
		 "Procedure Byte Conflict"},
		{PS_SLOTERROR_BAD_LENGTH, "Bad Length"},
		{PS_SLOTERROR_BAD_FIDI, "Bad Fidi"},
		{PS_SLOTERROR_BAD_ATR_IS, "Bad Atr IS"},
		{PS_SLOTERROR_ICC_NOT_POWERED_UP, "Icc Not Powered Up"},
		{PS_SLOTERROR_ICC_NOT_INSERTED, "Icc Not Inserted"},
		{PS_SLOTERROR_HW_ERROR, "Hardware Error"},
		{PS_SLOTERROR_XFE_OVERRUN, "Transfer Overrun"},
		{PS_SLOTERROR_XFE_PARITY_ERROR, "Transfer Parity Error"},
		{PS_SLOTERROR_ICC_MUTE, "ICC mute"},
		{PS_SLOTERROR_CMD_ABORTED, "Command Aborted"}
	};

	int i;

	for (i = 0; i < sizeof(ps_response_status_code_map) /
	     sizeof(ps_response_status_code_map[0]); i++) {

		if (ps_response_status_code_map[i].status_code == status_code) {
			return ps_response_status_code_map[i].status_str;
		}
	}

	return "Unknown response status code";
}

/*
 * Switch the driver to transmission state.
 */
static int ps_if_transmission_start(ifd_device_t * dev, long timeout)
{
	ps_device_data_t *device_data;

	if (ct_config.debug >= 1)
		ct_debug("ps_if_transmission_start: called");

	device_data = (ps_device_data_t *) dev->user_data;

	if (device_data->if_state != IDLE && device_data->if_state != ERROR) {
		ct_error("ps_if_transmission_start: can't start "
			 "transmission: device not idle");
		return IFD_ERROR_LOCKED;
	}

	device_data->if_state = WAITING_TO_SEND;
	device_data->if_timeout = (timeout < 0) ? dev->timeout : timeout;

	return IFD_SUCCESS;
}

/*
 * Send data to reader.
 */
static int
ps_if_transmission_send(ifd_device_t * dev,
			const unsigned char *sbuf, size_t slen)
{
	int rc;
	ps_device_data_t *device_data;

	if (ct_config.debug >= 1)
		ct_debug("ps_if_transmission_send: called");
	if (ct_config.debug >= 4)
		ct_debug("ps_if_transmission_send: sending %u bytes: %s",
		 	 slen, ct_hexdump(sbuf, slen));

	device_data = (ps_device_data_t *) dev->user_data;

	if (device_data->if_state  != WAITING_TO_SEND &&
	    device_data->if_state != SENDING) {
		ct_error("ps_if_transmission_send: "
		         "invalid transmission state %i.",
		         device_data->if_state);
		rc = IFD_ERROR_GENERIC;
		goto out;
	}

	if (device_data->if_state == WAITING_TO_SEND) {
		gettimeofday(&(device_data->begin), NULL);
		ifd_device_flush(dev);
		device_data->if_state = SENDING;
	}

	/* complete packet */
	rc = ifd_device_send(dev, sbuf, slen);
	if (slen != rc) {
		if (rc >= IFD_SUCCESS) {
			/* if_device_send didn't reported an error,
			   but didn't transmitted a full packet to the reader */
 			if (ct_config.debug >= 1)
				ct_debug("ps_if_transmission_send: unexpected "
					 "result from ifd_device_send: %i", rc);
			rc = IFD_ERROR_COMM_ERROR;
		}
		goto out;
	}

      out:

	if (rc < 0) {
		device_data->if_state = ERROR;
		ct_error("ps_if_transmission_send: failed: %i", rc);
	}

	return rc;
}

/*
 * Receive data from reader.
 */
static int
ps_if_transmission_receive(ifd_device_t * dev, unsigned char *rbuf, size_t rlen)
{

	int rc = IFD_SUCCESS;
	ps_device_data_t *device_data = NULL;
	unsigned char *chunk_start = NULL;
	size_t chunk_len = 0;
	size_t rbuf_offset = 0;

	if (ct_config.debug >= 1)
		ct_debug("ps_if_transmission_receive: called");

	device_data = (ps_device_data_t *) dev->user_data;

	if (device_data->if_state != SENDING
	    && device_data->if_state != RECEIVING) {
		ct_error("ps_if_transmission_receive: "
			 "invalid transmission state %i.",
			 device_data->if_state);
		rc = IFD_ERROR_GENERIC;
		goto out;
	}

	device_data->if_state = RECEIVING;

	for (;;) {
		chunk_len = min(PS_BULK_RECEIVE_PACKET_SIZE,
				rlen - rbuf_offset);

		if (chunk_len > 0) {
			long timeout;
			chunk_start = &rbuf[rbuf_offset];

			timeout = device_data->if_timeout -
			    ifd_time_elapsed(&(device_data->begin));

			rc = ifd_device_recv(dev, chunk_start, chunk_len,
					     timeout);

			if (rc < IFD_SUCCESS) {
				if(ct_config.debug >= 1)
					ct_debug("ps_if_transmission_receive: error: %i",
				     	         rc);
				goto out;
			}

			rbuf_offset += rc;
		}

		if (rc < chunk_len || rbuf_offset >= rlen) {
			break;
		}
	}

	/* return the length of received data */
	rc = rbuf_offset;

      out:
	if (rc < 0) {
		device_data->if_state = ERROR;
		ct_error("ps_if_transmission_receive: failed: %i", rc);
	} else {
		if (ct_config.debug >= 4)
			ct_debug("ps_if_transmission_receive: received %u bytes:%s",
				 rc, ct_hexdump(rbuf, rc));
	}

	return rc;
}

/*
 * Receive all data reader want to send us. Reader will
 * switch to receive state after sending the last packet.
 */
static int ps_if_transmission_flush_reader_output_buffer(ifd_device_t * dev)
{
	unsigned char buffer[PS_BULK_RECEIVE_PACKET_SIZE];

	int rc = IFD_SUCCESS;
	ps_device_data_t *device_data = NULL;
	const size_t buffer_len = sizeof(buffer);
	const long timeout = 100;

	if (ct_config.debug >= 1)
		ct_debug("ps_if_transmission_flush_reader_output_buffer: called");

	device_data = (ps_device_data_t *) dev->user_data;

	do {
		rc = ifd_device_recv(dev, buffer, buffer_len, timeout);
	} while (rc > 0);

	/* clear possible sensitive information */
	memset(buffer, 0, buffer_len);

	return IFD_SUCCESS;
}

/*
 * Switch driver state to non transmission state.
 */
static int ps_if_transmission_end(ifd_device_t * dev)
{
	ps_device_data_t *device_data;

	if (ct_config.debug >= 1)
		ct_debug("ps_if_transmission_end: called");

	device_data = (ps_device_data_t *) dev->user_data;

	if (device_data->if_state != IDLE) {
		device_data->if_state = IDLE;
	}

	return IFD_SUCCESS;
}

/*
 * Send command to reader.
 */
static int
ps_send_to_ifd(ifd_reader_t * reader,
	       ps_instruction_t instruction,
	       const unsigned char *sbuf, size_t slen)
{
	unsigned char good_buffer[5 * PS_BULK_SEND_PACKET_SIZE];
	unsigned char *buffer = good_buffer;

	size_t buffer_len = sizeof(good_buffer);

	int rc = IFD_SUCCESS;
	ifd_device_t *dev = NULL;
	ps_device_data_t *device_data = NULL;

	size_t command_size = PS_COMMAND_HEADER_SIZE + slen;

 	if (ct_config.debug >= 1)
		ct_debug("ps_send_to_ifd: called");
	if (ct_config.debug >= 3)
		ct_debug("ps_send_to_ifd: sending %u bytes:%s", rc,
		         ct_hexdump(sbuf, slen));
	
	/* needs padding? */
	if (command_size % PS_BULK_SEND_PACKET_SIZE) {
		/* calculate padding */
		command_size = PS_BULK_SEND_PACKET_SIZE *
		    (1 + (command_size / PS_BULK_SEND_PACKET_SIZE));
	}

	dev = reader->device;
	device_data = (ps_device_data_t *) dev->user_data;

	if (PS_MAX_SEND_LEN < slen) {
		ct_error("ps_apdu_send: transmission is "
			 "larger than maximum allowed: %i",
		         slen);
		goto out;
	}

	if (buffer_len < command_size) {
		buffer = (unsigned char *)malloc(command_size);

		if (buffer == NULL) {
			rc = IFD_ERROR_NO_MEMORY;
			goto out;
		}

		buffer_len = command_size;
	}

	/* build command */

	/* header */
	buffer[PS_HEADER_IDX] = PS_HEADER;
	buffer[PS_INSTRUCTION_IDX] = instruction;
	buffer[PS_COMMAND_LENGTH0_IDX] = (unsigned char)((slen >> 8) & 0xff);
	buffer[PS_COMMAND_LENGTH1_IDX] = (unsigned char)(slen & 0xff);

	/* data */
	memcpy(&buffer[PS_COMMAND_HEADER_SIZE], sbuf, slen);

	/* padding */
	if (buffer_len > slen) {
		const size_t pad_start = slen + PS_COMMAND_HEADER_SIZE;
		memset(&buffer[pad_start], 0, buffer_len - pad_start);
	}

	/* send the first packet to reader */
	rc = ps_if_transmission_send(dev, buffer, command_size);

	if (command_size != rc) {
		/* something is wrong */
		if (rc >= 0) {
			/* didn't sent all the data to the reader */
			rc = IFD_ERROR_COMM_ERROR;
		}

		goto out;
	}

	/* return the length of data sent to reader */
	rc = slen;

      out:
	if (buffer != NULL) {

		/* clear possible sensitive information */
		memset(buffer, 0, buffer_len);
	}

	if (buffer != good_buffer) {
		free(buffer);
	}

	if (rc < 0 ) {
		device_data->if_state = ERROR;
		ct_error("ps_send_to_ifd: failed: %i", rc);
	} else {
	 	if (ct_config.debug >= 4)
			ct_debug("ps_send_to_ifd: sent %u bytes:%s",
			         slen, ct_hexdump(sbuf, slen));
	}

	return rc;
}

/*
 * Receive a response from reader
 *
 * (rbuf == NULL && rlen == 0) means caller wants no data,
 * just the reader status 
 */
static int
ps_receive_from_ifd(ifd_reader_t * reader, unsigned char *rbuf, size_t rlen)
{

	int rc = IFD_SUCCESS;
	ifd_device_t *dev = NULL;
	ps_device_data_t *device_data = NULL;
	ps_response_status_code_t status = PS_SUCCESS;
	size_t data_len = 0;
	size_t received = 0;

	unsigned char buffer[PS_BULK_RECEIVE_PACKET_SIZE];

	const size_t buffer_len = sizeof(buffer);

	if (ct_config.debug >= 1)
		ct_debug("ps_receive_from_ifd: called");

	dev = reader->device;
	device_data = (ps_device_data_t *) dev->user_data;

	if (rbuf == NULL && rlen > 0) {
		ct_error("ps_receive_from_ifd: rbuf == NULL");
		rc = IFD_ERROR_GENERIC;
		goto out;
	}

	/* receive the response header */
	rc = ps_if_transmission_receive(dev, buffer, buffer_len);

	if (rc < 0 )
		goto out;

	if (rc < PS_RESPONSE_HEADER_SIZE) {
		/* response header too small to contain a valid response header */
		rc = IFD_ERROR_COMM_ERROR;
		goto out;
	}

	/* interpret the response header */

	if (PS_HEADER != buffer[PS_HEADER_IDX]) {
		/* receive error */
		rc = IFD_ERROR_COMM_ERROR;
		goto out;
	}

	/* decode status word */

	status = buffer[PS_STATUS_IDX];

 	if (ct_config.debug >= 1)
		ct_debug("ps_receive_from_ifd: status = %#02x, %s\n",
		 	 status, ps_get_status_string(status));

	switch (status) {

	case PS_SUCCESS:
		break;

	case PS_SLOTERROR_ICC_NOT_INSERTED:
		rc = IFD_ERROR_NO_CARD;
		goto out;

	case PS_SLOTERROR_XFE_OVERRUN:
	case PS_SLOTERROR_XFE_PARITY_ERROR:
		rc = IFD_ERROR_COMM_ERROR;
		goto out;

	default:
		rc = IFD_ERROR_GENERIC;
		goto out;
	}

	/* decode data length */
	data_len = (buffer[PS_RESPONSE_LENGTH0_IDX] << 8) |
	    buffer[PS_RESPONSE_LENGTH1_IDX];

	if (data_len > rlen) {
		rc = IFD_ERROR_BUFFER_TOO_SMALL;
		goto out;
	}

	if (data_len > 0) {
		size_t remaining;
		/* copy data from first packet */
		received = rc - PS_RESPONSE_DATA_IDX;

		if (received > 0) {
			memcpy(rbuf, &buffer[PS_RESPONSE_DATA_IDX], received);
		}

		/* receive the remaining data */
		remaining = data_len - received;

		rc = ps_if_transmission_receive(dev,
						&rbuf[received], remaining);
		if (rc < 0)
			goto out;

		received += rc;

		if (received != data_len) {
			rc = IFD_ERROR_COMM_ERROR;
			goto out;
		}
	}

	/* return the length of received data */
	rc = received;

      out:

	memset(buffer, 0, buffer_len);

	if (rc < 0) {
		ps_if_transmission_flush_reader_output_buffer(dev);
		device_data->if_state = ERROR;
		ct_error("ps_receive_from_ifd: failed: %i", rc);
	} else {
	 	if (ct_config.debug >= 3)
			ct_debug("ps_if_transmission_receive: "
			         "received %u bytes:%s", rc,
			         ct_hexdump(rbuf, rc));
	}

	return rc;
}

/*
 * Send an command and receive the response.
 */
static int
ps_transceive_instruction(ifd_reader_t * reader,
			  ps_instruction_t instruction,
			  const unsigned char *sbuf,
			  size_t slen, unsigned char *rbuf, size_t rlen)
{
	int rc = IFD_SUCCESS;
	ifd_device_t *dev = NULL;
	
	if (ct_config.debug >= 1)
		ct_debug("ps_transceive_instruction: called");

	dev = reader->device;

	if (rc == IFD_SUCCESS) {
		/* start the transmission */
		rc = ps_if_transmission_start(dev, dev->timeout);

		if (IFD_SUCCESS == rc) {
			/* send the data */
			rc = ps_send_to_ifd(reader, instruction, sbuf, slen);

			if (rc >= 0) {
				/* receive the data from the reader */
				rc = ps_receive_from_ifd(reader, rbuf, rlen);
			}

			ps_if_transmission_end(dev);
		}
	}

	if (rc < 0) 
		ct_error("ps_transceive_instruction: failed: %i", rc);

	return rc;
}

/*
 * Activate the reader.
 */
static int ps_activate(ifd_reader_t * reader)
{
	if (ct_config.debug >= 1)
		ct_debug("ps_activate: called");
	return IFD_SUCCESS;
}

/*
 * Deactivate the reader.
 */
static int ps_deactivate(ifd_reader_t * reader)
{
	int rc;

	if (ct_config.debug >= 1)
		ct_debug("ps_deactivate: called");

	rc = ps_transceive_instruction(reader, PS_POWER_OFF, NULL, 0, NULL, 0);

	if (rc < 0) {
		ct_error("ps_deactivate: failed: %i", rc);
	}

	return rc;
}

/*
 * Get the current reader status.
 */
static int ps_get_stat(ifd_reader_t * reader, ps_stat_t * stat)
{
	int rc;
	unsigned char buffer[16];
	unsigned char *p;

	if (ct_config.debug >= 1)
		ct_debug("ps_get_stat: called");

	rc = ps_transceive_instruction(reader, PS_GET_ACR_STAT,
				       NULL, 0, buffer, sizeof(buffer));

	if (rc < 0 ) 
		goto failed;

	if (rc < sizeof(buffer)) {
		rc = IFD_ERROR_COMM_ERROR;
		goto failed;
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

	return IFD_SUCCESS;

      failed:
	ct_error("ps_get_stat: failed: %i", rc);
	return rc;
}

/*
 * Get the current card status.
 */
static int ps_card_status(ifd_reader_t * reader, int slot, int *status)
{
	int rc;
	ifd_device_t *dev;
	ps_device_data_t *device_data;
	ps_card_status_t card_status;
	int status_tmp = 0;

	if (ct_config.debug >= 1)
		ct_debug("ps_card_status: called");

	if (slot != 0) {
		ct_error("ps_card_status: bad slot index %u", slot);
		return IFD_ERROR_INVALID_SLOT;
	}

	dev = reader->device;

	device_data = (ps_device_data_t *) dev->user_data;
	card_status = device_data->card_status;

	if (card_status == PS_CARD_UNKNOWN) {
		ps_stat_t stat;

		/* ask the current status to the reader */
		memset(&stat, 0, sizeof(stat));
		rc = ps_get_stat(reader, &stat);

		if (rc >= 0) {
			if (stat.c_stat) {
				device_data->card_status = PS_CARD_INSERTED;
				status_tmp = IFD_CARD_STATUS_CHANGED;
			} else {
				device_data->card_status = PS_CARD_NOT_INSERTED;
			}
		}
	} else {
		unsigned char packet_buf[PS_INTERRUPT_URB_DATA_SIZE];
		const size_t packet_buf_len = sizeof(packet_buf);

		/* read notifications received from the reader */

		for (;;) {
			int inserted = FALSE;
			
			rc = ifd_usb_capture(dev,
					     device_data->capture,
					     packet_buf,
					     packet_buf_len,
					     PS_INTERRUPT_TIMEOUT);

			if (IFD_ERROR_TIMEOUT == rc) {
				rc = IFD_SUCCESS;
				break;
			}

			if (rc < 0) 
				break;

			if (0 == memcmp(packet_buf,
					PS_CARD_INSERTED_NOTIFICATION,
					sizeof(PS_CARD_INSERTED_NOTIFICATION)))
			{
				inserted = TRUE;
			} else if (0 != memcmp(packet_buf,
					       PS_CARD_REMOVED_NOTIFICATION,
					       sizeof
					       (PS_CARD_REMOVED_NOTIFICATION)))
			{
				continue;
			}

			if (inserted) {
				if (device_data->card_status != PS_CARD_INSERTED) {
					device_data->card_status = PS_CARD_INSERTED;
					status_tmp = IFD_CARD_STATUS_CHANGED;
				}
			} else {
				if (device_data->card_status != PS_CARD_NOT_INSERTED) {
					device_data->card_status = PS_CARD_NOT_INSERTED;
					status_tmp = IFD_CARD_STATUS_CHANGED;
				}
			}
		}
	}

	if (rc < 0) {
		if (ct_config.debug >= 1)
			ct_debug("ps_card_status: failed: %i", rc);
	} else {
		if (device_data->card_status == PS_CARD_INSERTED) {
			status_tmp |= IFD_CARD_PRESENT;
		}
		*status = status_tmp;
	}

	return rc;
}

/*
 * Reset card and select the protocol
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

	/* protocol changed? */
	if (NULL == slot->proto || device_data->cur_icc_proto != new_icc_proto) {

		switch (new_icc_proto) {

		case IFD_PROTOCOL_DEFAULT:
			if (ct_config.debug >= 1)
				ct_debug("ps_card_reset_select_protocol: "
				         "using automatic protocol selection");
			sbuf[0] = PS_AUTO_T0_OR_T1_CARD_TYPE;
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
			ct_error("ps_card_reset_select_protocol: unknow or "
				 "unsupported protocol %i", new_icc_proto);
			return IFD_ERROR_NOT_SUPPORTED;
		}

		/* power off the card, the reader does PPS negotiation
		   after the next power up */
		rc = ps_transceive_instruction(reader, PS_POWER_OFF, NULL, 0,
					       NULL, 0);

		if (IFD_SUCCESS != rc) {
			ct_error("ps_card_reset_select_protocol: "
				 "failed (PS_POWER_OF): %i", rc);
			return rc;
		}

		rc = ps_transceive_instruction(reader, PS_SELECT_CARD_TYPE,
					       sbuf, sizeof(sbuf), NULL, 0);

		if (IFD_SUCCESS != rc) {
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

	{
		int status;

		ps_card_status(reader, nslot, &status);
	}

	/* the reader did PPS negotiation with the card
	   parse the atr to check the protocol negotiated by the reader */

	rc = ifd_atr_parse(&atr_info, atr, atr_len);

	if (rc < 0) {
		ct_error("ps_card_reset_select_protocol: %s: Bad ATR",
			 reader->name);
		return rc;
	}

	if (-1 != atr_info.TA[1]) {
		/* specific mode */
		if (ct_config.debug >= 1)
			ct_debug("ps_card_reset_select_protocol: "
		                 "card in specific mode %#02x",
				 atr_info.TA[1] & 0x0f);
		new_icc_proto = atr_info.TA[1] & 0x0f;
	} else if (IFD_PROTOCOL_DEFAULT == new_icc_proto) {
		new_icc_proto = atr_info.default_protocol;
	} else if (!(atr_info.supported_protocols & (1 << new_icc_proto))) {
		ct_error("Protocol not supported by card (according to ATR)");
		return IFD_ERROR_NOT_SUPPORTED;
	}

	if (NULL == slot->proto || device_data->cur_icc_proto != new_icc_proto) {

		if (NULL != slot->proto) {
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
						   TA[2] : PS_DEFAULT_T1_IFSC);
			ifd_protocol_set_parameter(slot->proto,
						   IFD_PROTOCOL_T1_IFSD,
						   PS_MAX_T1_IFSD);
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

/*
 * Reset card
 */
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

	if (NULL == slot->proto || device_data->cur_icc_proto != proto) {

		/* the reader negotiates the protocol during the card power up
		   must power down and reset the card to change it */
		rc = ps_card_reset_select_protocol(reader, nslot,
						   slot->atr,
						   sizeof(slot->atr), proto);

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

/*
 * Send an apdu to reader.
 */
static int
ps_apdu_send(ifd_reader_t * reader, unsigned int dad,
	     const unsigned char *sbuf, size_t slen)
{
	int rc;
	ifd_device_t *dev;
	ps_device_data_t *device_data;
	ps_instruction_t instruction;

	if (ct_config.debug >= 1) 
		ct_debug("ps_apdu_send: called");
	if (ct_config.debug >= 3) 
		ct_debug("ps_apdu_send: sending %i: %s",
		         slen, ct_hexdump(sbuf, slen));

	dev = reader->device;
	device_data = (ps_device_data_t *) dev->user_data;

	switch (device_data->cur_icc_proto) {

	case IFD_PROTOCOL_T0:

		if (ct_config.debug >= 1)
			ct_debug("ps_apdu_send: using EXCHANGE_TPDU_T0");

		instruction = PS_EXCHANGE_TPDU_T0;

		break;

	case IFD_PROTOCOL_T1:
		
		if (ct_config.debug >= 1)
			ct_debug("ps_apdu_send: using EXCHANGE_TPDU_T1");

		instruction = PS_EXCHANGE_TPDU_T1;

		break;

	default:

		if (ct_config.debug >= 1)
			ct_debug("ps_apdu_send: unknow protocol");
		return IFD_ERROR_GENERIC;
	}

	/* start the transmission */
	rc = ps_if_transmission_start(dev, dev->timeout);

	if (IFD_SUCCESS == rc) {

		rc = ps_send_to_ifd(reader, instruction, sbuf, slen);

	}

	if (0 > rc) {
		ct_error("ps_apdu_send: error %i", rc);
	}

	return rc;
}

/*
 * Receive an apdu from reader.
 */
static int
ps_apdu_recv(ifd_reader_t * reader, unsigned int dad, unsigned char *buffer,
	     size_t len, long timeout)
{
	int rc = IFD_SUCCESS;
	ifd_device_t *dev = NULL;
	ps_device_data_t *dev_data = NULL;

	if (ct_config.debug >= 1)
		ct_debug("ps_apdu_recv: called");

	dev = reader->device;
	dev_data = (ps_device_data_t *) dev->user_data;

	rc = ps_receive_from_ifd(reader, buffer, len);

	if (rc < 0) {
		ct_error("ps_apdu_recv: failed");
	} else {
		if (ct_config.debug >= 3)
			ct_debug("ps_apdu_recv: received %i bytes: %s",
			         rc, ct_hexdump(buffer, rc));
	}

	ps_if_transmission_end(dev);

	return rc;
}

/*
 * Initialize the device
 */
static int ps_open(ifd_reader_t * reader, const char *device_name)
{
	int rc = IFD_SUCCESS;
	ifd_device_t *dev = NULL;
	ps_device_data_t *device_data = NULL;
	ifd_device_params_t params;

	unsigned char sbuf[1];

	if (ct_config.debug >= 1)
		ct_debug("ps_open: called: device name =%s", device_name);

	dev = ifd_device_open(device_name);

	if (NULL == dev) {
		ct_error("ps_open: failed to open device: %", device_name);
		rc = IFD_ERROR_GENERIC;
		goto out;
	}

	switch (dev->type) {

	case IFD_DEVICE_TYPE_USB:
		reader->name = PS_USB_READER_NAME;
		break;

	default:
		ct_error("ps_open: unknow device type %i", dev->type);
		rc = IFD_ERROR_GENERIC;
		goto out;
	}

	/* set usb interface parameters */

	params = dev->settings;

	params.usb.interface = PS_USB_INTERFACE_INDEX;
	params.usb.ep_intr = PS_USB_INTERRUPT_ENDPOINT_ADDRESS;
	params.usb.ep_o = PS_USB_BULK_OUTPUT_ENDPOINT_ADDRESS;
	params.usb.ep_i = PS_USB_BULK_INPUT_ENDPOINT_ADDRESS;

	rc = ifd_device_set_parameters(dev, &params);

	if (IFD_SUCCESS > rc) {
		ct_error("ps_open: ifd_device_set_parameters returned error %i",
			 rc);
		goto out;
	}

	device_data = (ps_device_data_t *) calloc(1, sizeof(*device_data));

	if (NULL == device_data) {
		ct_error("ps_open: not enough memory");
		rc = IFD_ERROR_NO_MEMORY;
		goto out;
	}

	memset(device_data, 0, sizeof(*device_data));

	device_data->if_state = IDLE;
	device_data->card_status = PS_CARD_UNKNOWN;
	device_data->cur_icc_proto = IFD_PROTOCOL_DEFAULT;

	reader->nslots = 1;
	reader->device = dev;
	reader->device->user_data = device_data;
	reader->device->timeout = PS_BULK_TIMEOUT;

	/* set reader mode */
	sbuf[0] = PS_OPTION_EMV_MODE_ON;

	rc = ps_transceive_instruction(reader, PS_SET_OPTION, sbuf, 1, NULL, 0);

	if (IFD_SUCCESS > rc) {
		ct_error("ps_open: error setting reader option");
		goto out;
	}

	rc = ifd_usb_begin_capture(dev,
				   IFD_USB_URB_TYPE_INTERRUPT,
				   params.usb.ep_intr,
				   PS_INTERRUPT_URB_DATA_SIZE,
				   &(device_data->capture));

      out:

	if (IFD_SUCCESS > rc && NULL != dev) {
		ifd_device_close(dev);
	}

	return rc;
}

/*
 * Free resources used by reader.
 */
static int ps_close(ifd_reader_t * reader)
{
	ifd_device_t *dev;
	ps_device_data_t *device_data;

	if (ct_config.debug >= 1)
		ct_debug("ps_close: called");

	dev = reader->device;
	device_data = (ps_device_data_t *) dev->user_data;

	ps_deactivate(reader);

	if (NULL != device_data->capture) {
		ifd_usb_end_capture(dev, device_data->capture);
		device_data->capture = NULL;
	}

	free(device_data);

	ifd_device_close(dev);

	return 0;
}

/*
 * Initialize this module
 */
void ifd_pertosmart_ac1038_register(void)
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

	ifd_driver_register("pertosmart1038", &perto_smart_driver);
}
