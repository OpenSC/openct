/*
 * IFD reader
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <string.h>
#include "internal.h"

static int		ifd_recv_atr(ifd_device_t *, ct_buf_t *,
				unsigned int, int);

/*
 * Initialize a reader and open the device
 */
ifd_reader_t *
ifd_open(const char *driver_name, const char *device_name)
{
	const ifd_driver_t *driver;
	ifd_reader_t	*reader;

	if (!driver_name || !strcmp(driver_name, "auto")) {
		char	pnp_id[64];

		if (ifd_device_identify(device_name, pnp_id, sizeof(pnp_id)) < 0) {
			ct_error("%s: unable to identify device, "
			      "please specify driver",
			      device_name);
			return NULL;
		}
		if (!(driver_name = ifd_driver_for_id(pnp_id))) {
			ct_error("%s: no driver for ID %s, "
			      "please specify driver",
			      device_name, pnp_id);
			return NULL;
		}

		driver = ifd_driver_get(driver_name);
		if (driver == NULL) {
			ct_error("%s: driver \"%s\" not available "
				  "(identified as %s)",
				   device_name, driver_name, pnp_id);
			return NULL;
		}
	} else {
		driver = ifd_driver_get(driver_name);
		if (driver == NULL) {
			ct_error("%s: driver not available", driver_name);
			return NULL;
		}
	}

	reader = (ifd_reader_t *) calloc(1, sizeof(*reader));
	reader->driver = driver;

	if (driver->ops->open
	 && driver->ops->open(reader, device_name) < 0) {
		ct_error("%s: initialization failed (driver %s)",
				device_name, driver->name);
		free(reader);
		return NULL;
	}

	return reader;
}

/*
 * Select a different protocol for this reader
 */
int
ifd_set_protocol(ifd_reader_t *reader, unsigned int idx, int prot)
{
	const ifd_driver_t *drv = reader->driver;
	ifd_slot_t *slot;
	ifd_protocol_t *p;

	if (idx > reader->nslots)
		return -1;

	if (drv && drv->ops && drv->ops->set_protocol)
		return drv->ops->set_protocol(reader, idx, prot);

	if (prot == IFD_PROTOCOL_DEFAULT)
		prot = drv->ops->default_protocol;

	slot = &reader->slot[idx];
	if (slot->proto && slot->proto->ops->id == prot)
		return 0;

	if (!(p = ifd_protocol_new(prot, reader, slot->dad)))
		return -1;

	if (slot->proto) {
		ifd_protocol_free(slot->proto);
		slot->proto = NULL;
	}

	slot->proto = p;
	return 0;
}

/*
 * Activate/Deactivate the reader
 */
int
ifd_activate(ifd_reader_t *reader)
{
	const ifd_driver_t *drv = reader->driver;
	int		rc = 0;

	if (drv && drv->ops && drv->ops->activate)
		rc = drv->ops->activate(reader);
	reader->flags |= IFD_READER_ACTIVE;
	return rc;
}

int
ifd_deactivate(ifd_reader_t *reader)
{
	const ifd_driver_t *drv = reader->driver;
	int		rc = 0;

	if (drv && drv->ops && drv->ops->deactivate)
		rc = drv->ops->deactivate(reader);
	reader->flags &= ~IFD_READER_ACTIVE;
	return rc;
}

/*
 * Detect card status
 */
