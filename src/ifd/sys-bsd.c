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
#include <dev/usb/usb.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <openct/driver.h>

int
ifd_sysdep_device_type(const char *name)
{
	struct stat stb;

	ifd_debug(1, "BSD: ifd_sysdep_device_type(%s)", name);
	if (!name || name[0] != '/')
		return -1;

	if (!strncmp(name, "/dev/ugen", 9)) {
		ifd_debug(1, "BSD: returning IFD_DEVICE_TYPE_USB");
		return IFD_DEVICE_TYPE_USB;
	}

	if (stat(name, &stb) < 0)
		return -1;
#if 0
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
#endif
	return -1;
}

int
ifd_sysdep_usb_poll_presence(ifd_device_t *dev, struct pollfd *pfd)
{
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
	if(len)
		ifd_debug(5, "BSD: CTRL SEND data %s", ct_hexdump(data,len));

	val = timeout;
	if ((rc = ioctl(dev->fd, USB_SET_TIMEOUT, &val)) < 0) {
		ifd_debug(1,"USB_SET_TIMEOUT failed: %d", rc);
		ct_error("usb_set_timeout failed: %s(%d)",
				strerror(errno), errno);
        	return IFD_ERROR_COMM_ERROR;
	}
 
	if ((rc = ioctl(dev->fd, USB_DO_REQUEST, &ctrl)) < 0) {
		ifd_debug(1, "USB_DO_REQUEST failed: %d", rc);
		ct_error("usb_do_request failed: %s (%d)",
				strerror(errno), errno);
		return IFD_ERROR_COMM_ERROR;
        }

	if(ctrl.ucr_data==NULL)
		ifd_debug(1, "BSD: ctrl.ucr_data == NULL ");
	if(ctrl.ucr_data && ctrl.ucr_actlen)
		ifd_debug(1, "BSD: CTRL RECV data %s",
			ct_hexdump(ctrl.ucr_data,ctrl.ucr_actlen));
	return ctrl.ucr_actlen;
}

int
ifd_sysdep_usb_set_configuration(ifd_device_t *dev, int config) 
{
     return -1;
}

int
ifd_sysdep_usb_set_interface(ifd_device_t *dev, int ifc, int alt) 
{
     return -1;
}

int
ifd_sysdep_usb_claim_interface(ifd_device_t *dev, int interface) 
{
     return -1;
}

int
ifd_sysdep_usb_release_interface(ifd_device_t *dev, int interface) 
{
     return -1;
}

/*
 * USB bulk transfer
 */
int
ifd_sysdep_usb_bulk(ifd_device_t *dev, int ep, void *buffer, size_t len,
		    long timeout) 
{
     return -1;
}

int
ifd_sysdep_usb_begin_capture(ifd_device_t *dev,
		int type, int endpoint, size_t maxpacket,
	       	ifd_usb_capture_t **capret)
{
	return -1;
}

int
ifd_sysdep_usb_capture(ifd_device_t *dev,
		ifd_usb_capture_t *cap,
		void *buffer, size_t len,
		long timeout)
{
	return -1;
}

int
ifd_sysdep_usb_end_capture(ifd_device_t *dev, ifd_usb_capture_t *cap)
{
	return -1;
}

/*
 * Scan all usb devices to see if there is one we support
 */
int
ifd_scan_usb(void)
{
    int i, controller_fd;
    char controller_devname[10];

    ifd_debug(1, "BSD: ifd_scan_usb");
    for (i = 0; i < 10; i++) {
	snprintf(controller_devname, 10, "/dev/usb%d", i);
	if((controller_fd = open(controller_devname, O_RDONLY))<0)
	    continue;

	if (controller_fd >= 0) {
	    int address;
	    for (address = 1; address < USB_MAX_DEVICES; address++) {
		struct usb_device_info	 device_info;
		ifd_devid_t		 id;
		const char		*driver;
		char			device[256];

		device_info.udi_addr = address;

		if(ioctl(controller_fd, USB_DEVICEINFO, &device_info)) {
		    if (errno != ENXIO)
			fprintf(stderr, "addr %d: I/O error\n", address);
		    continue;
		}

		if(strncmp(device_info.udi_devnames[0],"ugen",4)!=0)
		    continue;

		id.type = IFD_DEVICE_TYPE_USB;
		id.num  = 2;

		id.val[0] = device_info.udi_vendorNo;
		id.val[1] = device_info.udi_productNo;

		ifd_debug(1, "BSD: ifd_scan_usb: "
			     "ifd_driver_for(%s[0x%04x].%s[0x%04x)",
			     device_info.udi_vendor,
			     device_info.udi_vendorNo,
			     device_info.udi_product,
			     device_info.udi_productNo);

		if (!(driver = ifd_driver_for_id(&id)))
		    continue;

		snprintf(device, sizeof(device),
			"/dev/%s", device_info.udi_devnames[0]);

		ifd_spawn_handler(driver, device, -1);
	    }
	    close(controller_fd);
	} else {
	    if (errno == ENOENT || errno == ENXIO)
		continue;
	    /* a more suitable error recovery should be done here */
	}
    }
    return 0;
}
#endif /* __Net/Free/OpenBSD__ */
