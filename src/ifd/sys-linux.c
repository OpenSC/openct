/*
 * Linux specific functions
 *
 */

#include <linux/major.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <string.h>
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

