/*
 * Omnikey Cardman driver
 * This driver is not yet complete, but at least it
 * spits out the ATR already.
 * Needs a recentish Linux Kernel (2.4.5 does NOT work)
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 *
 * Based on information from the cm2020 driver by
 * Omnikey AG.
 */

#include <stdlib.h>
#include <string.h>
/* XXX Linux specific */
#include <linux/usbdevice_fs.h>
#include "internal.h"

#define ET_TIMEOUT	1000

typedef int	complete_fn_t(const void *, size_t);
static int	cm_magic(ifd_device_t *);
static int	cm_usb_int(ifd_device_t *dev, int requesttype, int request,
			       int value, int index,
			       void *buf, size_t len,
			       complete_fn_t check,
			       long timeout);

/*
 * Initialize the device
 */
static int
cm_open(ifd_reader_t *reader, const char *device_name)
{
	ifd_device_t *dev;

	reader->name = "Omnikey Cardman";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("cardman: device %s is not a USB device",
				device_name);
		ifd_device_close(dev);
		return -1;
	}

	reader->device = dev;
	dev->timeout = 2000;

	return 0;
}

/*
 * Power up the reader
 */
static int
cm_activate(ifd_reader_t *reader)
{
	ifd_device_t *dev = reader->device;
	int	rc;

	ifd_debug(1, "called.");
	/* Set async card @9600 bps, 2 stop bits, even parity */
	if ((rc = ifd_usb_control(dev, 0x42, 0x30, 0x100, 2, NULL, 0, -1)) < 0) {
		ct_error("cardman: failed to set card parameters 9600/8E2");
		return rc;
	}
	return 0;
}

static int
cm_deactivate(ifd_reader_t *reader)
{
	ifd_device_t *dev = reader->device;
	int	rc;

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
static int
cm_card_status(ifd_reader_t *reader, int slot, int *status)
{
	ifd_device_t	*dev = reader->device;
	unsigned char	cm_status = 0;
	int		rc;

	*status = 0;

	if ((rc = cm_usb_int(dev, 0x42, 0x20, 0, 0, &cm_status, 1, NULL, -1)) < 0) {
		ct_error("cardman: failed to get card status");
		return -1;
	}
	if (rc == 1 && (cm_status & 0x42))
		*status = IFD_CARD_PRESENT;
	ifd_debug(1, "card %spresent", *status? "" : "not ");
	return 0;
}

/*
 * Reset
 */
static int
cm_card_reset(ifd_reader_t *reader, int slot, void *atr, size_t size)
{
	ifd_device_t	*dev = reader->device;
	unsigned char	buffer[IFD_MAX_ATR_LEN];
	int		n;

	/* Request the ATR */
	if ((n = cm_usb_int(dev, 0x42, 0x10, 1, 0,
					buffer, sizeof(buffer),
					(complete_fn_t *) ifd_atr_complete,
					-1)) < 0) {
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
 * Send/receive routines
 */
static int
cm_send(ifd_reader_t *reader, unsigned int dad, const unsigned char *buffer, size_t len)
{
	return IFD_ERROR_NOT_SUPPORTED; /* not yet */
}

static int
cm_recv(ifd_reader_t *reader, unsigned int dad, unsigned char *buffer, size_t len, long timeout)
{
	return IFD_ERROR_NOT_SUPPORTED; /* not yet */
}

/*
 * Send USB control message, and receive data via
 * Interrupt URBs.
 */
int
cm_usb_int(ifd_device_t *dev, int requesttype, int request,
	       int value, int index,
	       void *buf, size_t len,
	       complete_fn_t complete,
	       long timeout)
{
	ifd_usb_capture_t	*cap;
	struct timeval		begin;
	unsigned int		total = 0;
	int			rc;

	if (timeout < 0)
		timeout = dev->timeout;

	rc = ifd_usb_begin_capture(dev,
		       	IFD_USB_URB_TYPE_INTERRUPT,
			0x81, 8, &cap);
	if (rc < 0)
		return rc;

	gettimeofday(&begin, NULL);
	rc = ifd_usb_control(dev, requesttype, request,
			value, index, NULL, 0, timeout);
	if (rc < 0)
		goto out;

	/* Capture URBs until we have a complete answer */
	while (rc >= 0 && total < len) {
		unsigned char	temp[8];
		long		wait;

		wait = timeout - ifd_time_elapsed(&begin);
		if (wait <= 0)
			return IFD_ERROR_TIMEOUT;
		rc = ifd_usb_capture(dev, cap, temp, sizeof(temp), wait);
		if (rc > 0) {
			if (rc > (int) (len - total))
				rc = len - total;
			memcpy((caddr_t) buf + total, temp, rc);
			total += rc;

			if (complete && complete(buf, total))
				break;
		}
	}

	if (rc >= 0) {
		ifd_debug(3, "received %u bytes:%s", total, ct_hexdump(buf, total));
		rc = total;
	}

out:
	ifd_usb_end_capture(dev, cap);
	return rc;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops	cardman_driver = {
	open:		cm_open,
//	close:		cm_close,
	activate:	cm_activate,
	deactivate:	cm_deactivate,
	card_status:	cm_card_status,
	card_reset:	cm_card_reset,
	send:		cm_send,
	recv:		cm_recv,
};

/*
 * Initialize this module
 */
void
ifd_cardman_register(void)
{
	ifd_driver_register("cardman", &cardman_driver);
}