int
ifd_card_status(ifd_reader_t *reader, unsigned int idx, int *status)
{
	const ifd_driver_t *drv = reader->driver;

	if (idx > reader->nslots) {
		ct_error("%s: invalid slot number %u", reader->name, idx);
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
	return ifd_card_request(reader, idx, 0, NULL, atr, size);
}

/*
 * Request ICC
 */
int
ifd_card_request(ifd_reader_t *reader, unsigned int idx,
		time_t timeout, const char *message,
		void *atr, size_t size)
{
	const ifd_driver_t *drv = reader->driver;
	ifd_device_t	*dev = reader->device;
	ifd_slot_t	*slot;
	unsigned int	count;
	int		n, parity;

	if (idx > reader->nslots) {
		ct_error("%s: invalid slot number %u", reader->name, idx);
		return -1;
	}

	if (!drv || !drv->ops || !drv->ops->card_reset || !dev)
		return -1;

	slot = &reader->slot[idx];
	slot->atr_len = 0;

	if (slot->proto) {
		ifd_protocol_free(slot->proto);
		slot->proto = NULL;
	}

	/* Do the reset thing - if the driver supports
	 * request ICC, call the function if needed.
	 * Otherwise fall back to ordinary reset */
	if (drv->ops->card_request && (timeout || message)) {
		n = drv->ops->card_request(reader, idx,
				timeout, message, atr, size);
		if (n <= 0)
			return n;
		count = n;
	} else
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
			ct_buf_t	rbuf;
			unsigned char	c;
			unsigned int	num, proto = 0;
			int		revert_bits = 0;

			if (slot->atr[0] == 0x03) {
				revert_bits = 1;
				slot->atr[0] = 0x3F;
			}

			ct_buf_init(&rbuf, slot->atr, sizeof(slot->atr));
			rbuf.tail++;

			if (ifd_recv_atr(dev, &rbuf, 1, revert_bits) < 0)
				return -1;

			c = rbuf.base[1];
			while (1) {
				num = ifd_count_bits(c & 0xF0);
				if (ifd_recv_atr(dev, &rbuf, num, revert_bits) < 0)
					return -1;

				if (!(c & 0x80))
					break;

				c = rbuf.base[rbuf.tail-1];
				proto = c & 0xF;
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

	if (!ifd_protocol_select(reader, idx, IFD_PROTOCOL_DEFAULT))
		ct_error("Protocol selection failed");

	return count;
}

int
ifd_recv_atr(ifd_device_t *dev, ct_buf_t *bp,
		unsigned int count,
		int revert_bits)
{
	unsigned char	*buf;
	unsigned int	n;

	if (count > ct_buf_tailroom(bp)) {
		ct_error("ATR buffer too small");
		return -1;
	}

	buf = ct_buf_tail(bp);
	for (n = 0; n < count; n++) {
		if (ifd_device_recv(dev, buf + n, 1, 1000) < 0) {
			ct_error("failed to receive ATR");
			return -1;
		}
	}

	if (revert_bits)
		ifd_revert_bits(buf, count);

	/* Advance tail pointer */
	ct_buf_put(bp, NULL, count);
	return count;
}

/*
 * Send/receive APDU to the ICC
 */
int
ifd_card_command(ifd_reader_t *reader, unsigned int idx,
		 const void *sbuf, size_t slen,
		 void *rbuf, size_t rlen)
{
	ifd_slot_t	*slot;

	if (idx > reader->nslots)
		return -1;

	/* XXX handle driver specific methods of transmitting
	 * commands */

	slot = &reader->slot[idx];
	if (slot->proto == NULL) {
		ct_error("No communication protocol selected");
		return -1;
	}

	return ifd_protocol_transceive(slot->proto, slot->dad,
				sbuf, slen, rbuf, rlen);
}

/*
 * Transfer/receive APDU using driver specific mechanisms
 * This functions is called from the protocol (T=0,1,...) layer
 */
int
ifd_send_command(ifd_protocol_t *prot, const void *buffer, size_t len)
{
	const ifd_driver_t *drv;

	if (!prot || !prot->reader
	 || !(drv = prot->reader->driver)
	 || !drv->ops || !drv->ops->send)
		return -1;

	return drv->ops->send(prot->reader, prot->dad, buffer, len);
}

int
ifd_recv_response(ifd_protocol_t *prot, void *buffer, size_t len, long timeout)
{
	const ifd_driver_t *drv;

	if (!prot || !prot->reader
	 || !(drv = prot->reader->driver)
	 || !drv->ops || !drv->ops->recv)
		return -1;

	return drv->ops->recv(prot->reader, prot->dad, buffer, len, timeout);
}

/*
 * Shut down reader
 */
void
ifd_close(ifd_reader_t *reader)
{
	ifd_detach(reader);

	if (reader->device)
		ifd_device_close(reader->device);

	memset(reader, 0, sizeof(*reader));
	free(reader);
}
