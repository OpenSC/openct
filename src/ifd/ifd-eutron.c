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
	unsigned char	buffer[IFD_MAX_ATR_LEN+100];
	unsigned char	cookie[] = { 0xff, 0x11, 0x98, 0x76 };
	int		rc, lr, c, atrlen;

	if (ifd_usb_control(dev, 0x41, 0xa3, 0, 0, NULL, 0, -1) != 0
	 || ifd_usb_control(dev, 0x41, 0xa1, 0, 0, NULL, 0, -1) != 0
	 || ifd_usb_control(dev, 0x41, 0xa2, 0, 0, NULL, 0, -1) != 0
	 || ifd_usb_control(dev, 0x41, 0xa0, 0, 0, NULL, 0, -1) != 0
	 || ifd_usb_control(dev, 0x41, 0x09, 0, 0, NULL, 0, -1) != 0)
		goto failed;

	for (lr=0,c=0;c < 20;c++) {
		rc = ifd_usb_control(dev, 0xc1, 0x02, 0, 0,
				&buffer[lr], 100, 1000);

		if (rc < 0)
			goto failed;
		lr+=rc;

		rc = ifd_atr_complete(buffer,lr);

		if (rc) break;

		if (lr > IFD_MAX_ATR_LEN) 
			goto failed;
	}
	if (c >= 20)
		goto failed;

	atrlen = lr;
	memcpy(atr, buffer, atrlen);

	if (ifd_usb_control(dev, 0x41, 0x01, 0, 0, 
		cookie, sizeof(cookie), 1000) != sizeof(cookie))
		goto failed;

	for (lr=0,c=0;c < 20;c++) {
		rc = ifd_usb_control(dev, 0xc1, 0x02, 0, 0,
				&buffer[lr], 100, 1000);

		if (rc < 0)
			goto failed;
		lr+=rc;
		if (lr >= 4)
			break;

		if (lr > IFD_MAX_ATR_LEN) 
			goto failed;
	}
	if (c >= 20)
		goto failed;

	if (ifd_usb_control(dev, 0x41, 0x65, 0x98, 0, NULL, 0, -1) != 0
		|| ifd_usb_control(dev, 0x41, 0xa0, 0, 0, NULL, 0, -1) != 0)
		goto failed;

	return atrlen;

failed:	ct_error("eutron: failed to activate token");
	return -1;
}

/*
 * Send/receive routines
 */
static int
eutron_send(ifd_reader_t *reader, unsigned int dad, const unsigned char *buffer, size_t len)
{
	return ifd_usb_control(reader->device, 0x42, 0x01, 0, 0,
				buffer, len, 1000);
}

static int
eutron_recv(ifd_reader_t *reader, unsigned int dad, unsigned char *buffer, size_t len, long timeout)
{
	int rc,lr,c,rbs;

	for (lr=0,c=0;c < 200;c++) {
		rbs = len - lr;
		if (rbs > 100) rbs = 100;
		if (rbs == 0)
			goto failed;

		rc = ifd_usb_control(reader->device, 0xc1, 0x02, 0, 0,
				&buffer[lr], rbs, timeout);

		if (rc < 0)
			goto failed;
		lr+=rc;

		if (lr >= 4 && lr>=buffer[2]+4)
			break;
	}
	if (c >= 200)
		goto failed;

	return lr;
failed:	ct_error("eutron: failed to receive t=1 frame");
	return -1;
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
