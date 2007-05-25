/*
 * ACR30U driver 
 * Copyright (C) 2005, Laurent Pinchart <laurent.pinchart@skynet.be>
 */

#include "internal.h"
#include "usb-descriptors.h"

#include <stdlib.h>
#include <string.h>

/* Maximum buffer sizes
 *
 * The send buffer must be able to contain a short APDU. In the worst case
 * (Case 4S, Lc = 255), the APDU will be 5 (CLA + INS + P1 + P2 + Lc) +
 * 255 (Data) + 1 (Le) = 261 bytes long. The send buffer must then be big
 * enough to contain an extended command with 261 bytes of data, which gives
 * us a total of 5 (HDR + INS + LEN) + 261 (APDU) + 1 (CHK) = 267 bytes.
 *
 * The receive buffer must be able to contain a short APDU response. In the
 * worst case (Case 2S or Case 4S, Le = 256), the APDU will be 256 (Data) +
 * 2 (SW1, SW2) = 258 bytes long. The receive buffer must then be big enough
 * to contain an extended response with 258 bytes of data, which gives us a
 * total of 6 (HDR + SW1 + SW2 + LEN) + 258 (APDU) + 1 (CHK) = 265 bytes.
 * 
 * The ASCII buffer must be able to contain either the command or the reply
 * with the STX and ETX bytes.
 */
typedef struct acr_priv {
	int icc_proto;
	unsigned char sw1, sw2;
	unsigned char abuf[267 * 2 + 2];
	unsigned char sbuf[267];
	unsigned char rbuf[265];
	unsigned int head, tail;
} acr_priv_t;

typedef int complete_fn_t(const void *, size_t);

#define ACR_GET_STATUS			0x01
#define ACR_SELECT_CARD_TYPE		0x02
#define ACR_SET_PROTOCOL		0x03
#define ACR_SET_NOTIFICATION		0x06
#define ACR_SET_OPTION			0x07
#define ACR_RESET			0x80
#define ACR_POWER_OFF			0x81
#define ACR_ACTIVATE_SAM		0X88
#define ACR_DEACTIVATE_SAM		0X89
#define ACR_READ_DATA			0x90
#define ACR_WRITE_DATA			0x91
#define ACR_PRESENT_CODE		0x92
#define ACR_CHANGE_CODE			0X93
#define ACR_WRITE_PROTECTION		0x94
#define ACR_EXCHANGE_APDU		0xa0
#define ACR_EXCHANGE_T1			0xa1
#define ACR_EXCHANGE_SAM_APDU		0xb0
#define ACR_EXCHANGE_SAM_T1		0xb1

#define ACR_CARD_AUTO			0x00
#define ACR_CARD_GPM103			0x01
#define ACR_CARD_I2C			0x02
#define ACR_CARD_SLE44x8		0x05
#define ACR_CARD_SLE44x2		0x06
#define ACR_CARD_MCU_T0			0x0c
#define ACR_CARD_MCU_T1			0x0d
#define ACR_CARD_SAM_T0			0xc0
#define ACR_CARD_SAM_T1			0xd0

#define ACR_STATUS_DATA_ERROR		0x60
#define ACR_STATUS_COMMAND_ERROR	0x67
#define ACR_STATUS_OK			0x90
#define ACR_STATUS_STATUS		0xff

#define ACR_STATUS_LENGTH		16

/*
 * Send USB control message, and receive data via
 * Interrupt URBs.
 */
