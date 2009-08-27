/*
 * *BSD specific functions
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 * Copyright (C) 2003 Andreas Jellinghaus <aj@dungeon.inka.de>
 * Copyright (C) 2003 Markus Friedl <markus@openbsd.org>
 * Copyright (C) 2004-2005 William Wanders <william@wanders.org>
 *
 * These functions need to be re-implemented for every
 * new platform.
 */

#include "internal.h"
#if defined(__NetBSD__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__FreeBSD_kernel__) || defined(__DragonFly__)
#include <sys/types.h>
#ifndef ENABLE_LIBUSB
#if defined(__DragonFly__)
#include <bus/usb/usb.h>
#else
#include <dev/usb/usb.h>
#endif
#endif /* !ENABLE_LIBUSB */
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/file.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <openct/driver.h>
#ifdef ENABLE_LIBUSB
#include <usb.h>
#endif

#include "usb-descriptors.h"

#ifdef ENABLE_LIBUSB
struct usb_dev_handle *devices[128];
#endif

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

#ifndef ENABLE_LIBUSB
typedef struct ep {
	int ep_fd;
} ep_t;

typedef ep_t interface_t[128];

static interface_t interfaces[1];

#define USB_REQUEST_SIZE	8

/*
 * Open interface endpoint
 */
int open_ep(char *name, int interface, int endpoint, int flags)
{
	char filename[256];

	if (interfaces[interface][endpoint].ep_fd) {
		ifd_debug(6, "open_ep: endpoint already opened");
		return 0;
	}

#ifdef __OpenBSD__
	snprintf(filename, sizeof(filename), "%s.%02d", name, endpoint);
#else
	snprintf(filename, sizeof(filename), "%s.%d", name, endpoint);
#endif /* __OpenBSD__ */

	if ((interfaces[interface][endpoint].ep_fd = open(filename, flags)) < 0) {
		ifd_debug(6, "open_ep: error opening \"%s\": %s", filename,
			  strerror(errno));
		interfaces[interface][endpoint].ep_fd = 0;
		return -1;
	}
	return 0;
}

static void
close_ep(int interface, int endpoint)
{
	if (interfaces[interface][endpoint].ep_fd) {
		close(interfaces[interface][endpoint].ep_fd);
		interfaces[interface][endpoint].ep_fd = 0;
	}
}
#endif /* !ENABLE_LIBUSB */

int ifd_sysdep_usb_bulk(ifd_device_t * dev, int ep, void *buffer, size_t len,
			long timeout)
{
	int bytes_to_process;
	int bytes_processed;
	int direction =
	    (ep & IFD_USB_ENDPOINT_DIR_MASK) == IFD_USB_ENDPOINT_IN ? 1 : 0;
	int endpoint = (ep & ~IFD_USB_ENDPOINT_DIR_MASK);

	ct_debug("ifd_sysdep_usb_bulk: endpoint=%d direction=%d", endpoint,
		 direction);
	if (direction) {
#ifdef ENABLE_LIBUSB
		if ((bytes_to_process =
		     usb_bulk_read(devices[dev->fd], ep, buffer, len,
				   timeout)) < 0) {
			ifd_debug(6, "ifd_sysdep_usb_bulk: read failed: %s",
				  strerror(errno));
			ct_error("usb_bulk read failed: %s", strerror(errno));
			return IFD_ERROR_COMM_ERROR;
		}
#else
		int one = 1;

		if (open_ep(dev->name, 0, endpoint, O_RDONLY | O_NONBLOCK)) {
			ct_debug("ifd_sysdep_usb_bulk: opening endpoint failed");
			return -1;
		}

		if (ioctl(interfaces[0][endpoint].ep_fd, USB_SET_SHORT_XFER,
		    &one) < 0) {
			ifd_debug(6, "ifd_sysdep_usb_bulk: USB_SET_SHORT_XFER"
				  " failed: %s", strerror(errno));
			ct_error("usb_bulk read failed: %s", strerror(errno));
		}
		if ((bytes_to_process =
		     read(interfaces[0][endpoint].ep_fd, buffer, len)) < 0) {
			ifd_debug(6, "ifd_sysdep_usb_bulk: read failed: %s",
				  strerror(errno));
			ct_error("usb_bulk read failed: %s", strerror(errno));
			return IFD_ERROR_COMM_ERROR;
		}
#endif /* ENABLE_LIBUSB */
		ct_debug("ifd_sysdep_usb_bulk: read %d bytes",
			 bytes_to_process);
		return bytes_to_process;
	} else {
#ifndef ENABLE_LIBUSB
		if (open_ep(dev->name, 0, endpoint, O_WRONLY | O_NONBLOCK)) {
			ct_debug("ifd_sysdep_usb_bulk: opening endpoint failed");
			return -1;
		}
#endif /* !ENABLE_LIBUSB */

		bytes_to_process = len;
		if ((bytes_processed =
#ifdef ENABLE_LIBUSB
		     usb_bulk_write(devices[dev->fd], ep, buffer,
				    bytes_to_process, timeout)
#else
		     write(interfaces[0][endpoint].ep_fd, buffer,
			   bytes_to_process)
#endif /* ENABLE_LIBUSB */
			    ) != bytes_to_process) {
			ifd_debug(6, "ifd_sysdep_usb_bulk: write failed: %s",
				  strerror(errno));
			ct_error("usb_bulk write failed: %s", strerror(errno));
			return IFD_ERROR_COMM_ERROR;
		}
		ct_debug("ifd_sysdep_usb_bulk: wrote buffer[%d]=%s",
			 bytes_processed, ct_hexdump(buffer, len));
		return bytes_processed;
	}
}

