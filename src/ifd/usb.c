/*
 * USB device handling
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <sys/poll.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "internal.h"

/*
 * Send/receive USB control block
 */
static int
usb_control(ifd_device_t *dev, void *data, size_t len)
{
	ifd_usb_cmsg_t	*cmsg;
	int		n;

	cmsg = (ifd_usb_cmsg_t *) data;
	if (len < sizeof(*cmsg) || cmsg->guard != IFD_DEVICE_TYPE_USB)
		return -1;

	if (ct_config.debug >= 3) {
		ifd_debug(4, "usb req type=x%02x req=x%02x len=%u",
				cmsg->requesttype,
				cmsg->request,
				cmsg->len);
		if (!(cmsg->requesttype & 0x80) && cmsg->len)
			ifd_debug(4, "send %s", ct_hexdump(cmsg->data, cmsg->len));
	}

	n = ifd_sysdep_usb_control(dev->fd, cmsg, 10000);

	if (ct_config.debug >= 3) {
		if ((cmsg->requesttype & 0x80) && n >= 0)
			ifd_debug(4, "recv %s", ct_hexdump(cmsg->data, n));
	}

	return n;
}

/*
 * Check presence of USB device
 * XXX: make this a system dependent function?
 */
static int
usb_poll_presence(ifd_device_t *dev, struct pollfd *p)
{
	if (p->revents & POLLHUP)
		return 0;

	p->fd = dev->fd;
	p->events = POLLHUP;
	return 1;
}

static struct ifd_device_ops	ifd_usb_ops = {
	.control	= usb_control,
	.poll_presence	= usb_poll_presence,
};

/*
 * Open USB device - used by CTAPI
 */
ifd_device_t *
ifd_open_usb(const char *device)
{
	ifd_device_t	*dev;
	int		fd;

	if ((fd = open(device, O_EXCL | O_RDWR)) < 0) {
		ct_error("Unable to open USB device %s: %m", device);
		return NULL;
	}

	dev = ifd_device_new(device, &ifd_usb_ops, sizeof(*dev));
	dev->type = IFD_DEVICE_TYPE_USB;
	dev->fd = fd;

	return dev;
}

