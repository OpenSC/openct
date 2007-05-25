/*
 * driver for Rainbow iKey 3000 devices
 *
 * Copyright (C) 2003, Andreas Jellinghaus <aj@dungeon.inka.de>
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>

/*
 * Initialize the device
 */
static int ikey3k_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	ifd_device_params_t params;

	reader->name = "Rainbow iKey 3000";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("ikey3k: device %s is not a USB device", device_name);
		ifd_device_close(dev);
		return -1;
	}

	params = dev->settings;
	params.usb.interface = 0;
	if (ifd_device_set_parameters(dev, &params) < 0) {
		ct_error("ikey3k: setting parameters failed", device_name);
		ifd_device_close(dev);
		return -1;
	}

	reader->device = dev;

	return 0;
}

/*
 * Power up the reader
 */
static int ikey3k_activate(ifd_reader_t * reader)
{
	return 0;
}

static int ikey3k_deactivate(ifd_reader_t * reader)
{
	return -1;
}

/*
 * Card status - always present
 */
static int ikey3k_card_status(ifd_reader_t * reader, int slot, int *status)
{
	*status = IFD_CARD_PRESENT;
	return 0;
}

/*
 * Reset - nothing to be done?
 * We should do something to make it come back with all state zapped
 */
static int ikey3k_card_reset(ifd_reader_t * reader, int slot, void *atr,
			     size_t size)
{
	ifd_device_t *dev = reader->device;
	unsigned char buffer[256];
	int rc, n, atrlen;

	unsigned char expect5[] =
	    { 0x0a, 0x61, 0x00, 0x07, 0x2d, 0x2d, 0xc0, 0x80, 0x80, 0x60 };
	unsigned char expect11[] = { 0xff, 0x11, 0x11, 0xff };

	if (ifd_usb_control(dev, 0xc1, 0x00, 0, 0, buffer, 0x40, -1) != 10
	    || memcmp(buffer, expect5, sizeof(expect5)) != 0
	    || ifd_usb_control(dev, 0x41, 0x16, 0, 0, buffer, 00, -1) != 0
	    || ifd_usb_control(dev, 0xc1, 0x01, 0, 0, buffer, 02, -1) != 1
	    || buffer[0] != 00)
		goto failed;

	rc = ifd_usb_control(dev, 0x41, 0x16, 0x2005, 0, buffer, 0, 1000);
	if (rc < 0)
		goto failed;

	rc = ifd_usb_control(dev, 0xc1, 0x01, 0, 0, buffer, 0x20, 1000);
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

	if (ifd_usb_control(dev, 0x41, 0x16, 0x0002, 0, buffer, 0, -1) != 0
	    || ifd_usb_control(dev, 0xc1, 0x01, 0, 0, buffer, 04, -1) != 4
	    || memcmp(buffer, expect11, sizeof(expect11)) != 0)
		goto failed;

	return atrlen;

      failed:
	ct_error("ikey3k: failed to activate token");
	return -1;
}

/*
 * Select a protocol. We override this function to be able to set the T=1 IFSC
 */
static int ikey3k_set_protocol(ifd_reader_t * reader, int nslot, int proto)
{
	ifd_slot_t *slot = &reader->slot[nslot];
	int r;

	if (!(slot->proto = ifd_protocol_new(proto, reader, slot->dad)))
		return -1;

	if (proto == IFD_PROTOCOL_T1) {
		r = ifd_protocol_set_parameter(slot->proto,
					       IFD_PROTOCOL_T1_IFSC, 256);
		if (r < 0)
			return r;
	}

	return 0;
}

/*
 * Send/receive routines
 */
static int ikey3k_send(ifd_reader_t * reader, unsigned int dad,
		       const unsigned char *buffer, size_t len)
{
	int value, idx;
	value = buffer[1] << 8 | buffer[0];
	idx = buffer[3] << 8 | buffer[2];

	return ifd_usb_control(reader->device, 0x41, 0x17, value, idx,
			       (void *)&buffer[4], len - 4, -1);
}

static int ikey3k_recv(ifd_reader_t * reader, unsigned int dad,
		       unsigned char *buffer, size_t len, long timeout)
{
	return ifd_usb_control(reader->device, 0xc1, 0x01, 0, 0,
			       buffer, 255, timeout);
}

/*
 * Driver operations
 */
static struct ifd_driver_ops ikey3k_driver;

/*
 * Initialize this module
 */
void ifd_ikey3k_register(void)
{
	ikey3k_driver.open = ikey3k_open;
	ikey3k_driver.activate = ikey3k_activate;
	ikey3k_driver.deactivate = ikey3k_deactivate;
	ikey3k_driver.card_status = ikey3k_card_status;
	ikey3k_driver.card_reset = ikey3k_card_reset;
	ikey3k_driver.set_protocol = ikey3k_set_protocol;
	ikey3k_driver.send = ikey3k_send;
	ikey3k_driver.recv = ikey3k_recv;

	ifd_driver_register("ikey3k", &ikey3k_driver);
}
