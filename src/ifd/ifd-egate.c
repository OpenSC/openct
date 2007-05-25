/*
 * e-gate driver
 *
 * Copyright (C) 2003, Chaskiel Grundman <cg2v@andrew.cmu.edu>
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EG_TIMEOUT	1000

#define EGATE_CMD_SEND_APDU	0x80
#define EGATE_CMD_READ		0x81
#define EGATE_CMD_WRITE		0x82
#define EGATE_CMD_READ_ATR	0x83
#define EGATE_CMD_RESET		0x90
#define EGATE_CMD_STATUS	0xA0

#define EGATE_STATUS_READY	0x00
#define EGATE_STATUS_DATA	0x10
#define EGATE_STATUS_SW		0x20
#define EGATE_STATUS_BUSY	0x40
#define EGATE_STATUS_MASK	0xF0

#define EGATE_ATR_MAXSIZE	0x23

#ifdef IFD_USB_ENDPOINT_IN
#define EGATE_DIR_OUT (IFD_USB_ENDPOINT_OUT | \
		       IFD_USB_TYPE_VENDOR | \
		       IFD_USB_RECIP_DEVICE)
#define EGATE_DIR_IN  (IFD_USB_ENDPOINT_IN | \
		       IFD_USB_TYPE_VENDOR | \
		       IFD_USB_RECIP_DEVICE)
#else
#define EGATE_DIR_OUT 0x40
#define EGATE_DIR_IN  0xc0
#endif
/*
 * Initialize the device
 */
static int eg_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	ifd_device_params_t params;

	ifd_debug(1, "device=%s", device_name);
	reader->name = "Schlumberger E-Gate";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("egate: device %s is not a USB device", device_name);
		ifd_device_close(dev);
		return -1;
	}

	params = dev->settings;
	params.usb.interface = 0;
	if (ifd_device_set_parameters(dev, &params) < 0) {
		ct_error("egate: setting parameters failed", device_name);
		ifd_device_close(dev);
		return -1;
	}

	reader->device = dev;

	return 0;
}

/*
 * Power up the reader
 */
static int eg_activate(ifd_reader_t * reader)
{
	ifd_debug(1, "called.");
	return 0;
}

static int eg_deactivate(ifd_reader_t * reader)
{
	ifd_debug(1, "called.");
	return 0;
}

/*
 * Card status - always present
 */
static int eg_card_status(ifd_reader_t * reader, int slot, int *status)
{
	ifd_debug(3, "slot=%d", slot);
	*status = IFD_CARD_PRESENT;
	return 0;
}

static int eg_card_reset(ifd_reader_t * reader, int slot, void *atr,
			 size_t size)
{
	ifd_device_t *dev = reader->device;
	unsigned char buffer[EGATE_ATR_MAXSIZE];
	int rc, atrlen, stat;

	ifd_debug(1, "called.");
	usleep(100000);
	/* Reset the device */
	rc = ifd_usb_control(dev, EGATE_DIR_OUT, EGATE_CMD_RESET,
			     0, 0, NULL, 0, EG_TIMEOUT * 2);
	if (rc < 0) {
	      failed:
		ct_error("egate: failed to activate token");
		return IFD_ERROR_COMM_ERROR;
	}

	usleep(100000);
	rc = ifd_usb_control(reader->device, EGATE_DIR_IN, EGATE_CMD_STATUS,
			     0, 0, &stat, 1, EG_TIMEOUT);
	if (rc != 1)
		return IFD_ERROR_COMM_ERROR;

	/* Fetch the ATR */
	usleep(100000);
	rc = ifd_usb_control(dev, EGATE_DIR_IN, EGATE_CMD_READ_ATR,
			     0, 0, buffer, EGATE_ATR_MAXSIZE, EG_TIMEOUT);
	if (rc <= 0)
		goto failed;

	if (rc > IFD_MAX_ATR_LEN)
		goto failed;

	if (rc > size)
		rc = size;
	atrlen = rc;
	memcpy(atr, buffer, atrlen);

	return atrlen;

}

static int eg_set_protocol(ifd_reader_t * reader, int s, int proto)
{
	ifd_slot_t *slot;
	ifd_protocol_t *p;

	ifd_debug(1, "proto=%d", proto);
	if (proto != IFD_PROTOCOL_T0 && proto != IFD_PROTOCOL_TRANSPARENT) {
		ct_error("%s: protocol %d not supported", reader->name, proto);
		return IFD_ERROR_NOT_SUPPORTED;
	}
	slot = &reader->slot[s];
	p = ifd_protocol_new(IFD_PROTOCOL_TRANSPARENT, reader, slot->dad);
	if (p == NULL) {
		ct_error("%s: internal error", reader->name);
		return IFD_ERROR_GENERIC;
	}
	if (slot->proto) {
		ifd_protocol_free(slot->proto);
		slot->proto = NULL;
	}
	slot->proto = p;
	return 0;
}

