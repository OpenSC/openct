/*
 * libifd configuration handling
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"

struct ifd_config	ifd_config = {
	.autoload	= 1,
	.debug		= 0,
};
