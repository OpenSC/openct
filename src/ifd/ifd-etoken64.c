/*
 * eToken 64 driver
 *
 * Copyright (C) 2005, Olaf Kirch <okir@suse.de>
 * Copyright (C) 2005, Andreas Jellinghaus <aj@dungeon.inka.de>.
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>

#define ET64_TIMEOUT	1000

/*
 * Initialize the device
 */
static int et64_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	ifd_device_params_t params;

	reader->name = "Aladdin eToken PRO 64k";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("etoken64: device %s is not a USB device",
			 device_name);
		ifd_device_close(dev);
		return -1;
	}

	params = dev->settings;
	params.usb.interface = 0;
	if (ifd_device_set_parameters(dev, &params) < 0) {
		ct_error("etoken64: setting parameters failed", device_name);
		ifd_device_close(dev);
		return -1;
	}

	reader->device = dev;

	return 0;
}

/*
 * Power up the reader
 */
static int et64_activate(ifd_reader_t * reader)
{
	return 0;
}

static int et64_deactivate(ifd_reader_t * reader)
{
	return -1;
}

/*
 * Card status - always present
 */
static int et64_card_status(ifd_reader_t * reader, int slot, int *status)
{
	*status = IFD_CARD_PRESENT;
	return 0;
}

/*
 * Reset - nothing to be done?
 * We should do something to make it come back with all state zapped
 */
static int et64_card_reset(ifd_reader_t * reader, int slot, void *atr,
			   size_t size)
{
	ifd_device_t *dev = reader->device;
	unsigned char buffer[256];
	int rc, n, atrlen;

	/* Request the ATR */
	rc = ifd_usb_control(dev, 0x40, 0x01, 0, 0, NULL, 0, ET64_TIMEOUT);
	if (rc < 0)
		goto failed;

	/* Receive the ATR */
	rc = ifd_usb_control(dev, 0xc0, 0x81, 0, 0, buffer, 0x23, ET64_TIMEOUT);
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

	if (ifd_usb_control(dev, 0x40, 0x08, 0, 0, NULL, 0, -1) < 0
	    || ifd_usb_control(dev, 0xc0, 0x88, 0, 0, buffer, 02, -1) != 02
	    || ifd_usb_control(dev, 0x40, 0x03, 0, 0, NULL, 0, -1) < 0
	    || ifd_usb_control(dev, 0xc0, 0x83, 0, 0, buffer, 1, -1) != 1
	    || buffer[0] != 0)
		goto failed;

	return atrlen;

      failed:
	ct_error("etoken64: failed to activate token");
	return -1;
}

/*
 * Send/receive routines
 */
static int et64_send(ifd_reader_t * reader, unsigned int dad,
		     const unsigned char *buffer, size_t len)
{
	return ifd_usb_control(reader->device, 0x40, 0x06, 0, 0,
			       (void *)buffer, len, -1);
}

static int et64_recv(ifd_reader_t * reader, unsigned int dad,
		     unsigned char *buffer, size_t len, long timeout)
{
	return ifd_usb_control(reader->device, 0xc0, 0x86, 0, 0,
			       buffer, len, timeout);
}

static int et64_get_eventfd(ifd_reader_t * reader, short *events)
{
	ifd_debug(1, "called.");

	return ifd_device_get_eventfd(reader->device, events);
}

static int et64_event(ifd_reader_t * reader, int *status, size_t status_size)
{
	(void)reader;
	(void)status;
	(void)status_size;

	ifd_debug(1, "called.");

	return 0;
}

static int et64_error(ifd_reader_t * reader)
{
	(void)reader;

	ifd_debug(1, "called.");

	return IFD_ERROR_DEVICE_DISCONNECTED;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops etoken64_driver;

/*
 * Initialize this module
 */
void ifd_etoken64_register(void)
{
	etoken64_driver.open = et64_open;
	etoken64_driver.activate = et64_activate;
	etoken64_driver.deactivate = et64_deactivate;
	etoken64_driver.card_status = et64_card_status;
	etoken64_driver.card_reset = et64_card_reset;
	etoken64_driver.send = et64_send;
	etoken64_driver.recv = et64_recv;
	etoken64_driver.get_eventfd = et64_get_eventfd;
	etoken64_driver.event = et64_event;
	etoken64_driver.error = et64_error;

	ifd_driver_register("etoken64", &etoken64_driver);
}
