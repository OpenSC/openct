/*
 * libifd configuration handling
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <openct/conf.h>
#include <openct/config.h>
#include <openct/buffer.h>

struct ct_config	ct_config = {
	.debug		= 0,
	.autoload	= 1,
	.hotplug_scan_on_startup = 1,
	.modules_dir	= OPENCT_MODULES_PATH,
	.socket_dir	= OPENCT_SOCKET_PATH,
};