static int
acr_usb_int(ifd_device_t * dev, int requesttype, int request,
	    int value, int idx,
	    const void *sbuf, size_t slen,
	    void *rbuf, size_t rlen, complete_fn_t complete, long timeout)
{
	ifd_usb_capture_t *cap;
	struct timeval begin;
	unsigned int total = 0;
	unsigned char *etx;
	int rc;

	if (timeout < 0)
		timeout = dev->timeout;

	rc = ifd_usb_begin_capture(dev,
				   IFD_USB_URB_TYPE_INTERRUPT, 0x81, 8, &cap);
	if (rc < 0)
		return rc;

	gettimeofday(&begin, NULL);
	ifd_debug(3, "sending %u bytes:%s", slen, ct_hexdump(sbuf, slen));
	rc = ifd_usb_control(dev, requesttype, request,
			     value, idx, (void *)sbuf, slen, timeout);
	if (rc < 0)
		goto out;

	/* Capture URBs until we have a complete answer */
	while (rc >= 0 && total < rlen) {
		unsigned char temp[8];
		long wait;

		wait = timeout - ifd_time_elapsed(&begin);
		if (wait <= 0)
			return IFD_ERROR_TIMEOUT;
		memset(temp, 0, sizeof temp);
		rc = ifd_usb_capture(dev, cap, temp, sizeof(temp), wait);
		if (rc > 0) {
			if (rc > (int)(rlen - total))
				rc = rlen - total;
			memcpy((caddr_t) rbuf + total, temp, rc);
			total += rc;

			if (complete && complete(rbuf, total))
				break;
		}
	}

	/* Why does the USB spec provide short packets if the device doesn't
	 * use them ? Every interrupt URB contains 8 bytes, which means we
	 * must discard everything after the ETX marker or we will end up
	 * with garbage.
	 */
	if ((etx = memchr(rbuf, 0x03, total)) != NULL)
		total = etx - (unsigned char *)rbuf + 1;

	if (rc >= 0) {
		ifd_debug(3, "received %u bytes:%s", total,
			  ct_hexdump(rbuf, total));
		rc = total;
	}

      out:
	ifd_usb_end_capture(dev, cap);
	return rc;
}

static int acr_reply_complete(const void *ptr, size_t len)
{
	return memchr(ptr, 0x03, len) != NULL;
}

/*
 * Transmit a command to the reader.
 */
static int
acr_transmit(ifd_reader_t * reader,
	     const void *sbuf, size_t slen, void *rbuf, size_t rlen)
{
	static const char acr_hex[16] = "0123456789ABCDEF";

	acr_priv_t *priv = (acr_priv_t *) reader->driver_data;
	ifd_device_t *dev = reader->device;
	unsigned char *abuf = priv->abuf;
	unsigned char *cbuf = (unsigned char *)sbuf;
	unsigned char checksum;
	unsigned char c;
	int requesttype;
	int rc;
	int i;

	if (slen > 261)
		return IFD_ERROR_GENERIC;

	/* Convert the command to ascii and compute the checksum */
	*abuf++ = 0x02;

	checksum = 0x01;
	*abuf++ = '0';
	*abuf++ = '1';

	for (i = 0; i < (int)slen; ++i) {
		checksum ^= cbuf[i];
		*abuf++ = acr_hex[cbuf[i] >> 4];
		*abuf++ = acr_hex[cbuf[i] & 0x0f];
	}

	*abuf++ = acr_hex[checksum >> 4];
	*abuf++ = acr_hex[checksum & 0x0f];
	*abuf++ = 0x03;

	/* Transmit the encoded command */
	requesttype = IFD_USB_RECIP_DEVICE
	    | IFD_USB_TYPE_VENDOR | IFD_USB_ENDPOINT_OUT;
	rc = acr_usb_int(dev, requesttype, 0, 0, 0, priv->abuf, slen * 2 + 6,
			 priv->abuf, sizeof priv->abuf, acr_reply_complete, -1);

	if (rc <= 0)
		return rc;

	if (rc < 12) {
		ct_error("acr: communication error: short response received");
		return IFD_ERROR_COMM_ERROR;
	}

	/* Decode and verify the response */
	abuf = priv->abuf;
	if (abuf[0] != 0x02 || abuf[rc - 1] != 0x03) {
		ifd_debug(1, "data: %s", ct_hexdump(abuf, rc));
		ct_error("acr: communication error: invalid header/footer");
		return IFD_ERROR_COMM_ERROR;
	}

	abuf++;
	rc = (rc - 2) / 2;
	checksum = 0;

	for (i = 0; i < rc; ++i) {
		*abuf -= '0';
		if (*abuf > 9)
			*abuf -= 'A' - '0' - 10;
		c = (*abuf++ & 0x0f) << 4;
		*abuf -= '0';
		if (*abuf > 9)
			*abuf -= 'A' - '0' - 10;
		c += *abuf++ & 0x0f;

		priv->abuf[i] = c;
		checksum ^= c;
	}

	if (checksum != 0) {
		ct_error("acr: communication error: invalid checksum");
		return IFD_ERROR_COMM_ERROR;
	}

	priv->sw1 = priv->abuf[1];
	priv->sw2 = priv->abuf[2];
	if (priv->abuf[3] == 0xff) {
		rc = (priv->abuf[4] << 8) + priv->abuf[5];
		abuf = &priv->abuf[6];
	} else {
		rc = priv->abuf[3];
		abuf = &priv->abuf[4];
	}

	if (rc > (int)rlen) {
		ifd_debug(1, "received more data than requested, "
			  "discarding data: %s", ct_hexdump(abuf, rlen - rc));
	}

	rc = rc > (int)rlen ? (int)rlen : rc;
	memcpy(rbuf, abuf, rc);
	return rc;
}

