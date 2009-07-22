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
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#ifdef ENABLE_LIBUSB
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

#define USB_DISCONNECT_SIGNAL (SIGRTMIN)

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
 * Event fd to use.
 */
int ifd_sysdep_usb_get_eventfd(ifd_device_t * dev, short *events)
{
	*events = POLLOUT;
	return dev->fd;
}

/*
 * USB control command
 */
int ifd_sysdep_usb_control(ifd_device_t * dev, unsigned int requesttype,
			   unsigned int request, unsigned int value,
			   unsigned int idx, void *data, size_t len,
			   long timeout)
{
	struct usbdevfs_ctrltransfer ctrl;
	int rc;

	ctrl.bRequestType = requesttype;
	ctrl.bRequest = request;
	ctrl.wValue = value;
	ctrl.wIndex = idx;
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

int ifd_sysdep_usb_reset(ifd_device_t * dev)
{
	if (ioctl(dev->fd, USBDEVFS_RESET, NULL) < 0) {
		ct_error("usb_reset failed: %m");
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

int ifd_sysdep_usb_capture_event(ifd_device_t * dev, ifd_usb_capture_t * cap,
			   void *buffer, size_t len)
{
	struct usbdevfs_urb *purb;
	size_t copied = 0;
	int rc = 0;

	purb = NULL;
	rc = ioctl(dev->fd, USBDEVFS_REAPURBNDELAY, &purb);
	if (rc < 0) {
		if (errno == EAGAIN)
			return 0;
		ct_error("usb_reapurb failed: %m");
		return IFD_ERROR_COMM_ERROR;
	}

	if (purb != &cap->urb) {
		ifd_debug(2, "reaped usb urb %p", purb);
		return 0;
	}

	if (purb->status == -1) {
		return IFD_ERROR_COMM_ERROR;
	}

	if (purb->actual_length) {
		ifd_debug(6, "usb reapurb: len=%u",
			  purb->actual_length);
		if ((copied = purb->actual_length) > len)
			copied = len;
		if (copied && buffer)
			memcpy(buffer, purb->buffer, copied);
	}
	else {
		usleep(10000);
	}

	/* Re-submit URB */
	usb_submit_urb(dev->fd, cap);

	return copied;
}

int ifd_sysdep_usb_capture(ifd_device_t * dev, ifd_usb_capture_t * cap,
			   void *buffer, size_t len, long timeout)
{
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

		rc = ifd_sysdep_usb_capture_event(dev, cap, buffer, len);
		if (rc < 0) {
			return rc;
		}
		copied = (size_t)rc;
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
	struct usbdevfs_disconnectsignal ds;
	struct sigaction act;
	int fd = -1;
	int ret = -1;

	fd = open(device, O_RDWR);
	if (fd == -1) {
		goto cleanup;
	}

	/*
	 * The following will send signal
	 * to the process when device is disconnected
	 * even if the signal is ignored the blocking
	 * call will exit.
	 * <linux.2.6.28 - This code will have no affect.
	 * =linux-2.6.28 - CONFIG_USB_DEVICEFS must be on.
	 * >=linux-2.6.29 - works.
	 */
	memset(&act, 0, sizeof(act));
	act.sa_handler = SIG_IGN;
	if (sigaction(USB_DISCONNECT_SIGNAL, &act, NULL) == -1) {
		goto cleanup;
	}

	memset(&ds, 0, sizeof(ds));
	ds.signr = USB_DISCONNECT_SIGNAL;
	if (ioctl(fd, USBDEVFS_DISCSIGNAL, &ds) == -1) {
		goto cleanup;
	}

	ret = fd;
	fd = -1;

cleanup:
	if (fd != -1) {
		close(fd);
	}

	return ret;
}

#ifndef ENABLE_LIBUSB
static int read_number (const char *read_format, const char *format, ...) {
	va_list args;
	char full[PATH_MAX];
	FILE *fp = NULL;
	int n = -1;

	va_start(args, format);
	vsnprintf (full, sizeof(full), format, args);
	va_end(args);

	if ((fp = fopen (full, "r")) == NULL) {
		goto out;
	}

	fscanf (fp, read_format, &n);

out:
	if (fp != NULL) {
		fclose (fp);
	}

	return n;
}
#endif

/*
 * Scan all usb devices to see if there is one we support
 */
int ifd_scan_usb(void)
{
#ifdef ENABLE_LIBUSB
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
			const char *driver = NULL;
			char typedev[PATH_MAX];
			struct stat buf;

			id.val[0] = dev->descriptor.idVendor;
			id.val[1] = dev->descriptor.idProduct;

			/* if we don't find a driver with vendor/product
 			 * then check for the interface type (ccid) and use
 			 * driver ccid... */

			if (!(driver = ifd_driver_for_id(&id))) {
				/* no driver found, check for interface class */
				int conf;
 				for (conf = 0; conf < dev->descriptor.bNumConfigurations;
					conf++) {
					int interf;
					for (interf = 0; interf < dev->config[conf].bNumInterfaces;
						interf++) {
						int alt;
						for (alt = 0; alt < dev->config[conf].interface[interf].num_altsetting; alt++) {
							if (dev->config[conf].interface[interf].altsetting[alt].bInterfaceClass == 0x0b) {
								driver = "ccid";
							}
						}
					}	
				}
			}

			if (driver != NULL) {
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
	}
#else
	const char *base = "/sys/bus/usb/devices";
	DIR *dir = NULL;
	struct dirent *ent;

	if ((dir = opendir (base)) == NULL) {
		goto out;
	}

	while ((ent = readdir (dir)) != NULL) {
		if (ent->d_name[0] != '.') {
			int idProduct = -1;
			int idVendor = -1;
			int busnum = -1;
			int devnum = -1;

			idProduct = read_number ("%x", "%s/%s/%s", base, ent->d_name, "idProduct");
			idVendor = read_number ("%x", "%s/%s/%s", base, ent->d_name, "idVendor");
			busnum = read_number ("%d", "%s/%s/%s", base, ent->d_name, "busnum");
			devnum = read_number ("%d", "%s/%s/%s", base, ent->d_name, "devnum");

			ifd_debug (6, "coldplug: %s usb: %04x:%04x bus: %03d:%03d\n", ent->d_name, idProduct, idVendor, busnum, devnum);

			if (idProduct != -1 && idVendor != -1 && busnum != -1 && devnum != -1) {
				const char *driver = NULL;
				ifd_devid_t id;

				id.type = IFD_DEVICE_TYPE_USB;
				id.num = 2;
				id.val[0] = idVendor;
				id.val[1] = idProduct;

				if ((driver = ifd_driver_for_id(&id)) == NULL) {
					DIR *dir1 = NULL;
					struct dirent *ent1;

					if ((dir1 = opendir (base)) != NULL) {
						while ((ent1 = readdir (dir1)) != NULL && driver == NULL) {
							/* skip all none bus elements */
							if (strncmp (ent->d_name, ent1->d_name, strlen (ent->d_name))) {
								continue;
							}

							if (
								read_number (
									"%x",
									"%s/%s/%s/%s",
									base,
									ent->d_name,
									ent1->d_name,
									"bInterfaceClass"
								) == 0x0b
							) {
								driver = "ccid";
							}
						}
						closedir (dir1);
					}
				}

				if (driver != NULL) {
					char typedev[1024];

					snprintf(typedev, sizeof(typedev),
						"usb:/dev/bus/usb/%03d/%03d",
						busnum, devnum);
					ifd_spawn_handler(driver, typedev, -1);
				}
			}
		}
	}

out:
	if (dir != NULL) {
		closedir (dir);
	}
#endif
	return 0;
}

#endif				/* __linux__ */
