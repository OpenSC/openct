/*
 * *BSD specific functions
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 * Copyright (C) 2003 Andreas Jellinghaus <aj@dungeon.inka.de>
 * Copyright (C) 2003 Markus Friedl <markus@openbsd.org>
 *
 * These functions need to be re-implemented for every
 * new platform.
 */

#include "internal.h"
#if defined(__NetBSD__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <sys/types.h>
#include </usr/src/sys/dev/usb/usb.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <openct/driver.h>

int
ifd_sysdep_device_type(const char *name)
{
	struct stat stb;

	if (!name || name[0] != '/')
		return -1;

	if (!strncmp(name, "/dev/ugen", 7))
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

/*
 * USB control command
 */
int
ifd_sysdep_usb_control(ifd_device_t *dev,
		unsigned int requesttype,
		unsigned int request,
		unsigned int value,
		unsigned int index,
		void *data, size_t len, long timeout)
{
	struct usb_ctl_request ctrl;
	int		rc,val;

	memset(&ctrl, 0, sizeof(ctrl));
	
	ctrl.ucr_addr = 0;
	ctrl.ucr_request.bmRequestType = requesttype;
	ctrl.ucr_request.bRequest = request;
	USETW(ctrl.ucr_request.wValue, value);
	USETW(ctrl.ucr_request.wIndex, index);
	USETW(ctrl.ucr_request.wLength, len);
	ctrl.ucr_actlen = 0;
	ctrl.ucr_data = data;
	ctrl.ucr_flags = USBD_SHORT_XFER_OK;

	val = timeout;
	rc = ioctl(dev->fd, USB_SET_TIMEOUT, &val);
	if (rc < 0) {
		ct_error("usb_set_timeout failed: %m");
                return IFD_ERROR_COMM_ERROR;
	}
 
	if ((rc = ioctl(dev->fd, USB_DO_REQUEST, &ctrl)) < 0) {
                ct_error("usb_control failed: %m");
                return IFD_ERROR_COMM_ERROR;
        }

	return rc;
}

/*
 * USB URB capture
 */
struct ifd_usb_capture {
	struct usbdevfs_urb urb;
	int		type;
	int		endpoint;
	size_t		maxpacket;
	unsigned int	interface;
};

static int
usb_submit_urb(int fd, struct ifd_usb_capture *cap)
{
	/* Fill in the URB details */
	ifd_debug(6, "submit urb %p", &cap->urb);
	memset(&cap->urb, 0, sizeof(cap->urb));
	cap->urb.type = cap->type;
	cap->urb.endpoint = cap->endpoint;
	cap->urb.buffer = (caddr_t) (cap + 1);
	cap->urb.buffer_length = cap->maxpacket;
	return ioctl(fd, USBDEVFS_SUBMITURB, &cap->urb);
}

int
ifd_sysdep_usb_begin_capture(ifd_device_t *dev,
		int type, int endpoint, size_t maxpacket,
	       	ifd_usb_capture_t **capret)
{
	ifd_usb_capture_t	*cap;
	int			rc = 0;

	cap = calloc(1, sizeof(*cap) + maxpacket);

	/* Assume the interface # is 0 */
	cap->interface = 0;
	rc = ioctl(dev->fd, USBDEVFS_CLAIMINTERFACE, &cap->interface);
	if (rc < 0) {
		ct_error("usb_claiminterface failed: %m");
		free(cap);
		return IFD_ERROR_COMM_ERROR;
	}

	cap->type = type;
	cap->endpoint = endpoint;
	cap->maxpacket = maxpacket;

	if (usb_submit_urb(dev->fd, cap) < 0) {
		ct_error("usb_submiturb failed: %m");
		ifd_sysdep_usb_end_capture(dev, cap);
		return IFD_ERROR_COMM_ERROR;
	}

	*capret = cap;
	return 0;
}

int
ifd_sysdep_usb_capture(ifd_device_t *dev,
		ifd_usb_capture_t *cap,
		void *buffer, size_t len,
		long timeout)
{
	struct usbdevfs_urb	*purb;
	struct timeval		begin;
	size_t			copied;
	int			rc = 0;

	/* Loop until we've reaped the response to the
	 * URB we sent */
	copied = 0;
	gettimeofday(&begin, NULL);
	do {
		struct pollfd	pfd;
		long		wait;

		if ((wait = timeout - ifd_time_elapsed(&begin)) <= 0)
			return IFD_ERROR_TIMEOUT;

		pfd.fd = dev->fd;
		pfd.events = POLLOUT;
		if (poll(&pfd, 1, wait) != 1)
			continue;

		purb = NULL;
		rc = ioctl(dev->fd, USBDEVFS_REAPURBNDELAY, &purb);
		if (rc < 0) {
			if (errno == EAGAIN)
				continue;
			ct_error("usb_reapurb failed: %m");
			return IFD_ERROR_COMM_ERROR;
		}

		if (purb != &cap->urb) {
			ifd_debug(2, "reaped usb urb %p", purb);
			continue;
		}

		if (purb->actual_length) {
			ifd_debug(6, "usb reapurb: len=%u", purb->actual_length);
			if ((copied = purb->actual_length) > len)
				copied = len;
			if (copied && buffer)
				memcpy(buffer, purb->buffer, copied);
		} else {
			usleep(10000);
		}

		/* Re-submit URB */
		usb_submit_urb(fd, cap);
	} while (!copied);

	return copied;
}

int
ifd_sysdep_usb_end_capture(ifd_device_t *dev, ifd_usb_capture_t *cap)
{
	int	rc = 0;

	if (ioctl(dev->fd, USBDEVFS_DISCARDURB, &cap->urb) < 0 && errno != EINVAL) {
		ct_error("usb_discardurb failed: %m");
		rc = IFD_ERROR_COMM_ERROR;
	}
	/* Discarding an URB will place it in the queue of completed
	 * request, with urb->status == -1. So if we don't reap this
	 * URB now, the next call to REAPURB will return this one,
	 * clobbering random memory.
	 */
	(void) ioctl(dev->fd, USBDEVFS_REAPURBNDELAY, &cap->urb);
	if (ioctl(dev->fd, USBDEVFS_RELEASEINTERFACE, &cap->interface) < 0) {
		ct_error("usb_releaseinterface failed: %m");
		rc = IFD_ERROR_COMM_ERROR;
	}
	free(cap);
	return rc;
}

/*
 * Scan all usb devices to see if there is one we support
 */
int
ifd_scan_usb(void)
{
	return 0;
}

#endif /* __Net/Free/OpenBSD__ */
