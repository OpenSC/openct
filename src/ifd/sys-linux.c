/*
 * Linux specific functions
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 *
 * These functions need to be re-implemented for every
 * new platform.
 */

#include "internal.h"
#if defined (__linux__) && !defined (sunray)
#include <sys/types.h>
#include <linux/major.h>
#include <linux/compiler.h>
#include <linux/usbdevice_fs.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#ifdef HAVE_LIBUSB
#include <usb.h>
#endif
#include <openct/driver.h>

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

/*
 * Poll for presence of USB device
 */
int
ifd_sysdep_usb_poll_presence(ifd_device_t *dev, struct pollfd *pfd)
{
	if (pfd->revents & POLLHUP)
		return 0;
	pfd->fd = dev->fd;
	pfd->events = POLLHUP;
	return 1;
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
	struct usbdevfs_ctrltransfer ctrl;
	int		rc;

#ifdef LINUX_NEWUSB
	ctrl.bRequestType = requesttype;
	ctrl.bRequest = request;
	ctrl.wValue = value;
	ctrl.wIndex = index;
	ctrl.wLength = len;
	ctrl.data = data;
	ctrl.timeout = timeout;
#else
	ctrl.requesttype = requesttype;
	ctrl.request = request;
	ctrl.value = value;
	ctrl.index = index;
	ctrl.length = len;
	ctrl.data = data;
	ctrl.timeout = timeout;
#endif

	if ((rc = ioctl(dev->fd, USBDEVFS_CONTROL, &ctrl)) < 0) {
		ct_error("usb_control failed: %m");
		return IFD_ERROR_COMM_ERROR;
	}

	return rc;
}

int
ifd_sysdep_usb_set_configuration(ifd_device_t *dev, int config) 
{
	if (ioctl(dev->fd, USBDEVFS_SETCONFIGURATION, &config) < 0) {
		ct_error("usb_setconfig failed: %m");
		return IFD_ERROR_COMM_ERROR;
	}
	return 0;
}

int
ifd_sysdep_usb_set_interface(ifd_device_t *dev, int ifc, int alt) 
{
	struct usbdevfs_setinterface set;

	set.interface = ifc;
	set.altsetting = alt;
	if (ioctl(dev->fd, USBDEVFS_SETINTERFACE, &set) < 0) {
		ct_error("usb_setinterface failed: %m");
		return IFD_ERROR_COMM_ERROR;
	}
	return 0;
}

int
ifd_sysdep_usb_claim_interface(ifd_device_t *dev, int interface) 
{
	if (ioctl(dev->fd, USBDEVFS_CLAIMINTERFACE, &interface) < 0) {
		ct_error("usb_claiminterface failed: %m");
		return IFD_ERROR_COMM_ERROR;
	}
	return 0;
}

int
ifd_sysdep_usb_release_interface(ifd_device_t *dev, int interface) 
{
	if (ioctl(dev->fd, USBDEVFS_RELEASEINTERFACE, &interface) < 0) {
		ct_error("usb_releaseinterface failed: %m");
		return IFD_ERROR_COMM_ERROR;
	}
	return 0;
}

/*
 * USB bulk transfer
 */
int
ifd_sysdep_usb_bulk(ifd_device_t *dev, int ep, void *buffer, size_t len,
		       long timeout) 
{
	struct usbdevfs_bulktransfer bulk;
	int rc;

	bulk.ep = ep;
	bulk.data = buffer;
	bulk.len = len;
	bulk.timeout = timeout;
	if ((rc = ioctl(dev->fd, USBDEVFS_BULK, &bulk)) < 0) {
		ct_error("usb_bulk failed: %m");
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

	cap = (ifd_usb_capture_t *) calloc(1, sizeof(*cap) + maxpacket);

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
		usb_submit_urb(dev->fd, cap);
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
	free(cap);
	return rc;
}

int
ifd_sysdep_usb_open(char *device, int flags)
{
    return open(device, O_EXCL | O_RDWR);
}

/*
 * Scan all usb devices to see if there is one we support
 */
int
ifd_scan_usb(void)
{
#ifdef HAVE_LIBUSB
	ifd_devid_t	id;
	struct usb_bus	*bus;
	struct usb_device *dev;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	id.type = IFD_DEVICE_TYPE_USB;
	id.num  = 2;
	for (bus = usb_busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			const char	*driver;
			char		device[PATH_MAX];

			id.val[0] = dev->descriptor.idVendor;
			id.val[1] = dev->descriptor.idProduct;

			if (!(driver = ifd_driver_for_id(&id)))
				continue;

			snprintf(device, sizeof(device),
				"/proc/bus/usb/%s/%s",
				bus->dirname,
				dev->filename);

			ifd_spawn_handler(driver, device, -1);
		}
	}
#endif
	return 0;
}

#endif /* __linux__ */
