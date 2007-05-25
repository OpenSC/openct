/*
 * starkey driver
 *
 * Copyright (C) 2005, Andreas Jellinghaus <aj@dungeon.inka.de>
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>

#define STARKEY_TIMEOUT 100000

/*
 * Initialize the device
 */
static int starkey_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	ifd_device_params_t params;

	reader->name = "G&D Starkey 100";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("starkey: device %s is not a USB device", device_name);
		ifd_device_close(dev);
		return -1;
	}

	params = dev->settings;
	params.usb.interface = 0;
	if (ifd_device_set_parameters(dev, &params) < 0) {
		ct_error("starkey: setting parameters failed", device_name);
		ifd_device_close(dev);
		return -1;
	}

	reader->device = dev;

	return 0;
}

/*
 * Power up the reader
 */
static int starkey_activate(ifd_reader_t * reader)
{
	return 0;
}

static int starkey_deactivate(ifd_reader_t * reader)
{
	return -1;
}

/*
 * Card status - always present
 */
static int starkey_card_status(ifd_reader_t * reader, int slot, int *status)
{
	*status = IFD_CARD_PRESENT;
	return 0;
}

/*
 * Reset - nothing to be done?
 * We should do something to make it come back with all state zapped
 */
static int starkey_card_reset(ifd_reader_t * reader, int slot, void *atr,
			      size_t size)
{
	ifd_device_t *dev = reader->device;
	unsigned char buffer[32];
	int rc, atrlen;
	ifd_usb_capture_t *cap;

	rc = ifd_usb_begin_capture(dev,
				   IFD_USB_URB_TYPE_INTERRUPT, 0x81,
				   sizeof(buffer), &cap);
	if (rc < 0)
		return rc;

	rc = ifd_usb_capture(dev, cap, buffer, sizeof(buffer), STARKEY_TIMEOUT);

	if (rc <= 0) {
		ct_error("starkey: failed to activate token");
		return -1;
	}

	memcpy(atr, buffer, rc);
	atrlen = rc;
	return atrlen;
}

/*
 * Send/receive routines
 */
static int starkey_send(ifd_reader_t * reader, unsigned int dad,
			const unsigned char *buffer, size_t len)
{
	return ifd_usb_control(reader->device, 0x40, 0x06, 0, 0,
			       (void *)buffer, len, -1);
}

static int starkey_recv(ifd_reader_t * reader, unsigned int dad,
			unsigned char *buffer, size_t len, long timeout)
{
	return ifd_usb_control(reader->device, 0xc0, 0x86, 0, 0,
			       buffer, len, timeout);
}

/*
 * Driver operations
 */
static struct ifd_driver_ops starkey_driver;

/*
 * Initialize this module
 */
void ifd_starkey_register(void)
{
	starkey_driver.open = starkey_open;
	starkey_driver.activate = starkey_activate;
	starkey_driver.deactivate = starkey_deactivate;
	starkey_driver.card_status = starkey_card_status;
	starkey_driver.card_reset = starkey_card_reset;
	starkey_driver.send = starkey_send;
	starkey_driver.recv = starkey_recv;

	ifd_driver_register("starkey", &starkey_driver);
}
