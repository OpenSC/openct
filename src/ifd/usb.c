/*
 * USB device handling
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <sys/poll.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

/*
 * Send/receive USB control block
 */
int
ifd_usb_control(ifd_device_t *dev,
		unsigned int requesttype,
		unsigned int request,
		unsigned int value,
		unsigned int index,
		void *buffer, size_t len,
		long timeout)
{
	int		n;

	if (dev->type != IFD_DEVICE_TYPE_USB)
		return -1;
	if (timeout < 0)
		timeout = 10000;

	if ((ct_config.debug >= 3) && !(requesttype & 0x80)) {
		ifd_debug(4, "usb req type=x%02x req=x%02x val=x%04x ind=x%04x len=%u",
				requesttype,
				request,
				value,
				index,
				len);
		if (len)
			ifd_debug(4, "send %s", ct_hexdump(buffer, len));
	}

	n = ifd_sysdep_usb_control(dev, requesttype, request, value, index,
				buffer, len, timeout);

	if ((ct_config.debug >= 3) && (requesttype & 0x80)) {
		ifd_debug(4, "usb req type=x%02x req=x%02x val=x%04x ind=x%04x len=%d",
				requesttype,
				request,
				value,
				index,
				n);
		if (n > 0)
			ifd_debug(4, "recv %s", ct_hexdump(buffer, n));
	}

	return n;
}

/*
 * USB frame capture
 */
int
ifd_usb_begin_capture(ifd_device_t *dev, int type, int endpoint,
			size_t maxpacket, ifd_usb_capture_t **capret)
{
	if (dev->type != IFD_DEVICE_TYPE_USB)
		return -1;

	if (ct_config.debug >= 5)
		ifd_debug(5, "usb capture type=%d ep=x%x maxpacket=%u",
				type, endpoint, maxpacket);
	return ifd_sysdep_usb_begin_capture(dev, type, endpoint, maxpacket, capret);
}

int
ifd_usb_capture(ifd_device_t *dev, ifd_usb_capture_t *cap,
	       	void *buffer, size_t len, long timeout)
{
	int	rc;

	if (dev->type != IFD_DEVICE_TYPE_USB)
		return -1;

	ifd_debug(5, "called, timeout=%ld ms.", timeout);
	rc = ifd_sysdep_usb_capture(dev, cap, buffer, len, timeout);
	if (ct_config.debug >= 3) {
		if (rc < 0)
			ifd_debug(1, "usb capture: %s", ct_strerror(rc));
		if (rc > 0)
			ifd_debug(5, "recv %s", ct_hexdump(buffer, rc));
	}
	return rc;
}

int
ifd_usb_end_capture(ifd_device_t *dev, ifd_usb_capture_t *cap)
{
	if (dev->type != IFD_DEVICE_TYPE_USB)
		return -1;
	return ifd_sysdep_usb_end_capture(dev, cap);
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

static struct ifd_device_ops	ifd_usb_ops;

/*
 * Open USB device
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

	ifd_usb_ops.poll_presence = usb_poll_presence;

	dev = ifd_device_new(device, &ifd_usb_ops, sizeof(*dev));
	dev->type = IFD_DEVICE_TYPE_USB;
	dev->timeout = 10000;
	dev->fd = fd;

	return dev;
}