int ifd_sysdep_usb_get_eventfd(ifd_device_t * dev, short *events)
{
	return -1;
}

/*
 * USB URB capture
 */
struct ifd_usb_capture {
	int type;
	int endpoint;
	size_t maxpacket;
	unsigned int interface;
};

int ifd_sysdep_usb_begin_capture(ifd_device_t * dev, int type, int ep,
				 size_t maxpacket, ifd_usb_capture_t ** capret)
{
	ifd_usb_capture_t *cap;
#ifndef ENABLE_LIBUSB
	int direction =
	    (ep & IFD_USB_ENDPOINT_DIR_MASK) == IFD_USB_ENDPOINT_IN ? 1 : 0;
	int endpoint = (ep & ~IFD_USB_ENDPOINT_DIR_MASK);
#endif /* !ENABLE_LIBUSB */

	if (!(cap = (ifd_usb_capture_t *) calloc(1, sizeof(*cap) + maxpacket))) {
		ct_error("out of memory");
		return IFD_ERROR_NO_MEMORY;
	}
	cap->type = type;
	cap->endpoint = ep;
	cap->maxpacket = maxpacket;

#ifndef ENABLE_LIBUSB
	if (!interfaces[0][endpoint].ep_fd) {
		if (open_ep(dev->name, 0, endpoint, O_RDONLY | O_NONBLOCK)) {
			ct_debug
			    ("ifd_sysdep_usb_begin_capture: opening endpoint failed");
			return -1;
		}
	}
#endif /* !ENABLE_LIBUSB */
	*capret = cap;
	return 0;
}

int ifd_sysdep_usb_capture_event(ifd_device_t * dev, ifd_usb_capture_t * cap,
			   void *buffer, size_t len)
{
	return IFD_ERROR_NOT_SUPPORTED;
}

