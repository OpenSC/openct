/*
 * Protocol selection
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include "internal.h"

struct ifd_protocol_ops *
ifd_protocol_by_id(int id)
{
	/* First, check built-in protocols */
	switch (id) {
	case IFD_PROTOCOL_T0:
		return &ifd_protocol_t0;
	case IFD_PROTOCOL_T1:
		return &ifd_protocol_t1;
	}

	/* Check protocols registered dynamically */

	return NULL;
}
