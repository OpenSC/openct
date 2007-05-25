/*
 * OMNIKEY CardMan 2020/6020/6120 driver
 * This driver is not yet complete, but at least it
 * spits out the ATR already.
 * Needs a recentish Linux Kernel (2.4.5 does NOT work)
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 *
 * Based on information from the cm2020 driver by
 * Omnikey AG.
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct cm_priv {
	int icc_proto;
	unsigned char rbuf[64];
	unsigned int head, tail;
} cm_priv_t;

typedef int complete_fn_t(const void *, size_t);
static int cm_set_card_parameters(ifd_device_t *, unsigned int baudRate);
static int cm_transceive_t0(ifd_reader_t * reader,
			    const void *sbuf, size_t slen,
			    void *rbuf, size_t rlen);
static int cm_usb_int(ifd_device_t * dev, int requesttype, int request,
		      int value, int idx,
		      const void *sbuf, size_t slen,
		      void *rbuf, size_t rlen,
		      complete_fn_t check, long timeout);
static int cm_anyreply(const void *, size_t);

/*
 * Initialize the device
 */
static int cm_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	cm_priv_t *priv;
	ifd_device_params_t params;

	reader->name = "OMNIKEY CardMan 2020/6020/6120";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("cardman: device %s is not a USB device", device_name);
		ifd_device_close(dev);
		return -1;
	}

	params = dev->settings;
	params.usb.interface = 0;
	if (ifd_device_set_parameters(dev, &params) < 0) {
		ifd_device_close(dev);
		return -1;
	}
	priv = (cm_priv_t *) calloc(1, sizeof(cm_priv_t));
	if (!priv) {
		ct_error("out of memory");
		return IFD_ERROR_NO_MEMORY;
	}

	reader->driver_data = priv;
	reader->device = dev;
	dev->timeout = 2000;

	return 0;
}

/*
 * Power up the card slot
 */
static int cm_activate(ifd_reader_t * reader)
{
	ifd_device_t *dev = reader->device;
	int rc;

	ifd_debug(1, "called.");
	/* Set async card @9600 bps, 2 stop bits, even parity */
	if ((rc = cm_set_card_parameters(dev, 0x01)) < 0) {
		ct_error("cardman: failed to set card parameters 9600/8E2");
		return rc;
	}
	return 0;
}

static int cm_deactivate(ifd_reader_t * reader)
{
	ifd_device_t *dev = reader->device;
	int rc;

	ifd_debug(1, "called.");
	if ((rc = ifd_usb_control(dev, 0x42, 0x11, 0, 0, NULL, 0, -1)) < 0) {
		ct_error("cardman: failed to deactivate card");
		return rc;
	}
	return 0;
}

/*
 * Card status - always present
 */
static int cm_card_status(ifd_reader_t * reader, int slot, int *status)
{
	ifd_device_t *dev = reader->device;
	unsigned char cm_status = 0;
	int rc;

	*status = 0;

	if ((rc =
	     cm_usb_int(dev, 0x42, 0x20, 0, 0, NULL, 0, &cm_status, 1, NULL,
			-1)) < 0) {
		ct_error("cardman: failed to get card status");
		return -1;
	}
	if (rc == 1 && (cm_status & 0x42))
		*status = IFD_CARD_PRESENT;
	ifd_debug(1, "card %spresent", *status ? "" : "not ");
	return 0;
}

/*
 * Reset
 */
static int cm_card_reset(ifd_reader_t * reader, int slot, void *atr,
			 size_t size)
{
	ifd_device_t *dev = reader->device;
	unsigned char buffer[IFD_MAX_ATR_LEN];
	int n;

	/* Request the ATR */
	if ((n = cm_usb_int(dev, 0x42, 0x10, 1, 0, NULL, 0,
			    buffer, sizeof(buffer),
			    (complete_fn_t *) ifd_atr_complete, -1)) < 0) {
		ct_error("cardman: failed to reset card");
		return n;
	}

	/* XXX Handle inverse convention, odd parity, etc */

	if ((size_t) n > size)
		n = size;
	memcpy(atr, buffer, n);
	return n;
}