int ifd_sysdep_usb_capture(ifd_device_t * dev, ifd_usb_capture_t * cap,
			   void *buffer, size_t len, long timeout)
{
	int bytes_to_process = 0;
#ifdef ENABLE_LIBUSB
	if ((bytes_to_process =
	     usb_interrupt_read(devices[dev->fd], cap->endpoint, buffer, len,
				timeout)) < 0) {
		ifd_debug(6,
			  "ifd_sysdep_usb_capture: usb_interrupt_read failed: %s",
			  strerror(errno));
		ct_error("usb_bulk read failed: %s", strerror(errno));
		return IFD_ERROR_COMM_ERROR;
	}
#else
	struct timeval begin;
	int direction =
	    (cap->endpoint & IFD_USB_ENDPOINT_DIR_MASK) ==
	    IFD_USB_ENDPOINT_IN ? 1 : 0;
	int endpoint = (cap->endpoint & ~IFD_USB_ENDPOINT_DIR_MASK);

	gettimeofday(&begin, NULL);
	do {
		struct pollfd pfd;
		long wait;

		if ((wait = (timeout - ifd_time_elapsed(&begin))) <= 0)
			return IFD_ERROR_TIMEOUT;

		pfd.fd = interfaces[0][endpoint].ep_fd;
		pfd.events = POLLIN;
		if (poll(&pfd, 1, wait) != 1)
			continue;

		if ((bytes_to_process =
		     read(interfaces[0][endpoint].ep_fd, buffer, len)) < 0) {
			ifd_debug(6, "ifd_sysdep_usb_bulk: read failed: %s",
				  strerror(errno));
			ct_error("usb_bulk read failed: %s", strerror(errno));
			return IFD_ERROR_COMM_ERROR;
		}
	} while (!bytes_to_process);
#endif /* ENABLE_LIBUSB */
	ct_debug("ifd_sysdep_usb_capture: read buffer[%d]=%s", bytes_to_process,
		 ct_hexdump(buffer, bytes_to_process));
	return bytes_to_process;
}

int ifd_sysdep_usb_end_capture(ifd_device_t * dev, ifd_usb_capture_t * cap)
{
#ifndef ENABLE_LIBUSB
	int direction =
	    (cap->endpoint & IFD_USB_ENDPOINT_DIR_MASK) ==
	    IFD_USB_ENDPOINT_IN ? 1 : 0;
	int endpoint = (cap->endpoint & ~IFD_USB_ENDPOINT_DIR_MASK);
	close_ep(0, endpoint);
#endif /* !ENABLE_LIBUSB */
	if (cap)
		free(cap);
	return 0;
}

/*
 * USB control command
 */
int ifd_sysdep_usb_control(ifd_device_t * dev, unsigned int requesttype,
			   unsigned int request, unsigned int value,
			   unsigned int index, void *data, size_t len,
			   long timeout)
{
	int rc, val;

#ifdef ENABLE_LIBUSB
	ct_debug("ifd_sysdep_usb_control: dev->fd=%d handle=0x%x", dev->fd,
		 devices[dev->fd]);
	if ((rc =
	     usb_control_msg(devices[dev->fd], requesttype, request, value,
			     index, data, len, timeout)) < 0) {
		ifd_debug(1, "usb_control_msg failed: %d", rc);
		ct_error("usb_control_msg failed: %s(%d)",
			 strerror(errno), errno);
		return IFD_ERROR_COMM_ERROR;
	}

	ct_debug("ifd_sysdep_usb_control: return rc=%d", rc);
	return rc;
#else
	struct usb_ctl_request ctrl;
	int retries;

	ifd_debug(1, "BSD: ifd_sysdep_usb_control(0x%x)", request);
	memset(&ctrl, 0, sizeof(ctrl));

	ctrl.ucr_request.bmRequestType = requesttype;
	ctrl.ucr_request.bRequest = request;
	USETW(ctrl.ucr_request.wValue, value);
	USETW(ctrl.ucr_request.wIndex, index);
	USETW(ctrl.ucr_request.wLength, len);

	ctrl.ucr_data = data;
	ctrl.ucr_flags = USBD_SHORT_XFER_OK;

	ifd_debug(1, "BSD: CTRL bmRequestType 0x%x bRequest 0x%x "
		  "wValue 0x%x wIndex 0x%x wLength 0x%x",
		  requesttype, request, value, index, len);
	if (len)
		ifd_debug(5, "BSD: CTRL SEND data %s", ct_hexdump(data, len));

	val = timeout;
	if ((rc = ioctl(dev->fd, USB_SET_TIMEOUT, &val)) < 0) {
		ifd_debug(1, "USB_SET_TIMEOUT failed: %d", rc);
		ct_error("usb_set_timeout failed: %s(%d)",
			 strerror(errno), errno);
		return IFD_ERROR_COMM_ERROR;
	}

	retries = 5;
	while ((rc = ioctl(dev->fd, USB_DO_REQUEST, &ctrl)) < 0 && retries > 0) {
		ifd_debug(1, "USB_DO_REQUEST failed: %d", rc);
		ct_error("usb_do_request failed: %s (%d)",
			 strerror(errno), errno);
		retries--;
		if ((retries == 0) || (errno != EIO)) 
			return IFD_ERROR_COMM_ERROR;
	}

	if (ctrl.ucr_data == NULL)
		ifd_debug(1, "BSD: ctrl.ucr_data == NULL ");
	if (ctrl.ucr_data && ctrl.ucr_actlen)
		ifd_debug(1, "BSD: CTRL RECV data %s",
			  ct_hexdump(ctrl.ucr_data, ctrl.ucr_actlen));
	return ctrl.ucr_actlen;
#endif /* ENABLE_LIBUSB */
}

