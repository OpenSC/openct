/*
 * Sunray specific functions
 *
 * Copyright (C) 2005 William Wanders <william@wanders.org>
 *
 * These functions need to be re-implemented for every
 * new platform.
 */

#include "internal.h"
#if defined(sunray)
#include <sys/types.h>
#include <usb.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include <openct/driver.h>
#define USB_READ_INTERRUPT_TIMEOUT_WORKAROUND 1
#ifdef USB_READ_INTERRUPT_TIMEOUT_WORKAROUND
#include <setjmp.h>
#endif

#include "usb-descriptors.h"

struct usb_dev_handle *devices[128];

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

int ifd_sysdep_usb_bulk(ifd_device_t * dev, int ep, void *buffer, size_t len,
			long timeout)
{
	int bytes_to_process;
	int bytes_processed;
	int direction =
	    (ep & IFD_USB_ENDPOINT_DIR_MASK) == IFD_USB_ENDPOINT_IN ? 1 : 0;

	ct_debug("ifd_sysdep_usb_bulk: endpoint=%d direction=%d", ep,
		 direction);
	if (direction) {
		if ((bytes_to_process =
		     usb_bulk_read(devices[dev->fd], ep, buffer, len,
				   timeout)) < 0) {
			ifd_debug(6, "ifd_sysdep_usb_bulk: read failed: %s",
				  strerror(errno));
			ct_error("usb_bulk read failed: %s", strerror(errno));
			return IFD_ERROR_COMM_ERROR;
		}
		ct_debug("ifd_sysdep_usb_bulk: read %d bytes",
			 bytes_to_process);
		return bytes_to_process;
	} else {
		bytes_to_process = len;
		if ((bytes_processed =
		     usb_bulk_write(devices[dev->fd], ep, buffer,
				    bytes_to_process,
				    timeout)) != bytes_to_process) {
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

	if (!(cap = (ifd_usb_capture_t *) calloc(1, sizeof(*cap) + maxpacket))) {
		ct_error("out of memory");
		return IFD_ERROR_NO_MEMORY;
	}
	cap->type = type;
	cap->endpoint = ep;
	cap->maxpacket = maxpacket;

	*capret = cap;
	return 0;
}

#ifdef USB_READ_INTERRUPT_TIMEOUT_WORKAROUND
static jmp_buf env;

void workaround_timeout(int sig)
{
	ifd_debug(6, "SunRay: signal_handler: sig=%d", sig);
	switch (sig) {
	case SIGALRM:
		ifd_debug(6,
			  "SunRay: workaround_timeout: sig=SIGALRM -> longjmp");
		longjmp(env, sig);
	default:
		ifd_debug(6, "SunRay: workaround_timeout: exit(%d)", sig);
		exit(sig);
	}
}
#endif

int ifd_sysdep_usb_capture_event(ifd_device_t * dev, ifd_usb_capture_t * cap,
			   void *buffer, size_t len)
{
	return IFD_ERROR_NOT_SUPPORTED;
}

int ifd_sysdep_usb_capture(ifd_device_t * dev, ifd_usb_capture_t * cap,
			   void *buffer, size_t len, long timeout)
{
	int bytes_to_process = 0;

	ifd_debug(6, "SunRay: ifd_sysdep_usb_capture: len=%d timeout=%d", len,
		  timeout);
#ifdef USB_READ_INTERRUPT_TIMEOUT_WORKAROUND
	signal(SIGALRM, workaround_timeout);
	if (setjmp(env) != 0) {
		ifd_debug(6,
			  "ifd_sysdep_usb_capture: setjmp -> workaround_timeout");
		return 0;
	}
	ualarm(timeout * 1000, 0);
	timeout = 0;
#endif
	if ((bytes_to_process =
	     usb_interrupt_read(devices[dev->fd], cap->endpoint, buffer, len,
				timeout)) < 0) {
		ualarm(0, 0);
		ifd_debug(6,
			  "ifd_sysdep_usb_capture: usb_interrupt_read failed: %s",
			  strerror(errno));
		ct_error("usb_bulk read failed: %s", strerror(errno));
		return IFD_ERROR_COMM_ERROR;
	}
#ifdef USB_READ_INTERRUPT_TIMEOUT_WORKAROUND
	ualarm(0, 0);
#endif
	ct_debug("ifd_sysdep_usb_capture: read buffer[%d]=%s", bytes_to_process,
		 ct_hexdump(buffer, bytes_to_process));
	return bytes_to_process;
}

int ifd_sysdep_usb_end_capture(ifd_device_t * dev, ifd_usb_capture_t * cap)
{
	if (cap)
		free(cap);
	return 0;
}

/*
 * Event fd
 */
int ifd_sysdep_usb_get_eventfd(ifd_device_t * dev, short *events)
{
	return -1;
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
}

int ifd_sysdep_usb_set_configuration(ifd_device_t * dev, int config)
{
	int rc;
	if ((rc = usb_set_configuration(devices[dev->fd], config)) < 0) {
		ifd_debug(1, "usb_set_configuration failed: %d", rc);
		ct_error("usb_set_configuration failed: %s(%d)",
			 strerror(errno), errno);
		return IFD_ERROR_COMM_ERROR;
	}
	return 0;
}

int ifd_sysdep_usb_set_interface(ifd_device_t * dev, int ifc, int alt)
{
	int rc;

	if ((rc = usb_set_altinterface(devices[dev->fd], alt)) < 0) {
		ifd_debug(1, "usb_set_altinterface failed: %d", rc);
		ct_error("usb_set_interface failed: %s(%d)",
			 strerror(errno), errno);
		return IFD_ERROR_COMM_ERROR;
	}
	return 0;
}

int ifd_sysdep_usb_claim_interface(ifd_device_t * dev, int interface)
{
	int rc;

	ct_debug("ifd_sysdep_usb_claim_interface: interface=%d", interface);
	if ((rc = usb_claim_interface(devices[dev->fd], interface)) < 0) {
		ifd_debug(1, "usb_clain_interface failed: %d", rc);
		ct_error("usb_release_interface failed: %s(%d)",
			 strerror(errno), errno);
		return IFD_ERROR_COMM_ERROR;
	}
	return 0;
}

int ifd_sysdep_usb_release_interface(ifd_device_t * dev, int interface)
{
	int rc;

	ct_debug("ifd_sysdep_usb_release_interface: interface=%d", interface);
	if ((rc = usb_release_interface(devices[dev->fd], interface)) < 0) {
		ifd_debug(1, "usb_release_interface failed: %d", rc);
		ct_error("usb_release_interface failed: %s(%d)",
			 strerror(errno), errno);
		return IFD_ERROR_COMM_ERROR;
	}
	return 0;
}

/*
 * Open usb device
 */
int ifd_sysdep_usb_open(const char *name)
{
	struct usb_bus *bus;
	struct usb_device *dev;

	ct_debug("ifd_sysdep_usb_open: name=%s", name);
	ct_debug("ifd_sysdep_usb_open: usb_init()");
	usb_init();
	ct_debug("ifd_sysdep_usb_open: usb_find_busses()");
	usb_find_busses();
	ct_debug("ifd_sysdep_usb_open: usb_find_devices()");
	usb_find_devices();

	ct_debug("ifd_sysdep_usb_open: walk devices");
	for (bus = usb_busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			char device[PATH_MAX];
			int i;

			snprintf(device, sizeof(device),
				 "%s/usb/%s/%s",
				 getenv("UTDEVROOT"),
				 bus->dirname, dev->filename);
			ct_debug("ifd_sysdep_usb_open: check device=%s",
				 device);
			if (strcmp(device, name) != 0)
				continue;

			ct_debug
			    ("ifd_sysdep_usb_open: found match name=%s device=%s",
			     name, device);
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
	return ENOENT;
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
				 "usb:%s/usb/%s/%s",
				 getenv("UTDEVROOT"),
				 bus->dirname, dev->filename);

			ifd_spawn_handler(driver, typedev, -1);
		}
	}
	return 0;
}
#endif				/* sunray */
