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
	ifd_protocol_t	base;
	unsigned int	mem_size;
} sync_state_t;

/*
 * Attach synchronous protocol
 */
static int
sync_init(ifd_protocol_t *prot)
{
	sync_state_t	*st = (sync_state_t *) prot;

	st->mem_size = -1;
	return 0;
}

/*
 * Detach protocol
 */
static void
sync_release(ifd_protocol_t *prot)
{
	/* NOP */
}

/*
 * Read/write operations
 */
static int
sync_read(ifd_protocol_t *prot, int slot,
			unsigned short addr,
			unsigned char *rbuf, size_t rlen)
{
	sync_state_t	*st = (sync_state_t *) prot;
	ifd_reader_t	*reader = prot->reader;
	const ifd_driver_t *drv;

	if (!(drv = reader->driver) || !drv->ops || !drv->ops->sync_read)
		return IFD_ERROR_NOT_SUPPORTED;

	if (addr > st->mem_size)
		return IFD_ERROR_INVALID_ARG;
	if (rlen > st->mem_size - addr)
		rlen = st->mem_size - addr;

	return drv->ops->sync_read(reader, slot, prot->ops->id,
			addr, rbuf, rlen);
}

static int
sync_write(ifd_protocol_t *prot, int slot,
			unsigned short addr,
			const unsigned char *sbuf, size_t slen)
{
	sync_state_t	*st = (sync_state_t *) prot;
	ifd_reader_t	*reader = prot->reader;
	const ifd_driver_t *drv;
	unsigned int	retries = 1;
	int		prot_id;

	if (!(drv = reader->driver) || !drv->ops || !drv->ops->sync_read)
		return IFD_ERROR_NOT_SUPPORTED;

	prot_id = prot->ops->id;
	if ((prot_id == IFD_PROTOCOL_I2C_SHORT
	   || prot_id == IFD_PROTOCOL_I2C_LONG) && slen > 1)
		retries = 2;

	if (addr > st->mem_size)
		return IFD_ERROR_INVALID_ARG;
	if (slen > st->mem_size - addr)
		slen = st->mem_size - addr;

	while (slen) {
		unsigned char	temp[256];
		size_t		count;
		int		r;

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
static ifd_protocol_t *
ifd_sync_probe_icc(ifd_reader_t *reader, int slot, int proto)
{
	ifd_protocol_t	*p;
	unsigned char	byte;
	int		res = 0;

	if (ifd_deactivate(reader) < 0
	 || ifd_activate(reader) < 0)
		return 0;

	if (!(p = ifd_protocol_new(proto, reader, slot)))
		return 0;

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

out:	if (!res) {
		ifd_protocol_free(p);
		p = NULL;
	}
	return p;
}

/*
 * Detect I2C memory length
 */
int
ifd_sync_probe_memory_size(ifd_protocol_t *p, int slot)
{
	sync_state_t	*st = (sync_state_t *) p;
	unsigned int	prot_id, length;

	prot_id = p->ops->id;
	if (prot_id == IFD_PROTOCOL_2WIRE) {
		st->mem_size = 256;
		return 0;
	} else
	if (prot_id == IFD_PROTOCOL_3WIRE) {
		st->mem_size = 1024;
		return 0;
	}

	/* Probe memory length. */
	if (prot_id == IFD_PROTOCOL_I2C_SHORT) {
		length = 4096;
	} else {
		length = 8192;
	}

	while (length > 0) {
		unsigned int	address = length - 1;
		unsigned char	byte;
		int		r;

		if ((r = sync_read(p, slot, address, &byte, 1)) < 0)
			return r;
		byte = ~byte;

		if ((r = sync_write(p, slot, address, &byte, 1)) < 0) {
			length /= 2;
		} else {
			byte = ~byte;
			r = sync_write(p, slot, address, &byte, 1);
			if (r < 0)
				return r;
			break;
		}
	}

	st->mem_size = length;
	return 0;
}

unsigned int
ifd_sync_memory_length(ifd_protocol_t *p)
{
	return ((sync_state_t *) p)->mem_size;
}

/*
 * Detect synchronous ICC
 */
int
ifd_sync_detect_icc(ifd_reader_t *reader, int slot, void *atr, size_t size)
{
	ifd_protocol_t	*p = NULL;
	int		n;

	if ((p = ifd_sync_probe_icc(reader, slot, IFD_PROTOCOL_I2C_SHORT))
	 || (p = ifd_sync_probe_icc(reader, slot, IFD_PROTOCOL_I2C_LONG))) {
		/* I2C card. Empty ATR */
		n = 0;
	} else
	if ((p = ifd_sync_probe_icc(reader, slot, IFD_PROTOCOL_2WIRE))
	 || (p = ifd_sync_probe_icc(reader, slot, IFD_PROTOCOL_3WIRE))) {
		/* Try to read the ATR */
		if (ifd_deactivate(reader) < 0
		 || ifd_activate(reader) < 0)
			goto failed;
		n = ifd_protocol_read_memory(p, slot, 0, atr, size);
		if (n < 0)
			goto failed;
	} else {
	 	goto failed;
	}


	/* Probe memory length */
	if (ifd_sync_probe_memory_size(p, slot))
		goto failed;

	reader->slot[slot].proto = p;

	ifd_debug(1, "Detected synchronous card (%s), size=%u, %satr%s",
			p->ops->name,
			ifd_sync_memory_length(p),
			n? "" : "no ",
			ct_hexdump(atr, n));

	return n;

failed:	if (p != NULL)
		ifd_protocol_free(p);
	return IFD_ERROR_NO_ATR;
}

/*
 * Protocol structs
 */
struct ifd_protocol_ops	ifd_protocol_i2c_short = {
	IFD_PROTOCOL_I2C_SHORT,		/* id */
	"I2C short",			/* name */
	sizeof(sync_state_t),		/* size */
	sync_init,			/* init */
	sync_release,			/* release */
	NULL,				/* set_param */
	NULL,				/* get_param */
	NULL,				/* resynchronize */
	NULL,				/* transceive */
	sync_read,			/* sync_read */
	sync_write,			/* sync_write */
};

struct ifd_protocol_ops	ifd_protocol_i2c_long = {
	IFD_PROTOCOL_I2C_LONG,		/* id */
	"I2C long",			/* name */
	sizeof(sync_state_t),		/* size */
	sync_init,			/* init */
	sync_release,			/* release */
	NULL,				/* set_param */
	NULL,				/* get_param */
	NULL,				/* resynchronize */
	NULL,				/* transceive */
	sync_read,			/* sync_read */
	sync_write,			/* sync_write */
};

struct ifd_protocol_ops	ifd_protocol_2wire = {
	IFD_PROTOCOL_2WIRE,		/* id */
	"2Wire",			/* name */
	sizeof(sync_state_t),		/* size */
	sync_init,			/* init */
	sync_release,			/* release */
	NULL,				/* set_param */
	NULL,				/* get_param */
	NULL,				/* resynchronize */
	NULL,				/* transceive */
	sync_read,			/* sync_read */
	sync_write,			/* sync_write */
};

struct ifd_protocol_ops	ifd_protocol_3wire = {
	IFD_PROTOCOL_3WIRE,		/* id */
	"3Wire",			/* name */
	sizeof(sync_state_t),		/* size */
	sync_init,			/* init */
	sync_release,			/* release */
	NULL,				/* set_param */
	NULL,				/* get_param */
	NULL,				/* resynchronize */
	NULL,				/* transceive */
	sync_read,			/* sync_read */
	sync_write,			/* sync_write */
};
