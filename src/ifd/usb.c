/*
 * USB device handling
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "internal.h"

typedef struct ifd_usb {
	ifd_device_t	base;

	int		fd;
} ifd_usb_t;

/*
 * Send/receive USB control block
 */
static int
usb_control(ifd_device_t *dev, void *data, size_t len)
{
	ifd_usb_t	*usb = (ifd_usb_t *) dev;
	ifd_usb_cmsg_t	*cmsg;
	int		n;

	cmsg = (ifd_usb_cmsg_t *) data;
	if (len < sizeof(*cmsg) || cmsg->guard != IFD_DEVICE_TYPE_USB)
		return -1;

	if (ifd_config.debug >= 3) {
		ifd_debug("usb req type=x%02x req=x%02x len=%u",
				cmsg->requesttype,
				cmsg->request,
				cmsg->len);
		if (!(cmsg->requesttype & 0x80))
			ifd_debug("send %s", ifd_hexdump(cmsg->data, cmsg->len));
	}

	n = ifd_sysdep_usb_control(usb->fd, cmsg, 10000);

	if (ifd_config.debug >= 3) {
		if ((cmsg->requesttype & 0x80) && n >= 0)
			ifd_debug("recv %s", ifd_hexdump(cmsg->data, n));
	}

	return n;
}

static struct ifd_device_ops	ifd_usb_ops = {
	.control =	usb_control,
};

/*
 * Open USB device - used by CTAPI
 */
ifd_device_t *
ifd_open_usb(const char *device)
{
	ifd_usb_t	*dev;
	int		fd;

	if ((fd = open(device, O_EXCL | O_RDWR)) < 0) {
		ifd_error("Unable to open USB device %s: %m", device);
		return NULL;
	}

	dev = (ifd_usb_t *) ifd_device_new(device, &ifd_usb_ops, sizeof(*dev));
	dev->base.type = IFD_DEVICE_TYPE_USB;
	dev->fd = fd;

	return (ifd_device_t *) dev;
}

#include <usb.h>
#include <stdio.h>

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