/*
 * Read the reader status
 */
static int acr_reader_status(ifd_reader_t * reader, void *status, size_t len)
{
	unsigned char cmd[2];
	int rc;

	cmd[0] = ACR_GET_STATUS;
	cmd[1] = 0x00;

	rc = acr_transmit(reader, &cmd, sizeof cmd, status, len);
	if (rc < 0)
		return rc;

	if (rc != ACR_STATUS_LENGTH) {
		ct_error("acr: invalid status length");
		return IFD_ERROR_COMM_ERROR;
	}

	return rc;
}

/*
 * Initialize the device
 */
static int acr_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	acr_priv_t *priv;
	ifd_device_params_t params;
	unsigned char status[ACR_STATUS_LENGTH];
	int rc;

	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("acr30u: device %s is not a USB device", device_name);
		ifd_device_close(dev);
		return -1;
	}

	params = dev->settings;
	params.usb.interface = 0;
	if (ifd_device_set_parameters(dev, &params) < 0) {
		ct_error("acr30u: setting parameters failed", device_name);
		ifd_device_close(dev);
		return -1;
	}
	priv = (acr_priv_t *) calloc(1, sizeof(acr_priv_t));

	reader->driver_data = priv;
	reader->device = dev;
	reader->name = NULL;
	reader->nslots = 1;

	dev->timeout = 2000;

	if ((rc = acr_reader_status(reader, status, sizeof status)) < 0)
		return rc;

	/* strndup is a GNU extension, so it might not be available on all
	 * the target platforms. Use malloc and memcpy instead.
	 */
	reader->name = (char *)calloc(1, 11);
	memcpy((char *)reader->name, status, 10);
	ifd_debug(1, "found %s reader.", reader->name);
	ifd_debug(1, "supported cards: %02x%02x", status[12], status[13]);

	return 0;
}

/*
 * Close the device
 */
static int acr_close(ifd_reader_t * reader)
{
	free((char *)reader->name);
	free(reader->driver_data);
	return 0;
}

/*
 * Power up the reader - always powered up.
 * TODO: What about an USB standby mode ?
 */
static int acr_activate(ifd_reader_t * reader)
{
	ifd_debug(1, "called.");
	return 0;
}

static int acr_deactivate(ifd_reader_t * reader)
{
	ifd_debug(1, "called.");
	return -1;
}

/*
 * Card status
 */
static int acr_card_status(ifd_reader_t * reader, int slot, int *status)
{
	unsigned char acr_status[ACR_STATUS_LENGTH];
	int rc;

	*status = 0;

	if ((rc = acr_reader_status(reader, acr_status, sizeof acr_status)) < 0) {
		ct_error("acr: failed to get card status");
		return -1;
	}

	ifd_debug(2, "C_SEL: %02x C_STAT: %02x",
		  acr_status[14], acr_status[15]);

	if (acr_status[15])
		*status = IFD_CARD_PRESENT;

	ifd_debug(2, "card %spresent", *status ? "" : "not ");
	return 0;
}

/*
 * Reset
 */
static int
acr_card_reset(ifd_reader_t * reader, int slot, void *atr, size_t size)
{
	unsigned char buffer[IFD_MAX_ATR_LEN];
	unsigned char cmd[2];
	int rc;

	cmd[0] = ACR_RESET;
	cmd[1] = 0x00;

	rc = acr_transmit(reader, &cmd, sizeof cmd, buffer, sizeof buffer);
	if (rc < 0)
		return rc;

	if (rc < (int)size)
		size = rc;

	memcpy(atr, buffer, size);
	return size;
}

/*
 * Select a protocol for communication with the ICC.
 */
