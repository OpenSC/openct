/*
 * eToken driver
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 *
 * Based on information from the etoken driver by
 * Andreas Jellinghaus <aj@dungeon.inka.de>.
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>

#define ET_TIMEOUT	1000

static int et_magic(ifd_device_t *);

/*
 * Initialize the device
 */
static int et_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	ifd_device_params_t params;

	reader->name = "Aladdin eToken PRO";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("etoken: device %s is not a USB device", device_name);
		ifd_device_close(dev);
		return -1;
	}

	params = dev->settings;
	params.usb.interface = 0;
	if (ifd_device_set_parameters(dev, &params) < 0) {
		ct_error("etoken: setting parameters failed", device_name);
		ifd_device_close(dev);
		return -1;
	}

	reader->device = dev;

	return 0;
}

/* Some magic incantations copied from Andreas
 * Jellinghaus' eToken driver 
 * */
static int et_magic(ifd_device_t * dev)
{
	unsigned char cookie[] = { 0x00, 0x00, 0x01, 0x00, 0x88, 0x13 };
	unsigned char buffer[256];

	if (ifd_usb_control(dev, 0x40, 0x03, 0, 0, NULL, 0, -1) < 0
	    || ifd_usb_control(dev, 0xc0, 0x83, 0, 0, buffer, 13, -1) != 13
	    || ifd_usb_control(dev, 0x40, 0x02, 0, 0, cookie, sizeof(cookie),
			       -1) < 0
	    || ifd_usb_control(dev, 0xc0, 0x82, 0, 0, buffer, 1, -1) != 1
	    || buffer[0] != 0)
		return -1;

	return 0;
}

/*
 * Power up the reader
 */
static int et_activate(ifd_reader_t * reader)
{
	return 0;
}

static int et_deactivate(ifd_reader_t * reader)
{
	return -1;
}

/*
 * Card status - always present
 */
static int et_card_status(ifd_reader_t * reader, int slot, int *status)
{
	*status = IFD_CARD_PRESENT;
	return 0;
}

/*
 * Reset - nothing to be done?
 * We should do something to make it come back with all state zapped
 */
static int et_card_reset(ifd_reader_t * reader, int slot, void *atr,
			 size_t size)
{
	ifd_device_t *dev = reader->device;
	unsigned char buffer[256];
	int rc, n, atrlen;

	/* Request the ATR */
	rc = ifd_usb_control(dev, 0x40, 0x01, 0, 0, NULL, 0, ET_TIMEOUT);
	if (rc < 0)
		goto failed;

	/* Receive the ATR */
	rc = ifd_usb_control(dev, 0xc0, 0x81, 0, 0, buffer, 0x23, ET_TIMEOUT);
	if (rc <= 0)
		goto failed;

	n = buffer[0];
	if (n + 1 > rc)
		goto failed;
	if (n > IFD_MAX_ATR_LEN)
		goto failed;

	if (n > size)
		n = size;
	atrlen = n;
	memcpy(atr, buffer + 1, atrlen);

	if (et_magic(dev) < 0)
		goto failed;
	return atrlen;

      failed:
	ct_error("etoken: failed to activate token");
	return -1;
}

/*
 * Send/receive routines
 */
static int et_send(ifd_reader_t * reader, unsigned int dad,
		   const unsigned char *buffer, size_t len)
{
	return ifd_usb_control(reader->device, 0x40, 0x06, 0, 0,
			       (void *)buffer, len, -1);
}

static int et_recv(ifd_reader_t * reader, unsigned int dad,
		   unsigned char *buffer, size_t len, long timeout)
{
	return ifd_usb_control(reader->device, 0xc0, 0x86, 0, 0,
			       buffer, len, timeout);
}

static int et_get_eventfd(ifd_reader_t * reader, short *events)
{
	ifd_debug(1, "called.");

	return ifd_device_get_eventfd(reader->device, events);
}

static int et_event(ifd_reader_t * reader, int *status, size_t status_size)
{
	(void)reader;
	(void)status;
	(void)status_size;

	ifd_debug(1, "called.");

	return 0;
}

static int et_error(ifd_reader_t * reader)
{
	(void)reader;

	ifd_debug(1, "called.");

	return IFD_ERROR_DEVICE_DISCONNECTED;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops etoken_driver;

/*
 * Initialize this module
 */
void ifd_etoken_register(void)
{
	etoken_driver.open = et_open;
	etoken_driver.activate = et_activate;
	etoken_driver.deactivate = et_deactivate;
	etoken_driver.card_status = et_card_status;
	etoken_driver.card_reset = et_card_reset;
	etoken_driver.send = et_send;
	etoken_driver.recv = et_recv;
	etoken_driver.get_eventfd = et_get_eventfd;
	etoken_driver.event = et_event;
	etoken_driver.error = et_error;

	ifd_driver_register("etoken", &etoken_driver);
}
