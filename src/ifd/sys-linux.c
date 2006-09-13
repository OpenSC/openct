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
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#ifdef HAVE_LIBUSB
#include <usb.h>
#endif
#include <openct/driver.h>

/* imported from linux kernel header include/linux/usbdevice_fs.h */

#define USBDEVICE_SUPER_MAGIC 0x9fa2

/* usbdevfs ioctl codes */

struct usbdevfs_ctrltransfer {
	uint8_t bRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
	uint32_t timeout;  /* in milliseconds */
 	void *data;
};

struct usbdevfs_bulktransfer {
	unsigned int ep;
	unsigned int len;
	unsigned int timeout; /* in milliseconds */
	void *data;
};

struct usbdevfs_setinterface {
	unsigned int interface;
	unsigned int altsetting;
};

struct usbdevfs_disconnectsignal {
	unsigned int signr;
	void *context;
};

#define USBDEVFS_MAXDRIVERNAME 255

struct usbdevfs_getdriver {
	unsigned int interface;
	char driver[USBDEVFS_MAXDRIVERNAME + 1];
};

struct usbdevfs_connectinfo {
	unsigned int devnum;
	unsigned char slow;
};

#define USBDEVFS_URB_SHORT_NOT_OK          1
#define USBDEVFS_URB_ISO_ASAP              2

#define USBDEVFS_URB_TYPE_ISO		   0
#define USBDEVFS_URB_TYPE_INTERRUPT	   1
#define USBDEVFS_URB_TYPE_CONTROL	   2
#define USBDEVFS_URB_TYPE_BULK		   3

struct usbdevfs_iso_packet_desc {
	unsigned int length;
	unsigned int actual_length;
	unsigned int status;
};

struct usbdevfs_urb {
	unsigned char type;
	unsigned char endpoint;
	int status;
	unsigned int flags;
	void *buffer;
	int buffer_length;
	int actual_length;
	int start_frame;
	int number_of_packets;
	int error_count;
	unsigned int signr;  /* signal to be sent on error, -1 if none should be sent */
	void *usercontext;
	struct usbdevfs_iso_packet_desc iso_frame_desc[0];
};

/* ioctls for talking directly to drivers */
struct usbdevfs_ioctl {
	int	ifno;		/* interface 0..N ; negative numbers reserved */
	int	ioctl_code;	/* MUST encode size + direction of data so the
				 * macros in <asm/ioctl.h> give correct values */
	void *data;	/* param buffer (in, or out) */
};

/* You can do most things with hubs just through control messages,
 * except find out what device connects to what port. */
struct usbdevfs_hub_portinfo {
	char nports;		/* number of downstream ports in this hub */
	char port [127];	/* e.g. port 3 connects to device 27 */
};

#define USBDEVFS_CONTROL           _IOWR('U', 0, struct usbdevfs_ctrltransfer)
#define USBDEVFS_BULK              _IOWR('U', 2, struct usbdevfs_bulktransfer)
#define USBDEVFS_RESETEP           _IOR('U', 3, unsigned int)
#define USBDEVFS_SETINTERFACE      _IOR('U', 4, struct usbdevfs_setinterface)
#define USBDEVFS_SETCONFIGURATION  _IOR('U', 5, unsigned int)
#define USBDEVFS_GETDRIVER         _IOW('U', 8, struct usbdevfs_getdriver)
#define USBDEVFS_SUBMITURB         _IOR('U', 10, struct usbdevfs_urb)
#define USBDEVFS_DISCARDURB        _IO('U', 11)
#define USBDEVFS_REAPURB           _IOW('U', 12, void *)
#define USBDEVFS_REAPURBNDELAY     _IOW('U', 13, void *)
#define USBDEVFS_DISCSIGNAL        _IOR('U', 14, struct usbdevfs_disconnectsignal)
#define USBDEVFS_CLAIMINTERFACE    _IOR('U', 15, unsigned int)
#define USBDEVFS_RELEASEINTERFACE  _IOR('U', 16, unsigned int)
#define USBDEVFS_CONNECTINFO       _IOW('U', 17, struct usbdevfs_connectinfo)
#define USBDEVFS_IOCTL             _IOWR('U', 18, struct usbdevfs_ioctl)
#define USBDEVFS_HUB_PORTINFO      _IOR('U', 19, struct usbdevfs_hub_portinfo)
#define USBDEVFS_RESET             _IO('U', 20)
#define USBDEVFS_CLEAR_HALT        _IOR('U', 21, unsigned int)
#define USBDEVFS_DISCONNECT        _IO('U', 22)
#define USBDEVFS_CONNECT           _IO('U', 23)

/* end of import from usbdevice_fs.h */

/*
 * Poll for presence of USB device
 */
