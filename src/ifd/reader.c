/*
 * IFD reader
 *
 */

#include <stdlib.h>
#include "internal.h"

static ifd_reader_t *	ifd_new_reader(ifd_device_t *, const char *);
static int		ifd_recv_atr(ifd_device_t *, ifd_buf_t *,
				unsigned int, int);

/*
 * New serial reader
 */
ifd_reader_t *
ifd_new_serial(const char *device_name, const char *driver_name)
{
	ifd_device_t	*dev;
	ifd_reader_t	*reader;

	if (!(dev = ifd_open_serial(device_name))) {
		ifd_error("Unable to open %s: %m", device_name);
		return NULL;
	}

	if (!(reader = ifd_new_reader(dev, driver_name))) {
		ifd_device_close(dev);
		return NULL;
	}

	return reader;
}

/*
 * New USB reader
 */
ifd_reader_t *
ifd_new_usb(const char *device_name, const char *driver_name)
{
	ifd_device_t	*dev;
	ifd_reader_t	*reader;

	if (!(dev = ifd_open_usb(device_name))) {
		ifd_error("Unable to open %s: %m", device_name);
		return NULL;
	}

	if (!(reader = ifd_new_reader(dev, driver_name))) {
		ifd_device_close(dev);
		return NULL;
	}

	return reader;
}

/*
 * Initialize a reader
 */
static ifd_reader_t *
ifd_new_reader(ifd_device_t *dev, const char *driver_name)
{
	const ifd_driver_t *driver;
	ifd_reader_t	*reader;

	if (!driver_name || !strcmp(driver_name, "auto")) {
		char	pnp_id[64];

		if (ifd_device_identify(dev, pnp_id, sizeof(pnp_id)) < 0) {
			ifd_error("%s: unable to identify device, "
			      "please specify driver",
			      dev->name);
			return NULL;
		}
		if (!(driver_name = ifd_driver_for_id(pnp_id))) {
			ifd_error("%s: no driver for ID %s, "
			      "please specify driver",
			      dev->name, pnp_id);
			return NULL;
		}

		driver = ifd_driver_get(driver_name);
		if (driver == NULL) {
			ifd_error("%s: driver \"%s\" not available "
				  "(identified as %s)",
				   dev->name, driver_name, pnp_id);
			return NULL;
		}
	} else {
		driver = ifd_driver_get(driver_name);
		if (driver == NULL) {
			ifd_error("%s: driver not available", driver_name);
			return NULL;
		}
	}

	reader = (ifd_reader_t *) calloc(1, sizeof(*reader));
	reader->device = dev;
	reader->driver = driver;

	if (driver->ops->open
	 && driver->ops->open(reader) < 0) {
		ifd_error("%s: initialization failed (driver %s)",
				dev->name, driver->name);
		free(reader);
		return NULL;
	}

	if (ifd_set_protocol(reader, IFD_PROTOCOL_DEFAULT) < 0) {
		free(reader);
		return NULL;
	}

	return reader;
}

/*
 * Select a different protocol for this reader
 */
int
ifd_set_protocol(ifd_reader_t *reader, int prot)
{
	const ifd_driver_t *drv = reader->driver;
	ifd_protocol_t *p;

	if (drv && drv->ops && drv->ops->set_protocol)
		return drv->ops->set_protocol(reader, prot);

	if (prot == IFD_PROTOCOL_DEFAULT)
		prot = drv->ops->default_protocol;
	p = ifd_protocol_by_id(prot);
	if (p == 0) {
		ifd_error("unknown protocol id %u\n", prot);
		return -1;
	}

	reader->proto = p;
	return 0;
}

/*
 * Activate/Deactivate the reader
 */
int
ifd_activate(ifd_reader_t *reader)
{
	const ifd_driver_t *drv = reader->driver;

	if (drv && drv->ops && drv->ops->activate)
		return drv->ops->activate(reader);
	return 0;
}

int
ifd_deactivate(ifd_reader_t *reader)
{
	const ifd_driver_t *drv = reader->driver;

	if (drv && drv->ops && drv->ops->deactivate)
		return drv->ops->deactivate(reader);
	return 0;
}

/*
 * Detect card status
 */
int
ifd_card_status(ifd_reader_t *reader, unsigned int idx, int *status)
{
	const ifd_driver_t *drv = reader->driver;

	if (idx > reader->nslots) {
		ifd_error("%s: invalid slot number %u", reader->name, idx);
		return -1;
	}

	*status = 0;
	if (!drv || !drv->ops || !drv->ops->card_status)
		return -1;

	if (drv->ops->card_status(reader, idx, status) < 0)
		return -1;
	if (*status & IFD_CARD_STATUS_CHANGED)
		reader->slot[idx].atr_len = 0;
	reader->slot[idx].status = *status;

	return 0;
}

