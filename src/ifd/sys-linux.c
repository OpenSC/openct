/*
 * Linux specific functions
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 *
 * These functions need to be re-implemented for every
 * new platform.
 */

#include <linux/major.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include "internal.h"

int
ifd_device_guess_type(const char *name)
{
	struct stat stb;

	if (!name || name[0] != '/')
		return -1;

	if (!strncmp(name, "/proc/bus/usb", 13))
		return IFD_DEVICE_TYPE_USB;

	if (stat(name, &stb) < 0)
		return -1;

	if (S_ISCHR(stb.st_mode)) {
		int major = major(stb.st_rdev);
		int minor = minor(stb.st_rdev);

		if (major == TTY_MAJOR
		 || major == PTY_SLAVE_MAJOR
		 || (UNIX98_PTY_SLAVE_MAJOR <= major
		  && major < UNIX98_PTY_SLAVE_MAJOR + UNIX98_PTY_MAJOR_COUNT))
			return IFD_DEVICE_TYPE_SERIAL;

		if (major == MISC_MAJOR && minor == 1)
			return IFD_DEVICE_TYPE_PS2;
	}

	return -1;
}

const char *
ifd_device_cannel_to_name(unsigned int num)
{
	static char	namebuf[256];

	switch (num >> 24) {
	case 0:
		sprintf(namebuf, "/dev/ttyS%u", num);
		break;
	case 1:
		sprintf(namebuf, "usb:id=%d,%d",
				(num >> 8) & 0xff,
				num & 0xff);
		break;
	default:
		ifd_error("Unknown device channel 0x%x\n", num);
		return NULL;
	}

	return namebuf;
}