int ifd_sysdep_usb_poll_presence(ifd_device_t * dev, struct pollfd *pfd)
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
int ifd_sysdep_usb_control(ifd_device_t * dev, unsigned int requesttype,
			   unsigned int request, unsigned int value,
			   unsigned int index, void *data, size_t len,
			   long timeout)
{
	struct usbdevfs_ctrltransfer ctrl;
	int rc;

	ctrl.bRequestType = requesttype;
	ctrl.bRequest = request;
	ctrl.wValue = value;
	ctrl.wIndex = index;
	ctrl.wLength = len;
	ctrl.data = data;
	ctrl.timeout = timeout;

	if ((rc = ioctl(dev->fd, USBDEVFS_CONTROL, &ctrl)) < 0) {
		ct_error("usb_control failed: %m");
		return IFD_ERROR_COMM_ERROR;
	}

	return rc;
}

int ifd_sysdep_usb_set_configuration(ifd_device_t * dev, int config)
{
	if (ioctl(dev->fd, USBDEVFS_SETCONFIGURATION, &config) < 0) {
		ct_error("usb_setconfig failed: %m");
		return IFD_ERROR_COMM_ERROR;
	}
	return 0;
}

int ifd_sysdep_usb_set_interface(ifd_device_t * dev, int ifc, int alt)
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

int ifd_sysdep_usb_claim_interface(ifd_device_t * dev, int interface)
{
	if (ioctl(dev->fd, USBDEVFS_CLAIMINTERFACE, &interface) < 0) {
		ct_error("usb_claiminterface failed: %m");
		return IFD_ERROR_COMM_ERROR;
	}
	return 0;
}

int ifd_sysdep_usb_release_interface(ifd_device_t * dev, int interface)
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
int ifd_sysdep_usb_bulk(ifd_device_t * dev, int ep, void *buffer, size_t len,
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
	int type;
	int endpoint;
	size_t maxpacket;
};

static int usb_submit_urb(int fd, struct ifd_usb_capture *cap)
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

int ifd_sysdep_usb_begin_capture(ifd_device_t * dev, int type, int endpoint,
				 size_t maxpacket, ifd_usb_capture_t ** capret)
{
	ifd_usb_capture_t *cap;

	cap = (ifd_usb_capture_t *) calloc(1, sizeof(*cap) + maxpacket);
	if (!cap) {
		ct_error("out of memory");
		return IFD_ERROR_NO_MEMORY;
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

int ifd_sysdep_usb_capture(ifd_device_t * dev, ifd_usb_capture_t * cap,
			   void *buffer, size_t len, long timeout)
{
	struct usbdevfs_urb *purb;
	struct timeval begin;
	size_t copied;
	int rc = 0;

	/* Loop until we've reaped the response to the
	 * URB we sent */
	copied = 0;
	gettimeofday(&begin, NULL);
	do {
		struct pollfd pfd;
		long wait;

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
			ifd_debug(6, "usb reapurb: len=%u",
				  purb->actual_length);
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

int ifd_sysdep_usb_end_capture(ifd_device_t * dev, ifd_usb_capture_t * cap)
{
	int rc = 0;

	if (ioctl(dev->fd, USBDEVFS_DISCARDURB, &cap->urb) < 0
	    && errno != EINVAL) {
		ct_error("usb_discardurb failed: %m");
		rc = IFD_ERROR_COMM_ERROR;
	}
	/* Discarding an URB will place it in the queue of completed
	 * request, with urb->status == -1. So if we don't reap this
	 * URB now, the next call to REAPURB will return this one,
	 * clobbering random memory.
	 */
	(void)ioctl(dev->fd, USBDEVFS_REAPURBNDELAY, &cap->urb);
	free(cap);
	return rc;
}

int ifd_sysdep_usb_open(const char *device)
{
        return open(device, O_RDWR);
}

/*
 * Scan all usb devices to see if there is one we support
 */
int ifd_scan_usb(void)
{
#ifdef HAVE_LIBUSB
	ifd_devid_t id;
	struct usb_bus *bus;
	struct usb_device *dev;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	id.type = IFD_DEVICE_TYPE_USB;
	id.num = 2;
	for (bus = usb_busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			const char *driver;
			char typedev[PATH_MAX];
			struct stat buf;

			id.val[0] = dev->descriptor.idVendor;
			id.val[1] = dev->descriptor.idProduct;

			if (!(driver = ifd_driver_for_id(&id)))
				continue;

			snprintf(typedev, sizeof(typedev),
				 "/dev/bus/usb/%s/%s",
				 bus->dirname, dev->filename);
			if (stat(typedev, &buf) == 0) {
				snprintf(typedev, sizeof(typedev),
				 	"usb:/dev/bus/usb/%s/%s",
				 	bus->dirname, dev->filename);
				ifd_spawn_handler(driver, typedev, -1);
			} else {
				snprintf(typedev, sizeof(typedev),
				 	"usb:/proc/bus/usb/%s/%s",
				 	bus->dirname, dev->filename);
				ifd_spawn_handler(driver, typedev, -1);
			}
		}
	}
#endif
	return 0;
}

#endif				/* __linux__ */
