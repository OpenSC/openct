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

#include "cardman.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

typedef struct cm_priv {
	int icc_proto;
	unsigned char rbuf[64];
	unsigned int head, tail;
} cm_priv_t;

static int cm_set_card_parameters(ifd_device_t *, unsigned int baudRate);

/*
 * Initialize the device
 */
static int cm_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	cm_priv_t *priv;
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

	priv = (cm_priv_t *) calloc(1, sizeof(cm_priv_t));
	if (!priv) {
		ct_error("out of memory");
		return IFD_ERROR_NO_MEMORY;
	}

	reader->driver_data = priv;
	reader->device = dev;
	dev->timeout = 2000;

	return 0;
}

/*
 * Power up the card slot
 */
static int cm_activate(ifd_reader_t * reader)
{
	ifd_device_t *dev = reader->device;
	int rc;

	ifd_debug(1, "called.");
	/* Set async card @9600 bps, 2 stop bits, even parity */
	if ((rc = cm_set_card_parameters(dev, 0x01)) < 0) {
		ct_error("cm4000: failed to set card parameters 9600/8E2");
		return rc;
	}
	return 0;
}

static int cm_deactivate(ifd_reader_t * reader)
{
	ifd_device_t *dev = reader->device;
	int rc;

	ifd_debug(1, "called.");
	if ((rc = ifd_usb_control(dev, 0x42, 0x11, 0, 0, NULL, 0, -1)) < 0) {
		ct_error("cm4000: failed to deactivate card");
		return rc;
	}
	return 0;
}

/*
 * Card status - always present
 */
static int cm_card_status(ifd_reader_t * reader, int slot, int *status)
{
	ifd_device_t *dev = reader->device;
	unsigned char cm_status = 0;
	int rc;

	*status = 0;

#if 0
	if ((rc =
	     cm_usb_int(dev, 0x42, 0x20, 0, 0, NULL, 0, &cm_status, 1, NULL,
			-1)) < 0) {
		ct_error("cm4000: failed to get card status");
		return -1;
	}
	if (rc == 1 && (cm_status & 0x42))
		*status = IFD_CARD_PRESENT;
#endif
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

	// CM_IOCGATR
	if (ioctl(dev->fd, 0x0C0046301, &cmatr) != 0) {
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
	cm_priv_t *priv = (cm_priv_t *) reader->driver_data;

	return write(dev->fd, buffer, len);
}

static int cm_recv(ifd_reader_t * reader, unsigned int dad,
		   unsigned char *buffer, size_t len, long timeout)
{
	ifd_device_t *dev = reader->device;
	cm_priv_t *priv = (cm_priv_t *) reader->driver_data;

	return read(dev->fd, buffer, len);
}

/*
 * Set the card's baud rate etc
 */
static int cm_set_card_parameters(ifd_device_t * dev, unsigned int baudrate)
{
	/* this is done in kernel driver, nothing we can do about that */
	return 0;
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
	cm4000_driver.send = cm_send;
	cm4000_driver.recv = cm_recv;

	ifd_driver_register("cm4000", &cm4000_driver);
}
