/*
 * Transparent protocol - simply pass everything to the reader driver
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"


/*
 * Attach t0 protocol
 */
static int
trans_init(ifd_protocol_t *prot)
{
	ifd_reader_t	*reader = prot->reader;
	const ifd_driver_t *drv;

	if (!reader || !(drv = reader->driver)
	 || !drv->ops || !drv->ops->transparent)
	 	return -1;
	return 0;
}

/*
 * Detach t0 protocol
 */
static void
trans_release(ifd_protocol_t *prot)
{
	/* NOP */
}

/*
 * Get/set parmaters for T1 protocol
 */
static int
trans_set_param(ifd_protocol_t *prot, int type, long value)
{
	ifd_error("set_pameter not supported");
	return -1;
}

static int
trans_get_param(ifd_protocol_t *prot, int type, long *result)
{
	ifd_error("get_pameter not supported");
	return -1;
}

/*
 * Transceive an APDU
 */
static int
trans_transceive(ifd_protocol_t *prot, ifd_apdu_t *apdu)
{
	ifd_reader_t	*reader = prot->reader;
	const ifd_driver_t *drv = reader->driver;

	return drv->ops->transparent(reader, prot->dad, apdu);
}

/*
 * Protocol struct
 */
struct ifd_protocol_ops	ifd_protocol_trans = {
	IFD_PROTOCOL_TRANSPARENT,
	"transparent",
	sizeof(ifd_protocol_t),
	trans_init,
	trans_release,
	trans_set_param,
	trans_get_param,
	trans_transceive,
};