int ifd_sysdep_usb_set_configuration(ifd_device_t * dev, int config)
{
	int rc;
#ifdef ENABLE_LIBUSB
	if ((rc = usb_set_configuration(devices[dev->fd], config)) < 0) {
		ifd_debug(1, "usb_set_configuration failed: %d", rc);
#else
	int value;
	value = config;
	if ((rc = ioctl(dev->fd, USB_SET_CONFIG, &value)) < 0) {
		ifd_debug(1, "USB_SET_CONFIG failed: %d", rc);
#endif /* ENABLE_LIBUSB */
		ct_error("usb_set_configuration failed: %s(%d)",
			 strerror(errno), errno);
		return IFD_ERROR_COMM_ERROR;
	}
	return 0;
}

int ifd_sysdep_usb_set_interface(ifd_device_t * dev, int ifc, int alt)
{
	int rc;
#ifdef ENABLE_LIBUSB
	if ((rc = usb_set_altinterface(devices[dev->fd], alt)) < 0) {
		ifd_debug(1, "usb_set_altinterface failed: %d", rc);
#else
	struct usb_alt_interface {
		int uai_config_index;
		int uai_interface_index;
		int uai_alt_no;
	} value;

	value.uai_config_index = ifc;
	value.uai_interface_index = 0;
	value.uai_alt_no = alt;
	if ((rc = ioctl(dev->fd, USB_SET_ALTINTERFACE, &value)) < 0) {
		ifd_debug(1, "USB_SET_ALTINTERFACE failed: %d", rc);
#endif /* ENABLE_LIBUSB */
		ct_error("usb_set_interface failed: %s(%d)",
			 strerror(errno), errno);
		return IFD_ERROR_COMM_ERROR;
	}
	return 0;
}

int ifd_sysdep_usb_claim_interface(ifd_device_t * dev, int interface)
{
#ifdef ENABLE_LIBUSB
	int rc;

	ct_debug("ifd_sysdep_usb_claim_interface: interface=%d", interface);
	if ((rc = usb_claim_interface(devices[dev->fd], interface)) < 0) {
		ifd_debug(1, "usb_clain_interface failed: %d", rc);
		ct_error("usb_release_interface failed: %s(%d)",
			 strerror(errno), errno);
		return IFD_ERROR_COMM_ERROR;
	}
#else
	ct_debug
	    ("ifd_sysdep_usb_claim_interface: interface=%d (not yet implemented)",
	     interface);
#endif /* ENABLE_LIBUSB */
	return 0;
}

int ifd_sysdep_usb_release_interface(ifd_device_t * dev, int interface)
{
#ifdef ENABLE_LIBUSB
	int rc;

	ct_debug("ifd_sysdep_usb_release_interface: interface=%d", interface);
	if ((rc = usb_release_interface(devices[dev->fd], interface)) < 0) {
		ifd_debug(1, "usb_release_interface failed: %d", rc);
		ct_error("usb_release_interface failed: %s(%d)",
			 strerror(errno), errno);
		return IFD_ERROR_COMM_ERROR;
	}
#else
	ct_debug
	    ("ifd_sysdep_usb_release_interface: interface=%d (not yet implemented)",
	     interface);
#endif /* ENABLE_LIBUSB */
	return 0;
}

int ifd_sysdep_usb_open(const char *device)
{
#ifdef ENABLE_LIBUSB
	struct usb_bus *bus;
	struct usb_device *dev;

	ct_debug("ifd_sysdep_usb_open: name=%s", device);
	ct_debug("ifd_sysdep_usb_open: usb_init()");
	usb_init();
	ct_debug("ifd_sysdep_usb_open: usb_find_busses()");
	usb_find_busses();
	ct_debug("ifd_sysdep_usb_open: usb_find_devices()");
	usb_find_devices();

	ct_debug("ifd_sysdep_usb_open: walk devices");
	for (bus = usb_busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			int i;

			if (strcmp(dev->filename, device) != 0)
				continue;

			ct_debug
			    ("ifd_sysdep_usb_open: found match name=%s device=%s",
			     device, dev->filename);
			for (i = 0; i < 128; i++) {
				if (devices[i] == NULL) {
					devices[i] = usb_open(dev);
					ct_debug
					    ("ifd_sysdep_usb_open: usb_open index=%d handle=0x%x",
					     i, devices[i]);
					return i;
				}
			}
		}
	}
	return -1;
#else
#ifdef __OpenBSD__
	char path[256];

	if (snprintf(&path, sizeof(path), "%s.00", device) < 0)
		return -1;
	return open(path, O_RDWR);
#else
	return open(device, O_RDWR);
#endif /* __OpenBSD__ */
#endif /* ENABLE_LIBUSB */
}

int ifd_sysdep_usb_reset(ifd_device_t * dev)
{
	/* not implemented so far */
	return -1;
}

/*
 * Scan all usb devices to see if there is one we support
 */
int ifd_scan_usb(void)
{
#ifdef ENABLE_LIBUSB
	struct usb_bus *bus;
	struct usb_device *dev;
	ifd_devid_t id;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	id.type = IFD_DEVICE_TYPE_USB;
	id.num = 2;
	for (bus = usb_busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			const char *driver;
			char typedev[PATH_MAX];

			id.val[0] = dev->descriptor.idVendor;
			id.val[1] = dev->descriptor.idProduct;

			/* FIXME: if we don't find a driver with vendor/product
			 * then check for the interface type (ccid) and use
			 * driver ccid... */

			if (!(driver = ifd_driver_for_id(&id)))
				continue;

			snprintf(typedev, sizeof(typedev),
				 "usb:%s", dev->filename);

			ifd_spawn_handler(driver, typedev, -1);
		}
	}
#else
	int i, controller_fd;
	char controller_devname[10];

	ifd_debug(1, "BSD: ifd_scan_usb");
	for (i = 0; i < 10; i++) {
		int address;

		snprintf(controller_devname, 10, "/dev/usb%d.00", i);
		if ((controller_fd = open(controller_devname, O_RDONLY)) < 0)
			continue;

		if (controller_fd < 0) {
			if (errno == ENOENT || errno == ENXIO)
				continue;
			/* a more suitable error recovery should be done here */
			continue;
		}
		for (address = 1; address < USB_MAX_DEVICES; address++) {
			struct usb_device_info device_info;
			ifd_devid_t id;
			const char *driver;
			char typedev[256];

			device_info.udi_addr = address;

			if (ioctl(controller_fd, USB_DEVICEINFO, &device_info)) {
				if (errno != ENXIO)
					fprintf(stderr,
						"addr %d: I/O error\n",
						address);
				continue;
			}

			if (strncmp
			    (device_info.udi_devnames[0], "ugen", 4) != 0)
				continue;

			id.type = IFD_DEVICE_TYPE_USB;
			id.num = 2;

			id.val[0] = device_info.udi_vendorNo;
			id.val[1] = device_info.udi_productNo;

			ifd_debug(1, "BSD: ifd_scan_usb: "
				  "ifd_driver_for(%s[0x%04x].%s[0x%04x)",
				  device_info.udi_vendor,
				  device_info.udi_vendorNo,
				  device_info.udi_product,
				  device_info.udi_productNo);

			/* FIXME: if we don't find a driver with vendor/product
			 * then check for the interface type (ccid) and use
			 * driver ccid... */

			if (!(driver = ifd_driver_for_id(&id)))
				continue;
			snprintf(typedev, sizeof(typedev),
				 "usb:/dev/%s", device_info.udi_devnames[0]);

			ifd_spawn_handler(driver, typedev, -1);
		}
		close(controller_fd);
	}
#endif /* ENABLE_LIBUSB */
	return 0;
}
#endif				/* __Net/Free/OpenBSD__ */
