/*
 * Implementation of synchronous protocols
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <sys/poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	ifd_protocol_t base;
} sync_state_t;

/*
 * Detach protocol
 */
static void sync_release(ifd_protocol_t * prot)
{
	/* NOP */
}

/*
 * Read/write operations
 */
static int sync_read(ifd_protocol_t * prot, int slot, unsigned short addr,
		     unsigned char *rbuf, size_t rlen)
{
	ifd_reader_t *reader = prot->reader;
	const ifd_driver_t *drv;

	if (!(drv = reader->driver) || !drv->ops || !drv->ops->sync_read)
		return IFD_ERROR_NOT_SUPPORTED;

	return drv->ops->sync_read(reader, slot, prot->ops->id,
				   addr, rbuf, rlen);
}

static int sync_write(ifd_protocol_t * prot, int slot, unsigned short addr,
		      const unsigned char *sbuf, size_t slen)
{
	ifd_reader_t *reader = prot->reader;
	const ifd_driver_t *drv;
	unsigned int retries = 1;
	int prot_id;

	if (!(drv = reader->driver) || !drv->ops || !drv->ops->sync_read)
		return IFD_ERROR_NOT_SUPPORTED;

	prot_id = prot->ops->id;
	if ((prot_id == IFD_PROTOCOL_I2C_SHORT
	     || prot_id == IFD_PROTOCOL_I2C_LONG) && slen > 1)
		retries = 2;

	while (slen) {
		unsigned char temp[256];
		size_t count;
		int r;

		/* All bytes must be within a 256 page.
		 * Is this generic, or a Towitoko requirement?
		 */
		count = 256 - (addr & 255);
		if (count > slen)
			count = slen;

		ifd_debug(2, "writing %u@%04x", count, addr);
		r = drv->ops->sync_write(reader, slot,
					 prot_id, addr, sbuf, count);

		if (r < 0)
			return r;

		/* Verify that data was written correctly */
		ifd_debug(2, "verifying %u@%04x", count, addr);
		r = drv->ops->sync_read(reader, slot,
					prot_id, addr, temp, count);

		if (memcmp(sbuf, temp, count)) {
			ifd_debug(2, "failed to verify write");
			if (retries--)
				continue;
			return -1;
		}

		addr += count;
		sbuf += count;
		slen -= count;
	}

	return 0;
}

/*
 * Probe for sync ICC type
 */
static ifd_protocol_t *ifd_sync_probe_icc(ifd_reader_t * reader, int slot,
					  int proto)
{
	ifd_protocol_t *p;
	unsigned char byte;
	int res = 0;

	if (ifd_deactivate(reader) < 0 || ifd_activate(reader) < 0)
		return NULL;

	if (!(p = ifd_protocol_new(proto, reader, slot)))
		return NULL;

	if (ifd_protocol_read_memory(p, slot, 0, &byte, 1) != 1)
		goto out;

	if (proto == IFD_PROTOCOL_2WIRE || proto == IFD_PROTOCOL_3WIRE) {
		if (byte != 0x00 && byte != 0x0FF)
			res = 1;
	} else {
		byte = ~byte;
		if (ifd_protocol_write_memory(p, slot, 0, &byte, 1) >= 0) {
			byte = ~byte;
			ifd_protocol_write_memory(p, slot, 0, &byte, 1);
			res = 1;
		}
	}

      out:
	if (!res) {
		ifd_protocol_free(p);
		p = NULL;
	}
	return p;
}

/*
 * Detect synchronous ICC
 */
int ifd_sync_detect_icc(ifd_reader_t * reader, int slot, void *atr, size_t size)
{
	ifd_protocol_t *p = NULL;
	int n;

	if ((p = ifd_sync_probe_icc(reader, slot, IFD_PROTOCOL_I2C_SHORT))
	    || (p = ifd_sync_probe_icc(reader, slot, IFD_PROTOCOL_I2C_LONG))) {
		/* I2C card. Empty ATR */
		n = 0;
	} else if ((p = ifd_sync_probe_icc(reader, slot, IFD_PROTOCOL_2WIRE))
		   || (p =
		       ifd_sync_probe_icc(reader, slot, IFD_PROTOCOL_3WIRE))) {
		/* Try to read the ATR */
		if (ifd_deactivate(reader) < 0 || ifd_activate(reader) < 0)
			goto failed;
		n = ifd_protocol_read_memory(p, slot, 0, (unsigned char *)atr,
					     size);
		if (n < 0)
			goto failed;
	} else {
		goto failed;
	}

	reader->slot[slot].proto = p;

	ifd_debug(1, "Detected synchronous card (%s), %satr%s",
		  p->ops->name, n ? "" : "no ", ct_hexdump(atr, n));

	return n;

      failed:
	if (p != NULL)
		ifd_protocol_free(p);
	return IFD_ERROR_NO_ATR;
}

/*
 * Protocol structs
 */
struct ifd_protocol_ops ifd_protocol_i2c_short = {
	IFD_PROTOCOL_I2C_SHORT,	/* id */
	"I2C short",		/* name */
	sizeof(sync_state_t),	/* size */
	NULL,			/* init */
	sync_release,		/* release */
	NULL,			/* set_param */
	NULL,			/* get_param */
	NULL,			/* resynchronize */
	NULL,			/* transceive */
	sync_read,		/* sync_read */
	sync_write,		/* sync_write */
};

struct ifd_protocol_ops ifd_protocol_i2c_long = {
	IFD_PROTOCOL_I2C_LONG,	/* id */
	"I2C long",		/* name */
	sizeof(sync_state_t),	/* size */
	NULL,			/* init */
	sync_release,		/* release */
	NULL,			/* set_param */
	NULL,			/* get_param */
	NULL,			/* resynchronize */
	NULL,			/* transceive */
	sync_read,		/* sync_read */
	sync_write,		/* sync_write */
};

struct ifd_protocol_ops ifd_protocol_2wire = {
	IFD_PROTOCOL_2WIRE,	/* id */
	"2Wire",		/* name */
	sizeof(sync_state_t),	/* size */
	NULL,			/* init */
	sync_release,		/* release */
	NULL,			/* set_param */
	NULL,			/* get_param */
	NULL,			/* resynchronize */
	NULL,			/* transceive */
	sync_read,		/* sync_read */
	sync_write,		/* sync_write */
};

struct ifd_protocol_ops ifd_protocol_3wire = {
	IFD_PROTOCOL_3WIRE,	/* id */
	"3Wire",		/* name */
	sizeof(sync_state_t),	/* size */
	NULL,			/* init */
	sync_release,		/* release */
	NULL,			/* set_param */
	NULL,			/* get_param */
	NULL,			/* resynchronize */
	NULL,			/* transceive */
	sync_read,		/* sync_read */
	sync_write,		/* sync_write */
};

struct ifd_protocol_ops ifd_protocol_eurochip = {
	IFD_PROTOCOL_EUROCHIP,	/* id */
	"Eurochip Countercard",	/* name */
	sizeof(sync_state_t),	/* size */
	NULL,			/* init */
	sync_release,		/* release */
	NULL,			/* set_param */
	NULL,			/* get_param */
	NULL,			/* resynchronize */
	NULL,			/* transceive */
	sync_read,		/* sync_read */
	sync_write,		/* sync_write */
};
