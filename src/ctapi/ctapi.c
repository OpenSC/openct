/*
 * Generic CT-API functions
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include "ctapi.h"

int
ifd_reader_ctbcs(ifd_reader_t *reader, ifd_apdu_t *apdu)
{
	ifd_error("CTAPI CT commands not yet supported");
	return ERR_INVALID;
}
