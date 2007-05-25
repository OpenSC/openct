/*
 * Escape protocol - simply pass everything to the reader driver's escape()
 *
 * This is required for exporting access to vendor-specific CCID extensions,
 * such as the Omnikey CardMan 5121 RFID support.
 *
 * The higher-level applications select a virtual slot (the last available slot
 * number).  This virtual slot will automatically get the IFD_PROTOCOL_ESCAPE 
 * assgigned to it and can then be used to transceive() data to/from the CCID.
 *
 * It's a bit ugly, but I was unable to come up with something cleaner.
 *
 * Copyright (C) 2005, Harald Welte <laforge@gnumonks.org>
 */

#include "internal.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static int escape_init(ifd_protocol_t * prot)
{
	ifd_reader_t *reader = prot->reader;
	const ifd_driver_t *drv;

	if (!reader || !(drv = reader->driver)
	    || !drv->ops || !drv->ops->escape)
		return -1;
	return 0;
}

static void escape_release(ifd_protocol_t * prot)
{
	/* NOP */
}

static int escape_set_param(ifd_protocol_t * prot, int type, long value)
{
	ct_error("set_pameter not supported");
	return -1;
}

static int escape_get_param(ifd_protocol_t * prot, int type, long *result)
{
	ct_error("get_pameter not supported");
	return -1;
}

static int
escape_transceive(ifd_protocol_t * prot, int dad,
		  const void *sbuf, size_t slen, void *rbuf, size_t rlen)
{
	ifd_reader_t *reader = prot->reader;
	const ifd_driver_t *drv = reader->driver;

	return drv->ops->escape(reader, dad, sbuf, slen, rbuf, rlen);
}

struct ifd_protocol_ops ifd_protocol_esc = {
	IFD_PROTOCOL_ESCAPE,	/* id */
	"escape",		/* name */
	sizeof(ifd_protocol_t),	/* size */
	escape_init,		/* init */
	escape_release,		/* release */
	escape_set_param,	/* set_param */
	escape_get_param,	/* get_param */
	NULL,			/* resynchronize */
	escape_transceive,	/* transceive */
	NULL,			/* sync_read */
	NULL,			/* sync_write */
};
