/*
 * Transparent protocol - simply pass everything to the reader driver
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/*
 * Attach t0 protocol
 */
static int trans_init(ifd_protocol_t * prot)
{
	ifd_reader_t *reader = prot->reader;
	const ifd_driver_t *drv;

	if (!reader || !(drv = reader->driver)
	    || !drv->ops || !drv->ops->transparent)
		return -1;
	return 0;
}

/*
 * Detach t0 protocol
 */
static void trans_release(ifd_protocol_t * prot)
{
	/* NOP */
}

/*
 * Get/set parmaters for T1 protocol
 */
static int trans_set_param(ifd_protocol_t * prot, int type, long value)
{
	ct_error("set_pameter not supported");
	return -1;
}

static int trans_get_param(ifd_protocol_t * prot, int type, long *result)
{
	ct_error("get_pameter not supported");
	return -1;
}

/*
 * Transceive an APDU
 */
static int trans_transceive(ifd_protocol_t * prot, int dad, const void *sbuf,
			    size_t slen, void *rbuf, size_t rlen)
{
	ifd_reader_t *reader = prot->reader;
	const ifd_driver_t *drv = reader->driver;

	return drv->ops->transparent(reader, dad, sbuf, slen, rbuf, rlen);
}

/*
 * Protocol struct
 */
struct ifd_protocol_ops ifd_protocol_trans = {
	IFD_PROTOCOL_TRANSPARENT,	/* id */
	"transparent",		/* name */
	sizeof(ifd_protocol_t),	/* size */
	trans_init,		/* init */
	trans_release,		/* release */
	trans_set_param,	/* set_param */
	trans_get_param,	/* get_param */
	NULL,			/* resynchronize */
	trans_transceive,	/* transceive */
	NULL,			/* sync_read */
	NULL,			/* sync_write */
};