/*
 * Select a protocol for communication with the ICC.
 */
static int cm_set_protocol(ifd_reader_t * reader, int nslot, int proto)
{
	ifd_device_t *dev = reader->device;
	ifd_slot_t *slot;
	cm_priv_t *priv;
	unsigned char pts[4], reply[4];
	unsigned int baudRate;
	int n;

	ifd_debug(1, "called, proto=%d", proto);

	pts[0] = 0xFF;
	switch (proto) {
	case IFD_PROTOCOL_T0:
		pts[1] = 0x10;
		pts[2] = 0x11;
		break;
	case IFD_PROTOCOL_T1:
		pts[1] = 0x11;
		/* XXX select Fi/Di according to TA1 */
		pts[2] = 0x11;
		break;
	default:
		return IFD_ERROR_NOT_SUPPORTED;
	}
	pts[3] = pts[0] ^ pts[1] ^ pts[2];

	/* Send the PTS bytes */
	if ((n =
	     cm_usb_int(dev, 0x42, 1, 0, 0, pts, 4, reply, 2, NULL, -1)) < 0) {
		ct_error("cardman: failed to send PTS");
		return n;
	}
	if (reply[0] != 4) {
		ct_error("cardman: card refused PTS");
		return IFD_ERROR_COMM_ERROR;
	}
#ifdef notyet
	/* Receive PTS response */
	if ((n = ifd_usb_control(dev, 0xC2, 0, 0, 0, reply, 4, -1)) < 0) {
		ct_error("cardman: failed to receive PTS response");
		return n;
	}
	if (n != 4) {
		ct_error("cardman: received short PTS response (%u bytes)", n);
		return IFD_ERROR_COMM_ERROR;
	}
	if (memcmp(pts, reply, 4)) {
		ct_error("cardman: PTS reply does not match request", n);
		return IFD_ERROR_COMM_ERROR;
	}
#endif

	baudRate = pts[2] & 0xf;
	/* Select f=5.12 MHz */
	if ((pts[2] & 0xF0) == 0x90)
		baudRate |= 0x10;
	if ((n = cm_set_card_parameters(dev, baudRate)) < 0) {
		ct_error
		    ("cardman: failed to set card communication parameters");
		return n;
	}

	/* T=0 goes through send/receive functions, but
	 * T=1 needs special massaging */
	slot = &reader->slot[nslot];
	if (proto == IFD_PROTOCOL_T0) {
		slot->proto = ifd_protocol_new(proto, reader, slot->dad);
	} else {
		slot->proto = ifd_protocol_new(IFD_PROTOCOL_TRANSPARENT,
					       reader, slot->dad);
	}
	if (slot->proto == NULL) {
		ct_error("cardman: internal error");
		return -1;
	}

	priv = (cm_priv_t *) reader->driver_data;
	priv->icc_proto = proto;

	return 0;
}

/*
 * Send/receive using the underlying protocol.
 */
static int cm_transparent(ifd_reader_t * reader, int dad,
			  const void *sbuf, size_t slen, void *rbuf,
			  size_t rlen)
{
	cm_priv_t *priv = (cm_priv_t *) reader->driver_data;

	switch (priv->icc_proto) {
	case IFD_PROTOCOL_T0:
		return cm_transceive_t0(reader, sbuf, slen, rbuf, rlen);
	case IFD_PROTOCOL_T1:
		return IFD_ERROR_NOT_SUPPORTED;	/* not yet */
	}

	return IFD_ERROR_NOT_SUPPORTED;
}