static int acr_set_protocol(ifd_reader_t * reader, int nslot, int proto)
{
	ifd_slot_t *slot;
	acr_priv_t *priv;
	unsigned char cmd[3];
	int rc;

	ifd_debug(1, "called, proto=%d", proto);

	cmd[0] = ACR_SELECT_CARD_TYPE;
	cmd[1] = 0x01;

	switch (proto) {
	case IFD_PROTOCOL_T0:
		cmd[2] = ACR_CARD_MCU_T0;
		break;
	case IFD_PROTOCOL_T1:
		cmd[2] = ACR_CARD_MCU_T1;
		break;
	default:
		return IFD_ERROR_NOT_SUPPORTED;
	}

	if ((rc = acr_transmit(reader, cmd, sizeof cmd, NULL, 0)) < 0) {
		ct_error("acr: unable to set the protocol");
		return IFD_ERROR_COMM_ERROR;
	}

	slot = &reader->slot[nslot];
	slot->proto = ifd_protocol_new(proto, reader, slot->dad);
	if (slot->proto == NULL) {
		ct_error("acr: unable to create protocol");
		return -1;
	}
	ifd_protocol_set_parameter(slot->proto, IFD_PROTOCOL_BLOCK_ORIENTED, 1);

	priv = (acr_priv_t *) reader->driver_data;
	priv->icc_proto = proto;

	return 0;
}

/*
 * Send/receive routines
 */
static int
acr_send_t0(ifd_reader_t * reader, unsigned int dad, const unsigned char *sbuf,
	    size_t slen)
{
	acr_priv_t *priv = (acr_priv_t *) reader->driver_data;
	ifd_iso_apdu_t iso;
	int rc;

	if (slen > 260)
		return IFD_ERROR_GENERIC;

	/* The reader expects Lc and Le to always be present, so fix the
	 * APDU to add a null Le byte for Case 1, Case 3S and Case 4S and
	 * insert a null Lc byte for Case 2S. The T=0 protocol handler already
	 * took care of inserting a null Lc byte for Case 1, and removed the
	 * Le byte for Case 4S.
	 */
	priv->sbuf[0] = ACR_EXCHANGE_APDU;
	priv->sbuf[1] = slen + 1;
	memcpy(&priv->sbuf[2], sbuf, slen);

	if ((rc = ifd_iso_apdu_parse(sbuf, slen, &iso)) < 0)
		return rc;

	if (iso.cse == IFD_APDU_CASE_2S) {
		priv->sbuf[slen + 2] = priv->sbuf[slen + 1];
		priv->sbuf[slen + 1] = 0;
	} else
		priv->sbuf[slen + 2] = 0;

	priv->head = priv->tail = 0;
	rc = acr_transmit(reader, priv->sbuf, slen + 3, priv->rbuf,
			  sizeof priv->rbuf);
	if (rc >= 0) {
		priv->tail = rc;
		rc = slen;
	}
	return rc;
}

static int
acr_send(ifd_reader_t * reader, unsigned int dad, const unsigned char *buffer,
	 size_t len)
{
	acr_priv_t *priv = (acr_priv_t *) reader->driver_data;

	switch (priv->icc_proto) {
	case IFD_PROTOCOL_T0:
		return acr_send_t0(reader, dad, buffer, len);
	}

	return IFD_ERROR_NOT_SUPPORTED;
}

static int
acr_recv(ifd_reader_t * reader, unsigned int dad, unsigned char *buffer,
	 size_t len, long timeout)
{
	acr_priv_t *priv = (acr_priv_t *) reader->driver_data;

	switch (priv->icc_proto) {
	case IFD_PROTOCOL_T0:
		if (priv->tail - priv->head < len)
			len = priv->tail - priv->head;
		memcpy(buffer, priv->rbuf + priv->head, len);
		priv->head += len;
		return len;
	}
	return IFD_ERROR_NOT_SUPPORTED;	/* not yet */
}

#if 0
/*
 * Set the card's baud rate etc
 */
static int acr_set_card_parameters(ifd_device_t * dev, unsigned int baudrate)
{
	return 0;
}
#endif

/*
 * Driver operations
 */
static struct ifd_driver_ops acr30u_driver;

/*
 * Initialize this module
 */
void ifd_acr30u_register(void)
{
	memset(&acr30u_driver, 0, sizeof acr30u_driver);

	acr30u_driver.open = acr_open;
	acr30u_driver.close = acr_close;
	acr30u_driver.activate = acr_activate;
	acr30u_driver.deactivate = acr_deactivate;
	acr30u_driver.card_status = acr_card_status;
	acr30u_driver.card_reset = acr_card_reset;
	acr30u_driver.send = acr_send;
	acr30u_driver.recv = acr_recv;
	acr30u_driver.set_protocol = acr_set_protocol;

	ifd_driver_register("acr30u", &acr30u_driver);
}
