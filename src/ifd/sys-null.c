/*
 * Generic functions
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 *
 * These functions need to be re-implemented for every
 * new platform.
 */

#include "internal.h"
#if !defined (__NetBSD__) && !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__linux__)
#include <sys/types.h>
#include <stdio.h>
#include <openct/driver.h>

int
ifd_sysdep_device_type(const char *name)
{
	return -1;
}

const char *
ifd_sysdep_channel_to_name(unsigned int num)
{
	return NULL;
}

/*
 * USB control command
 */
int
ifd_sysdep_usb_control(int fd,
		unsigned int requesttype,
		unsigned int request,
		unsigned int value,
		unsigned int index,
		void *data, size_t len, long timeout)
{
	return -1;
}

/*
 * USB URB capture
 */
struct ifd_usb_capture {
	int		type;
	int		endpoint;
	size_t		maxpacket;
	unsigned int	interface;
};

int
ifd_sysdep_usb_begin_capture(int fd,
		int type, int endpoint, size_t maxpacket,
	       	ifd_usb_capture_t **capret)
{
	return -1;
}

int
ifd_sysdep_usb_capture(int fd,
		ifd_usb_capture_t *cap,
		void *buffer, size_t len,
		long timeout)
{
	return -1;
}

int
ifd_sysdep_usb_end_capture(int fd, ifd_usb_capture_t *cap)
{
	return -1;
}

/*
 * Scan all usb devices to see if there is one we support
 * This function is called at start-up because we can't be
 * sure the hotplug system notifies us of devices that were
 * attached at startup time already.
 */
int
ifd_scan_usb(void)
{
	return 0;
}

#endif
