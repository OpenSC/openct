/*
 * driver for Rainbow iKey 3000 devices
 *
 * Copyright (C) 2003, Andreas Jellinghaus <aj@dungeon.inka.de>
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <string.h>
#include "internal.h"

static int	ikey3k_control(ifd_device_t *dev, int requesttype, int request,
			       int value, int index,
			       void *buf, size_t len,
			       long timeout);

/*
 * Initialize the device
 */
static int
ikey3k_open(ifd_reader_t *reader, const char *device_name)
{
	ifd_device_t *dev;

	reader->name = "Rainbow iKey 3000";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("ikey3k: device %s is not a USB device",
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
ikey3k_activate(ifd_reader_t *reader)
{
	return 0;
}

static int
ikey3k_deactivate(ifd_reader_t *reader)
{
	return -1;
}

/*
 * Card status - always present
 */
static int
ikey3k_card_status(ifd_reader_t *reader, int slot, int *status)
{
	*status = IFD_CARD_PRESENT;
	return 0;
}

/*
 * Reset - nothing to be done?
 * We should do something to make it come back with all state zapped
 */
static int
ikey3k_card_reset(ifd_reader_t *reader, int slot, void *atr, size_t size)
{
	ifd_device_t *dev = reader->device;
	unsigned char	buffer[256];
	int		rc, n;

	unsigned char expect5[] =
	{ 0x0a, 0x61, 0x00, 0x07, 0x2d, 0x2d, 0xc0, 0x80, 0x80, 0x60 };
	unsigned char expect11[] = { 0xff, 0x11, 0x11, 0xff };

	if (ikey3k_control(dev, 0xc1, 0x00, 0, 0, buffer, 0x40, -1) != 10
	 || memcmp(buffer,expect5, sizeof(expect5)) != 0 
	 || ikey3k_control(dev, 0x41, 0x16, 0, 0, buffer, 00, -1) != 0
	 || ikey3k_control(dev, 0xc1, 0x01, 0, 0, buffer, 02, -1) != 1
	 || buffer[0] != 00)
		goto failed;

	rc = ikey3k_control(dev, 0x41, 0x16, 0x2005, 0, buffer, 0, 1000);
	if (rc < 0)
		goto failed;

	rc = ikey3k_control(dev, 0xc1, 0x01, 0, 0, buffer, 0x20, 1000);
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

	if (ikey3k_control(dev, 0x41, 0x16, 0x0002, 0, buffer, 0, -1) != 0
	 || ikey3k_control(dev, 0xc1, 0x01, 0, 0, buffer, 04, -1) != 4
	 || memcmp(buffer, expect11, sizeof(expect11)) != 0)
		goto failed;

	return n;

failed:	ct_error("ikey3k: failed to activate token");
	return -1;
}

/*
 * Send/receive routines
 */
static int
ikey3k_send(ifd_reader_t *reader, unsigned int dad, const unsigned char *buffer, size_t len)
{
	int value, index;
	value = buffer[1] << 8 | buffer[0];
	index = buffer[3] << 8 | buffer[2];

	return ikey3k_control(reader->device, 0x41, 0x17, value, index,
				(void *) &buffer[4], len-4, -1);
}

static int
ikey3k_recv(ifd_reader_t *reader, unsigned int dad, unsigned char *buffer, size_t len, long timeout)
{
	return ikey3k_control(reader->device, 0xc1, 0x01, 0, 0,
				buffer, 255, timeout);
}

/*
 * Send USB control message
 */
int
ikey3k_control(ifd_device_t *dev, int requesttype, int request,
	       int value, int index,
	       void *buf, size_t len,
	       long timeout)
{
	struct ifd_usb_cmsg cmsg;

	cmsg.guard = IFD_DEVICE_TYPE_USB;
	cmsg.requesttype = requesttype;
	cmsg.request = request;
	cmsg.value = value;
	cmsg.index = index;
	cmsg.data  = buf;
	cmsg.len = len;

	return ifd_device_control(dev, &cmsg, sizeof(cmsg));
}

/*
 * Driver operations
 */
static struct ifd_driver_ops	ikey3k_driver = {
	open:		ikey3k_open,
//	close:		ikey3k_close,
	activate:	ikey3k_activate,
	deactivate:	ikey3k_deactivate,
	card_status:	ikey3k_card_status,
	card_reset:	ikey3k_card_reset,
	send:		ikey3k_send,
	recv:		ikey3k_recv,
};
/*
 * Initialize this module
 */
void
ifd_ikey3k_register(void)
{
	ifd_driver_register("ikey3k", &ikey3k_driver);
}