static unsigned char eg_status(ifd_reader_t * reader)
{
	int rc;
	unsigned char stat;

	/* Shouldn't there be a retry counter that prevents the command
	 * from hanging indefinitely? Are there scenarios where the
	 * egate would be busy for more than say 180 secs?    --okir
	 */
	while (1) {
		rc = ifd_usb_control(reader->device, EGATE_DIR_IN,
				     EGATE_CMD_STATUS, 0, 0, &stat, 1,
				     EG_TIMEOUT);
		if (rc != 1)
			return -1;
		stat &= EGATE_STATUS_MASK;
		if (stat != EGATE_STATUS_BUSY) {
			return stat;
		}
		usleep(100);
	}

}

/*
 * Send/receive routines
 */
static int eg_transparent(ifd_reader_t * reader, int dad, const void *inbuffer,
			  size_t inlen, void *outbuffer, size_t outlen)
{
	int rc, bytesread;
	unsigned char stat;
	ifd_iso_apdu_t iso;
	unsigned char cmdbuf[5];
	int i;

	stat = eg_status(reader);
	if (stat != EGATE_STATUS_READY) {
		for (i = 0; i < 4; i++) {
			ifd_debug(2, "device not ready, attempting reset");
			rc = ifd_usb_control(reader->device, EGATE_DIR_OUT,
					     EGATE_CMD_RESET, 0, 0, NULL, 0,
					     EG_TIMEOUT);
			if (rc < 0)
				return IFD_ERROR_COMM_ERROR;
			usleep(100);
			stat = eg_status(reader);
			if (stat == EGATE_STATUS_READY) {
				ifd_debug(2, "reset succeeded");
				/* FIXME: we need a better error code */
				return IFD_ERROR_COMM_ERROR;
			}
			ifd_debug(2, "reset failed");
		}
		ifd_debug(2, "giving up on reset");
		return IFD_ERROR_COMM_ERROR;
	}

	if (ifd_iso_apdu_parse(inbuffer, inlen, &iso) < 0)
		return IFD_ERROR_INVALID_ARG;
	if (inlen >= 5 && inlen < 5 + iso.lc)
		return IFD_ERROR_BUFFER_TOO_SMALL;
	if (outlen < 2 + iso.le)
		return IFD_ERROR_BUFFER_TOO_SMALL;
	memset(cmdbuf, 0, 5);
	memmove(cmdbuf, inbuffer, inlen < 5 ? inlen : 5);
	rc = ifd_usb_control(reader->device, EGATE_DIR_OUT, EGATE_CMD_SEND_APDU,
			     0, 0, (void *)cmdbuf, 5, -1);
	if (rc != 5)
		return IFD_ERROR_COMM_ERROR;
	stat = eg_status(reader);
	if (inlen > 5 && stat == EGATE_STATUS_DATA) {
		rc = ifd_usb_control(reader->device, EGATE_DIR_OUT,
				     EGATE_CMD_WRITE, 0, 0,
				     (void *)(((unsigned char *)inbuffer) + 5),
				     iso.lc, -1);
		if (rc < 0)
			return IFD_ERROR_COMM_ERROR;
		if (rc != iso.lc) {
			ifd_debug(1, "short USB write (%u of %u bytes)", rc,
				  iso.lc);
			return IFD_ERROR_COMM_ERROR;
		}
		ifd_debug(3, "sent %d bytes of data", iso.lc);
		stat = eg_status(reader);
	}
	bytesread = 0;

	while (stat == EGATE_STATUS_DATA && bytesread < iso.le) {
		rc = ifd_usb_control(reader->device, EGATE_DIR_IN,
				     EGATE_CMD_READ, 0, 0,
				     (void *)(((unsigned char *)outbuffer) +
					      bytesread),
				     iso.le - bytesread, EG_TIMEOUT);
		if (rc < 0)
			return IFD_ERROR_COMM_ERROR;
		bytesread += rc;
		ifd_debug(3, "received %d bytes of data", rc);
		stat = eg_status(reader);
	}
	iso.le = bytesread;
	if (stat != EGATE_STATUS_SW)
		return IFD_ERROR_DEVICE_DISCONNECTED;
	rc = ifd_usb_control(reader->device, EGATE_DIR_IN, EGATE_CMD_READ, 0, 0,
			     (void *)(((unsigned char *)outbuffer) + iso.le), 2,
			     EG_TIMEOUT);
	if (rc != 2)
		return IFD_ERROR_COMM_ERROR;
	ifd_debug(2, "returning a %d byte response", iso.le + 2);
	return iso.le + 2;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops egate_driver;

/*
 * Initialize this module
 */
void ifd_egate_register(void)
{
	egate_driver.open = eg_open;
	egate_driver.activate = eg_activate;
	egate_driver.deactivate = eg_deactivate;
	egate_driver.card_status = eg_card_status;
	egate_driver.card_reset = eg_card_reset;
	egate_driver.set_protocol = eg_set_protocol;
	egate_driver.transparent = eg_transparent;

	ifd_driver_register("egate", &egate_driver);
}
