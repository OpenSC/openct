/*
 * IFD reader
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

static int ifd_recv_atr(ifd_device_t *, ct_buf_t *, unsigned int, int);

/*
 * Initialize a reader and open the device
 */
ifd_reader_t *ifd_open(const char *driver_name, const char *device_name)
{
	const ifd_driver_t *driver;
	ifd_reader_t *reader;

	ifd_debug(1, "trying to open %s@%s", driver_name, device_name);
	driver = ifd_driver_get(driver_name);
	if (driver == NULL) {
		ct_error("%s: driver not available", driver_name);
		return NULL;
	}

	reader = (ifd_reader_t *) calloc(1, sizeof(*reader));
	if (!reader) {
		ct_error("out of memory");
		return NULL;
	}
	reader->driver = driver;

	if (driver->ops->open && driver->ops->open(reader, device_name) < 0) {
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
int ifd_set_protocol(ifd_reader_t * reader, unsigned int idx, int prot)
{
	const ifd_driver_t *drv = reader->driver;
	ifd_slot_t *slot;
	ifd_protocol_t *p;

	if (idx > reader->nslots)
		return -1;

	if (drv && drv->ops && drv->ops->set_protocol)
		return drv->ops->set_protocol(reader, idx, prot);

	if (drv && drv->ops && prot == IFD_PROTOCOL_DEFAULT)
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

#if 0
/*
 * Set the serial speed at which we communicate with the
 * reader
 */
int ifd_set_speed(ifd_reader_t * reader, unsigned int speed)
{
	const ifd_driver_t *drv = reader->driver;
	int rc = 0;

	if (drv && drv->ops && drv->ops->change_speed)
		rc = drv->ops->change_speed(reader, speed);
	else
		rc = IFD_ERROR_NOT_SUPPORTED;
	return rc;
}
#endif

/*
 * Activate/Deactivate the reader
 */
int ifd_activate(ifd_reader_t * reader)
{
	const ifd_driver_t *drv = reader->driver;
	int rc = 0;

	if (drv && drv->ops && drv->ops->activate)
		rc = drv->ops->activate(reader);
	reader->flags |= IFD_READER_ACTIVE;
	return rc;
}

int ifd_deactivate(ifd_reader_t * reader)
{
	const ifd_driver_t *drv = reader->driver;
	int rc = 0;

	if (drv && drv->ops && drv->ops->deactivate)
		rc = drv->ops->deactivate(reader);
	reader->flags &= ~IFD_READER_ACTIVE;
	return rc;
}

/*
 * Output to reader's display
 */
int ifd_output(ifd_reader_t * reader, const char *message)
{
	const ifd_driver_t *drv = reader->driver;

	if (!drv || !drv->ops || !drv->ops->output)
		return IFD_ERROR_NOT_SUPPORTED;

	return drv->ops->output(reader, message);
}

/*
 * Detect card status
 */
int ifd_card_status(ifd_reader_t * reader, unsigned int idx, int *status)
{
	const ifd_driver_t *drv = reader->driver;
	int rc;

	if (idx > reader->nslots) {
		ct_error("%s: invalid slot number %u", reader->name, idx);
		return -1;
	}

	*status = 0;
	if (!drv || !drv->ops || !drv->ops->card_status)
		return IFD_ERROR_NOT_SUPPORTED;

	if ((rc = drv->ops->card_status(reader, idx, status)) < 0)
		return rc;
	if (*status & IFD_CARD_STATUS_CHANGED)
		reader->slot[idx].atr_len = 0;
	reader->slot[idx].status = *status;

	return 0;
}

/*
 * Reset card and obtain ATR
 */
int ifd_card_reset(ifd_reader_t * reader, unsigned int idx, void *atr,
		   size_t size)
{
	return ifd_card_request(reader, idx, 0, NULL, atr, size);
}

/*
 * Request ICC
 */
int ifd_card_request(ifd_reader_t * reader, unsigned int idx, time_t timeout,
		     const char *message, void *atr, size_t size)
{
	const ifd_driver_t *drv = reader->driver;
	ifd_device_t *dev = reader->device;
	ifd_slot_t *slot;
	unsigned int count;
	int n, parity;

	if (idx > reader->nslots) {
		ct_error("%s: invalid slot number %u", reader->name, idx);
		return IFD_ERROR_INVALID_ARG;
	}

	if (dev == NULL)
		return IFD_ERROR_INVALID_ARG;

	if (!drv || !drv->ops || !drv->ops->card_reset)
		return IFD_ERROR_NOT_SUPPORTED;

	slot = &reader->slot[idx];
	slot->atr_len = 0;

	if (slot->proto) {
		ifd_protocol_free(slot->proto);
		slot->proto = NULL;
	}

	/* Do the reset thing - if the driver supports
	 * request ICC, call the function if needed.
	 * Otherwise fall back to ordinary reset.
	 *
	 * For asynchronous cards, the driver's card_reset
	 * function should perform the reset, and start to
	 * read the ATR. It should either read the first byte
	 * of the ATR, and leave it to the caller to read
	 * the remaining bytes of it, or it should read the
	 * whole ATR (as done by the B1 driver, for instance).
	 *
	 * When receiving the complete ATR, we will select
	 * the default protocol as specified by the card.
	 * 
	 * If the driver was unable to receive the ATR
	 * (e.g. because the command timed out) it should
	 * return IFD_ERROR_NO_ATR. This will allow us
	 * to retry with different parity.
	 *
	 * For synchronous cards, the driver can call
	 * ifd_sync_detect_icc to detect whether the card
	 * is synchronous. This will also set the slot's
	 * protocol.
	 *
	 * If the card driver does it's own handling of sync
	 * ICCs, it should call ifd_set_protocol to signal
	 * that card detection was successful.
	 */
	if (drv->ops->card_request && (timeout || message)) {
		n = drv->ops->card_request(reader, idx,
					   timeout, message, slot->atr,
					   sizeof(slot->atr));
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
		if ((n = drv->ops->change_parity(reader, parity)) < 0)
			return n;

		/* Reset the card */
		n = drv->ops->card_reset(reader, idx,
					 slot->atr, sizeof(slot->atr));

		/* If there was no ATR, try again with odd parity */
		if (n == IFD_ERROR_NO_ATR) {
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

		/* If we got just the first byte of the (async) ATR,
		 * get the rest now */
		if (count == 1) {
			ct_buf_t rbuf;
			unsigned char c;
			unsigned int num, proto = 0;
			int revert_bits = 0;

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
				if (ifd_recv_atr(dev, &rbuf, num, revert_bits) <
				    0)
					return -1;

				if (!(c & 0x80))
					break;

				c = rbuf.base[rbuf.tail - 1];
				proto = c & 0xF;
			}

			/* Historical bytes */
			c = rbuf.base[1] & 0xF;
			if (ifd_recv_atr(dev, &rbuf, c, revert_bits) < 0)
				return -1;

			/* If a protocol other than T0 was specified,
			 * read check byte */
			if (proto
			    && ifd_recv_atr(dev, &rbuf, 1, revert_bits) < 0)
				return -1;

			if (slot->atr[0] == 0x3F)
				parity = IFD_SERIAL_PARITY_TOGGLE(parity);
			count = rbuf.tail - rbuf.head;
		}

		ifd_debug(1, "received atr:%s", ct_hexdump(slot->atr, count));

		/* Set the parity in case it was toggled */
		if (drv->ops->change_parity(reader, parity) < 0)
			return -1;
	}

	slot->atr_len = count;

	if (count > size)
		size = count;
	if (atr)
		memcpy(atr, slot->atr, count);

	/* For synchronous cards, the slot's protocol will already
	 * be set when we get here. */
	if (slot->proto == NULL) {
		if (!ifd_protocol_select(reader, idx, IFD_PROTOCOL_DEFAULT))
			ct_error("Protocol selection failed");
	}

	return count;
}

static int ifd_recv_atr(ifd_device_t * dev, ct_buf_t * bp, unsigned int count,
			int revert_bits)
{
	unsigned char *buf;
	unsigned int n;

	if (count > ct_buf_tailroom(bp)) {
		ct_error("ATR buffer too small");
		return -1;
	}

	buf = (unsigned char *)ct_buf_tail(bp);
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
 * Check ATR for completeness
 */
int ifd_atr_complete(const unsigned char *atr, size_t len)
{
	unsigned int j = 2, c;
	int proto = 0;

	do {
		if (j > len)
			return 0;
		c = atr[j - 1];
		if (j > 2)
			proto = c & 0xF;
		j += ifd_count_bits(c & 0xF0);
	} while (c & 0x80);

	/* Historical bytes */
	if ((j += (atr[1] & 0xF)) > len)
		return 0;

	/* If a protocol other than T0 was specified,
	 * read check byte */
	if (proto && j + 1 > len)
		return 0;

	return 1;
}

/*
 * Eject the card
 */
int ifd_card_eject(ifd_reader_t * reader, unsigned int idx, time_t timeout,
		   const char *message)
{
	const ifd_driver_t *drv = reader->driver;

	if (idx > reader->nslots) {
		ct_error("%s: invalid slot number %u", reader->name, idx);
		return -1;
	}

	if (!drv || !drv->ops || !drv->ops->card_eject)
		return 0;

	return drv->ops->card_eject(reader, idx, timeout, message);
}

/*
 * Perform a PIN verification, using the reader's pin pad
 */
int ifd_card_perform_verify(ifd_reader_t * reader, unsigned int idx,
			    time_t timeout, const char *message,
			    const unsigned char *data, size_t data_len,
			    unsigned char *resp, size_t resp_len)
{
	const ifd_driver_t *drv = reader->driver;

	if (idx > reader->nslots) {
		ct_error("%s: invalid slot number %u", reader->name, idx);
		return -1;
	}

	if (!drv || !drv->ops || !drv->ops->perform_verify)
		return IFD_ERROR_NOT_SUPPORTED;

	return drv->ops->perform_verify(reader, idx, timeout, message,
					data, data_len, resp, resp_len);
}

/*
 * Send/receive APDU to the ICC
 */
int ifd_card_command(ifd_reader_t * reader, unsigned int idx, const void *sbuf,
		     size_t slen, void *rbuf, size_t rlen)
{
	ifd_slot_t *slot;

	if (idx > reader->nslots)
		return -1;

	/* XXX handle driver specific methods of transmitting
	 * commands */

	slot = &reader->slot[idx];
	if (slot->proto == NULL) {
		ct_error("No communication protocol selected");
		return -1;
	}

	/* An application is talking to the card. Prevent
	 * automatic card status updates from slowing down
	 * things */
	slot->next_update = time(NULL) + 1;

	return ifd_protocol_transceive(slot->proto, slot->dad,
				       sbuf, slen, rbuf, rlen);
}

/*
 * Read/write synchronous ICCs
 */
int ifd_card_read_memory(ifd_reader_t * reader, unsigned int idx,
			 unsigned short addr, unsigned char *rbuf, size_t rlen)
{
	ifd_slot_t *slot;

	if (idx > reader->nslots)
		return -1;

	slot = &reader->slot[idx];
	if (slot->proto == NULL) {
		ct_error("No communication protocol selected");
		return -1;
	}

	/* An application is talking to the card. Prevent
	 * automatic card status updates from slowing down
	 * things */
	slot->next_update = time(NULL) + 1;

	return ifd_protocol_read_memory(slot->proto, idx, addr, rbuf, rlen);
}

int ifd_card_write_memory(ifd_reader_t * reader, unsigned int idx,
			  unsigned short addr, const unsigned char *sbuf,
			  size_t slen)
{
	ifd_slot_t *slot;

	if (idx > reader->nslots)
		return -1;

	slot = &reader->slot[idx];
	if (slot->proto == NULL) {
		ct_error("No communication protocol selected");
		return -1;
	}

	/* An application is talking to the card. Prevent
	 * automatic card status updates from slowing down
	 * things */
	slot->next_update = time(NULL) + 1;

	return ifd_protocol_write_memory(slot->proto, idx, addr, sbuf, slen);
}

/*
 * Transfer/receive APDU using driver specific mechanisms
 * This functions is called from the protocol (T=0,1,...) layer
 */
int ifd_send_command(ifd_protocol_t * prot, const void *buffer, size_t len)
{
	const ifd_driver_t *drv;

	if (!prot || !prot->reader || !(drv = prot->reader->driver)
	    || !drv->ops || !drv->ops->send)
		return -1;

	return drv->ops->send(prot->reader, prot->dad,
			      (const unsigned char *)buffer, len);
}

int
ifd_recv_response(ifd_protocol_t * prot, void *buffer, size_t len, long timeout)
{
	const ifd_driver_t *drv;

	if (!prot || !prot->reader || !(drv = prot->reader->driver)
	    || !drv->ops || !drv->ops->recv)
		return -1;

	return drv->ops->recv(prot->reader, prot->dad, (unsigned char *)buffer,
			      len, timeout);
}

/*
 * Shut down reader
 */
void ifd_close(ifd_reader_t * reader)
{
	ifd_detach(reader);

	if (reader->driver->ops->close)
		reader->driver->ops->close(reader);

	if (reader->device)
		ifd_device_close(reader->device);

	memset(reader, 0, sizeof(*reader));
	free(reader);
}

/*
 * Before command
 */
int ifd_before_command(ifd_reader_t *reader)
{
	if (reader->driver->ops->before_command)
		return reader->driver->ops->before_command(reader);
	else
		return 0;
}

/*
 * After command
 */
int ifd_after_command(ifd_reader_t *reader)
{
	if (reader->driver->ops->after_command)
		return reader->driver->ops->after_command(reader);
	else
		return 0;
}

/*
 * Get eventfd
 */
int ifd_get_eventfd(ifd_reader_t *reader, short *events)
{
	if (reader->driver->ops->get_eventfd) {
		return reader->driver->ops->get_eventfd(reader, events);
	}
	else {
		return -1;
	}
}

static void ifd_slot_status_update(ifd_reader_t *reader, int slot, int status)
{
	static unsigned int card_seq = 1;

	ct_info_t *info = reader->status;
	unsigned int prev_seq, new_seq;

	new_seq = prev_seq = info->ct_card[slot];

	if (!(status & IFD_CARD_PRESENT)) {
		new_seq = 0;
	}
	else if (!prev_seq || (status & IFD_CARD_STATUS_CHANGED)) {
		new_seq = card_seq++;
	}

	if (prev_seq != new_seq) {
		ifd_debug(1, "card status change slot %d: %u -> %u",
			  slot, prev_seq, new_seq);
		info->ct_card[slot] = new_seq;
		ct_status_update(info);
	}
}

void ifd_poll(ifd_reader_t *reader)
{
	unsigned slot;

	/* Check if the card status changed */
	for (slot = 0; slot < reader->nslots; slot++) {
		time_t now;
		int status;

		time(&now);
		if (now < reader->slot[slot].next_update)
			continue;

		/* Poll card status at most once a second
		 * XXX: make this configurable */
		reader->slot[slot].next_update = now + 1;

		if (ifd_card_status(reader, slot, &status) < 0) {
			/* Don't return error; let the hotplug test
			 * pick up the detach
			 if (rc == IFD_ERROR_DEVICE_DISCONNECTED)
			 return rc;
			 */
			continue;
		}

		ifd_slot_status_update(reader, slot, status);
	}
}

int ifd_error(ifd_reader_t *reader)
{
	if (reader->driver->ops->error == NULL) {
		return IFD_ERROR_NOT_SUPPORTED;
	}

	return reader->driver->ops->error(reader);
}

int ifd_event(ifd_reader_t *reader)
{
	int status[OPENCT_MAX_SLOTS];
	unsigned slot;
	int rc;

	if (reader->driver->ops->event == NULL) {
		return IFD_ERROR_NOT_SUPPORTED;
	}

	rc = reader->driver->ops->event(reader, status, reader->nslots);

	for (slot=0;slot<reader->nslots;slot++) {
		ifd_slot_status_update(reader, slot, status[slot]);
	}

	return rc;
}

