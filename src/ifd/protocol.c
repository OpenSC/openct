/*
 * Protocol selection
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include "internal.h"

ifd_protocol_t *
ifd_protocol_by_id(int id)
{
	/* First, check built-in protocols */
	switch (id) {
	case IFD_PROTOCOL_T1:
		return &ifd_protocol_t1;
	}

	/* Check protocols registered dynamically */

	return NULL;
}
