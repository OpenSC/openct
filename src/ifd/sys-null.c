/*
 * A void implementation for any platform
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 *
 * These functions need to be re-implemented for every
 * new platform.
 */

#include "internal.h"
#if !defined(sun) && !defined (__NetBSD__) && !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__FreeBSD_kernel__) && !defined(__linux__) && !defined(__APPLE__) && !defined(__DragonFly__)
#include <sys/types.h>
#include <stdio.h>
#include <openct/driver.h>

/*
 * USB handling
 */
int ifd_sysdep_usb_poll_presence(ifd_device_t * dev, struct pollfd *pfd)
{
#if 0
	if (pfd->revents & POLLHUP)
		return 0;
	pfd->fd = dev->fd;
	pfd->events = POLLHUP;
	return 1;
#else
	return -1;
#endif
}

int ifd_sysdep_usb_get_eventfd(ifd_device_t * dev, short *events)
{
	return -1;
}

int ifd_sysdep_usb_control(ifd_device_t * dev, unsigned int requesttype,
			   unsigned int request, unsigned int value,
			   unsigned int index, void *data, size_t len,
			   long timeout)
{
	return -1;
}

int ifd_sysdep_usb_set_configuration(ifd_device_t * dev, int config)
{
	return -1;
}

int ifd_sysdep_usb_set_interface(ifd_device_t * dev, int ifc, int alt)
{
	return -1;
}

int ifd_sysdep_usb_claim_interface(ifd_device_t * dev, int interface)
{
	return -1;
}

int ifd_sysdep_usb_release_interface(ifd_device_t * dev, int interface)
{
	return -1;
}

/*
 * USB bulk transfer
 */
int ifd_sysdep_usb_bulk(ifd_device_t * dev, int ep, void *buffer, size_t len,
			long timeout)
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

int ifd_sysdep_usb_begin_capture(ifd_device_t * dev, int type, int endpoint,
				 size_t maxpacket, ifd_usb_capture_t ** capret)
{
	return -1;
}

int ifd_sysdep_usb_capture_event(ifd_device_t * dev, ifd_usb_capture_t * cap,
			   void *buffer, size_t len)
{
	return IFD_ERROR_NOT_SUPPORTED;
}

int ifd_sysdep_usb_capture(ifd_device_t * dev, ifd_usb_capture_t * cap,
			   void *buffer, size_t len, long timeout)
{
	return -1;
}

int ifd_sysdep_usb_end_capture(ifd_device_t * dev, ifd_usb_capture_t * cap)
{
	return -1;
}

int ifd_sysdep_usb_open(const char *device)
{
	return -1;
}

int ifd_sysdep_usb_reset(ifd_device_t * dev)
{
	return -1;
}

/*
 * Scan all usb devices to see if there is one we support
 */
int ifd_scan_usb(void)
{
	return 0;
}

#endif
