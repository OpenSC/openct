/*
 * Linux specific functions
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 *
 * These functions need to be re-implemented for every
 * new platform.
 */

#include <sys/types.h>
#include <linux/major.h>
#include <linux/usbdevice_fs.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdio.h>
#include <usb.h>
#include "internal.h"

int
ifd_sysdep_device_type(const char *name)
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
ifd_sysdep_channel_to_name(unsigned int num)
{
	static char	namebuf[256];

	switch (num >> 24) {
	case 0:
		sprintf(namebuf, "/dev/ttyS%u", num);
		break;
	case 1:
		sprintf(namebuf, "/proc/bus/usb/%03d/%03d",
				(num >> 8) & 0xff,
				num & 0xff);
		break;
	default:
		ct_error("Unknown device channel 0x%x\n", num);
		return NULL;
	}

	return namebuf;
}

/*
 * USB control command
 */
int
ifd_sysdep_usb_control(int fd, ifd_usb_cmsg_t *cmsg, long timeout)
{
	struct usbdevfs_ctrltransfer ctrl;
	int	rc;

	ctrl.requesttype = cmsg->requesttype;
	ctrl.request = cmsg->request;
	ctrl.value = cmsg->value;
	ctrl.index = cmsg->index;
	ctrl.length = cmsg->len;
	ctrl.data = cmsg->data;
	ctrl.timeout = timeout;

	rc = ioctl(fd, USBDEVFS_CONTROL, &ctrl);
	if (rc < 0)
		ct_error("usb ioctl failed: %m");

	return rc;
}

/*
 * Scan all usb devices to see if there is one we support
 * This function is called at start-up because we can't be
 * sure the hotplug system notifies us of devices that were
 * attached at startup time already.
 */
int
ifd_sysdep_usb_scan(void)
{
	struct usb_bus	*bus;
	struct usb_device *dev;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	for (bus = usb_busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			char	idstring[64], device[PATH_MAX];

			snprintf(idstring, sizeof(idstring),
				"usb:%04x,%04x",
				dev->descriptor.idVendor,
				dev->descriptor.idProduct);

			snprintf(device, sizeof(device),
				"/proc/bus/usb/%s/%s",
				bus->dirname,
				dev->filename);

			ifd_hotplug_attach(device, idstring);
		}
	}

	return 0;
}
