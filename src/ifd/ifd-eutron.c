/*
 * Eutron Crypto Idendity IT-Sec driver
 *
 * Copyright (C) 2003, Andreas Jellinghaus <aj@dungeon.inka.de>
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <string.h>
#include "internal.h"

/*
 * Initialize the device
 */
static int
eutron_open(ifd_reader_t *reader, const char *device_name)
{
	ifd_device_t *dev;

	reader->name = "Eutron CryptoIdendity IT-SEC";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("eutron: device %s is not a USB device",
				device_name);
		ifd_device_close(dev);
		return -1;
	}

	reader->device = dev;

	return 0;
}

/*
 * Power up the reader
 */
static int
eutron_activate(ifd_reader_t *reader)
{
	return 0;
}

static int
eutron_deactivate(ifd_reader_t *reader)
{
	return -1;
}

/*
 * Card status - always present
 */
static int
eutron_card_status(ifd_reader_t *reader, int slot, int *status)
{
	*status = IFD_CARD_PRESENT;
	return 0;
}

/*
 * Reset - nothing to be done?
 * We should do something to make it come back with all state zapped
 */
static int
eutron_card_reset(ifd_reader_t *reader, int slot, void *atr, size_t size)
{
	ifd_device_t *dev = reader->device;
	unsigned char	buffer[256];
	int		rc, n;
	unsigned char cookie[] = { 0x00, 0x00, 0x01, 0x00, 0x88, 0x13 };

	if (ifd_usb_control(dev, 0x41, 0xa3, 0, 0, NULL, 0, -1) != 0
	 || ifd_usb_control(dev, 0x41, 0xa1, 0, 0, NULL, 0, -1) != 0
	 || ifd_usb_control(dev, 0x41, 0xa2, 0, 0, NULL, 0, -1) != 0
	 || ifd_usb_control(dev, 0x41, 0xa0, 0, 0, NULL, 0, -1) != 0
	 || ifd_usb_control(dev, 0x41, 0x09, 0, 0, NULL, 0, -1) != 0)
		goto failed;

	/* Request the ATR */
	rc = ifd_usb_control(dev, 0x40, 0x01, 0, 0, NULL, 0, 1000);
	if (rc < 0)
		goto failed;

	/* Receive the ATR */
	rc = ifd_usb_control(dev, 0xc0, 0x81, 0, 0, buffer, 0x23, 1000);
	if (rc <= 0)
		goto failed;

	n = buffer[0];
	if (n + 1 > rc)
		goto failed;
	if (n > IFD_MAX_ATR_LEN)
		goto failed;

	if (n > size)
		n = size;
	memcpy(atr, buffer + 1, n);

	return n;

failed:	ct_error("eutron: failed to activate token");
	return -1;
}

/*
 * Send/receive routines
 */
static int
eutron_send(ifd_reader_t *reader, unsigned int dad, const unsigned char *buffer, size_t len)
{
	return ifd_usb_control(reader->device, 0x40, 0x06, 0, 0,
				(void *) buffer, len, -1);
}

static int
eutron_recv(ifd_reader_t *reader, unsigned int dad, unsigned char *buffer, size_t len, long timeout)
{
	return ifd_usb_control(reader->device, 0xc0, 0x86, 0, 0,
				buffer, len, timeout);
}

/*
 * Driver operations
 */
static struct ifd_driver_ops	eutron_driver = {
	open:		eutron_open,
//	close:		eutron_close,
	activate:	eutron_activate,
	deactivate:	eutron_deactivate,
	card_status:	eutron_card_status,
	card_reset:	eutron_card_reset,
	send:		eutron_send,
	recv:		eutron_recv,
};
/*
 * Initialize this module
 */
void
ifd_eutron_register(void)
{
	ifd_driver_register("eutron", &eutron_driver);
}
