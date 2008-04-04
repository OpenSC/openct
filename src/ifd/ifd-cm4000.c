/*
 * OMNIKEY CardMan Mobile PCMCIA 4000 Driver
 *
 * This driver is not yet complete, but at least it
 * spits out the ATR already.
 *
 * Copyright (C) 2005, Harald Welte <laforge@gnumonks.org>
 *
 * Based on information from the cm4000 driver by Omnikey AG.
 */

/* only available on linux */
#ifdef linux

#include "internal.h"
#include "cardman.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

/*
 * Initialize the device
 */
static int cm_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	ifd_device_params_t params;

	reader->name = "OMNIKEY CardMan 4000";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_PCMCIA) {
		ct_error("cm4000: device %s is not a PCMCIA device",
			 device_name);
		ifd_device_close(dev);
		return -1;
	}

	reader->driver_data = NULL;
	reader->device = dev;
	dev->timeout = 2000;

	params = dev->settings;
	params.usb.interface = 0;
	if (ifd_device_set_parameters(dev, &params) < 0) {
		ct_error("cm4000: setting parameters failed", device_name);
		ifd_device_close(dev);
		return -1;
	}

	return 0;
}

/*
 * Power up the card slot
 */
static int cm_activate(ifd_reader_t * reader)
{
	ifd_debug(1, "called.");
	return 0;
}

static int cm_deactivate(ifd_reader_t * reader)
{
	ifd_debug(1, "called.");
	return 0;
}

/*
 * Card status 
 */
static int cm_card_status(ifd_reader_t * reader, int slot, int *status)
{
	ifd_device_t *dev = reader->device;
	unsigned int cm_status = 0;
	int rc;

	*status = 0;

	ifd_debug(1, "called.");
	rc = ioctl(dev->fd, CM_IOCGSTATUS, &cm_status);
	if (rc != 0) {
		ifd_debug(1, "error during ioctl(CM_IOCGSTATUS): %d=%s",
			  rc, strerror(errno));
		return -1;
	}

	if (cm_status & CM_ATR_PRESENT)
		*status = IFD_CARD_PRESENT;

	/* Hardware doesn't tell us about status change */

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
	struct atreq cmatr;
	int len;

	ioctl(dev->fd, 0x6304, 1);
	/* propriatary driver doesn't check return value here, too */

	/* CM_IOCGATR */
	if (ioctl(dev->fd, CM_IOCGATR, &cmatr) != 0) {
		ifd_debug(1, "error during ioctl(CM_IOCGATR)\n");
		return -1;
	}

	if (cmatr.atr_len == -1) {
		ifd_debug(1, "atr_len == -1\n");
		return -1;
	}

	len = cmatr.atr_len;
	if ((size_t) len > size)
		len = size;

	memcpy(atr, &cmatr.atr, len);

	return len;
}

static int cm_send(ifd_reader_t * reader, unsigned int dad,
		   const unsigned char *buffer, size_t len)
{
	ifd_device_t *dev = reader->device;

	return write(dev->fd, buffer, len);
}

static int cm_recv(ifd_reader_t * reader, unsigned int dad,
		   unsigned char *buffer, size_t len, long timeout)
{
	ifd_device_t *dev = reader->device;

	return read(dev->fd, buffer, len);
}

/*
 * Driver operations
 */
static struct ifd_driver_ops cm4000_driver;

/*
 * Initialize this module
 */
void ifd_cm4000_register(void)
{
	cm4000_driver.open = cm_open;
	cm4000_driver.activate = cm_activate;
	cm4000_driver.deactivate = cm_deactivate;
	cm4000_driver.card_reset = cm_card_reset;
	cm4000_driver.card_status = cm_card_status;
	cm4000_driver.send = cm_send;
	cm4000_driver.recv = cm_recv;

	ifd_driver_register("cm4000", &cm4000_driver);
}

#else

/*
 * Initialize this module
 */
void ifd_cm4000_register(void)
{
}

#endif