/*
 * Reset card and obtain ATR
 */
int
ifd_card_reset(ifd_reader_t *reader, unsigned int idx, void *atr, size_t size)
{
	const ifd_driver_t *drv = reader->driver;
	ifd_device_t	*dev = reader->device;
	ifd_slot_t	*slot;
	unsigned int	count;
	int		n, parity;

	if (idx > reader->nslots) {
		ifd_error("%s: invalid slot number %u", reader->name, idx);
		return -1;
	}

	if (!drv || !drv->ops || !drv->ops->card_reset || !dev)
		return -1;

	slot = &reader->slot[idx];
	slot->atr_len = 0;

	/* Serial devices need special frobbing */
	if (dev->type != IFD_DEVICE_TYPE_SERIAL
	 || !drv->ops->change_parity) {
		n = drv->ops->card_reset(reader, idx,
					 slot->atr, sizeof(slot->atr));
		if (n <= 0)
			return n;
		count = n;
	} else {
		parity = IFD_SERIAL_PARITY_EVEN;
		if (drv->ops->change_parity(reader, parity) < 0)
			return -1;

		/* Reset the card */
		n = drv->ops->card_reset(reader, idx,
					 slot->atr, sizeof(slot->atr));

		/* If there was no ATR, try again with odd parity */
		if (n == 0) {
			parity = IFD_SERIAL_PARITY_TOGGLE(parity);
			if (drv->ops->change_parity(reader, parity) < 0)
				return -1;
			n = drv->ops->card_reset(reader, idx,
						 slot->atr, sizeof(slot->atr));
		}

		/* Bail out in case of general error */
		if (n < 0)
			return -1;

		count = n;

		/* If we got just the first byte of the ATR, get the
		 * rest now */
		if (count == 1) {
			ifd_buf_t	rbuf;
			unsigned char	c;
			unsigned int	num, proto = 0;
			int		revert_bits = 0;

			if (slot->atr[0] == 0x03) {
				revert_bits = 1;
				slot->atr[0] = 0x3F;
			}

			ifd_buf_init(&rbuf, slot->atr, sizeof(slot->atr));
			rbuf.tail++;

			if (ifd_recv_atr(dev, &rbuf, 1, revert_bits) < 0)
				return -1;

			while ((num = ifd_count_bits(c & 0xF0)) != 0) {
				if (ifd_recv_atr(dev, &rbuf, num, revert_bits) < 0)
					return -1;

				if (c & 0x80) {
					c = rbuf.base[rbuf.tail-1];
					proto = c & 0xF;
				} else {
					c &= 0xF;
				}
			}

			/* Historical bytes */
			c = rbuf.base[1] & 0xF;
			if (ifd_recv_atr(dev, &rbuf, c, revert_bits) < 0)
				return -1;

			/* If a protocol other than T0 was specified,
			 * read check byte */
			if (proto && ifd_recv_atr(dev, &rbuf, 1, revert_bits) < 0)
				return -1;

			if (slot->atr[0] == 0x3F)
				parity = IFD_SERIAL_PARITY_TOGGLE(parity);
			count = rbuf.tail - rbuf.head;
		}

		/* Set the parity in case it was toggled */
		if (drv->ops->change_parity(reader, parity) < 0)
			return -1;
	}

	slot->atr_len = count;

	if (count > size)
		size = count;
	if (atr)
		memcpy(atr, slot->atr, count);

	return count;
}

int
ifd_recv_atr(ifd_device_t *dev, ifd_buf_t *bp,
		unsigned int count,
		int revert_bits)
{
	if (count > ifd_buf_tailroom(bp)) {
		ifd_error("ATR buffer too small");
		return -1;
	}

	if (ifd_device_recv(dev, bp->base + bp->tail, count, 1000000) < 0) {
		ifd_error("failed to receive ATR");
		return -1;
	}

	if (revert_bits)
		ifd_revert_bits(bp->base + bp->tail, count);

	bp->tail += count;
	return count;
}

/*
 * Send/receive APDU
 */
int
ifd_transceive(ifd_reader_t *reader, int dad, ifd_apdu_t *apdu)
{
	ifd_error("ifd_transceive: not yet implemented");
	return -1;
}

/*
 * Shut down reader
 */
void
ifd_shutdown(ifd_reader_t *reader)
{
	ifd_detach(reader);

	if (reader->device)
		ifd_device_close(reader->device);

	memset(reader, 0, sizeof(*reader));
	free(reader);
}