static int cm_transceive_t0(ifd_reader_t * reader,
			    const void *sbuf, size_t slen, void *rbuf,
			    size_t rlen)
{
#if 0
	ifd_device_t *dev = reader->device;
	int rc;

	if (len > 5) {
		rc = ifd_usb_control(dev, 0x42, 2, 0, 0, rbuf, rlen);
	} else {
		unsigned char temp[5];

		if (len < 4)
			return IFD_ERROR_INVALID_ARG;
		temp[4] = 0;
		memcpy(temp, sbuf, slen);
		rc = ifd_usb_control(dev, 0x42, 3, 8, (temp[1] << 8) | temp[4],
				     temp, 5);
	}
#endif
	return IFD_ERROR_NOT_SUPPORTED;
}

/*
 * Send/receive routines
 */
static int cm_send_t0(ifd_reader_t * reader, unsigned int dad,
		      const unsigned char *sbuf, size_t slen)
{
	cm_priv_t *priv = (cm_priv_t *) reader->driver_data;
	ifd_device_t *dev = reader->device;
	int rc;

	/* XXX how can we know if this is a CASE 1 or CASE 2 APDU? */
	priv->head = priv->tail = 0;
	rc = cm_usb_int(dev, 0x42, 2, 0, 0, sbuf, slen,
			priv->rbuf, sizeof(priv->rbuf), cm_anyreply, -1);
	if (rc >= 0) {
		priv->tail = rc;
		rc = slen;
	}
	return rc;
}

static int cm_send(ifd_reader_t * reader, unsigned int dad,
		   const unsigned char *buffer, size_t len)
{
	cm_priv_t *priv = (cm_priv_t *) reader->driver_data;

	switch (priv->icc_proto) {
	case IFD_PROTOCOL_T0:
		return cm_send_t0(reader, dad, buffer, len);
	}

	return IFD_ERROR_NOT_SUPPORTED;
}

static int cm_recv(ifd_reader_t * reader, unsigned int dad,
		   unsigned char *buffer, size_t len, long timeout)
{
	cm_priv_t *priv = (cm_priv_t *) reader->driver_data;

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

/*
 * Set the card's baud rate etc
 */
static int cm_set_card_parameters(ifd_device_t * dev, unsigned int baudrate)
{
	return ifd_usb_control(dev, 0x42, 0x30, baudrate << 8, 2, NULL, 0, -1);
}

/*
 * Send USB control message, and receive data via
 * Interrupt URBs.
 */
static int cm_usb_int(ifd_device_t * dev, int requesttype, int request,
		      int value, int idx, const void *sbuf, size_t slen,
		      void *rbuf, size_t rlen, complete_fn_t complete,
		      long timeout)
{
	ifd_usb_capture_t *cap;
	struct timeval begin;
	unsigned int total = 0;
	int rc;

	if (timeout < 0)
		timeout = dev->timeout;

	rc = ifd_usb_begin_capture(dev,
				   IFD_USB_URB_TYPE_INTERRUPT, 0x81, 8, &cap);
	if (rc < 0)
		return rc;

	gettimeofday(&begin, NULL);
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

	if (rc >= 0) {
		ifd_debug(3, "received %u bytes:%s", total,
			  ct_hexdump(rbuf, total));
		rc = total;
	}

      out:
	ifd_usb_end_capture(dev, cap);
	return rc;
}

static int cm_anyreply(const void *ptr, size_t len)
{
	return 1;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops cardman_driver;

/*
 * Initialize this module
 */
void ifd_cardman_register(void)
{
	cardman_driver.open = cm_open;
	cardman_driver.activate = cm_activate;
	cardman_driver.deactivate = cm_deactivate;
	cardman_driver.card_status = cm_card_status;
	cardman_driver.card_reset = cm_card_reset;
	cardman_driver.send = cm_send;
	cardman_driver.recv = cm_recv;
	cardman_driver.set_protocol = cm_set_protocol;
	cardman_driver.transparent = cm_transparent;

	ifd_driver_register("cardman", &cardman_driver);
}
