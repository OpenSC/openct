/*
 * Solaris specific functions
 *
 * Copyright (C) 2004 William Wanders <william@wanders.org>
 *
 */

#include "internal.h"
#if defined (sun) && !defined (sunray)
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <openct/driver.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

#include <sys/usb/usba.h>
#include <sys/usb/clients/ugen/usb_ugen.h>

/*
 * USB Device Setup Packet
 * Refer to USB 2.0/ 9.3
 *
 * All USB devices respond to requests from the host on the device's
 * Default Control Pipe. These request are made using control transfers.
 * The request and the request's parameters are sent to the device in
 * the Setup packet. Every Setup packet has eight bytes:
 *
 * Offset	Field		Size
 * ----------------------------------
 *   0		bmRequestType	 1
 *   1		bRequest	 1
 *   2		wValue		 2
 *   4		wIndex		 2
 *   6		wLength		 2
 *
 * IMPORTANT: Pay attention to (little) endianness.
 *
 */
typedef struct usb_request {
	uint8_t bmRequestType;	/* Request Type                 */
	uint8_t bRequest;	/* Setup Request                */
	uint16_t wValue;	/* Request Info                 */
	uint16_t wIndex;	/* Index/Offset Info            */
	uint16_t wLength;	/* Number of bytes in transfer  */
	uint8_t data[1];	/* Outgoing Data                */
} usb_request_t;

/*
 * Globals
 */
static int cntrl0_fd = 0;
static int cntrl0stat_fd = 0;
static int devstat_fd = 0;

typedef struct ep {
	int ep_fd[2];
	int ep_stat_fd[2];
} ep_t;

typedef ep_t interface_t[128];

static interface_t interfaces[1];

/*
 * Defines
 */
#define USB_DEVICE_ROOT	"/dev/usb"
#define BYTESWAP(in)	(((in & 0xFF) << 8) + ((in & 0xFF00) >> 8))
#define USB_REQUEST_SIZE	8

/*
 * Open device status interface
 */
static int open_devstat(char *name)
{
	/* Open devstat to retrieve control pipe status. */
	if (!devstat_fd) {
		char *devstat;

		if ((devstat = calloc(1, strlen(name) + 2)) == NULL) {
			ct_error("out of memory");
			return IFD_ERROR_NO_MEMORY;
		}
		strcpy(devstat, name);
		strcpy(devstat + strlen(name) - 6, "devstat");

		ifd_debug(6, "open_devstat: open device status: \"%s\"",
			  devstat);

		if ((devstat_fd =
		     open(devstat, O_RDONLY | O_EXCL | O_NONBLOCK)) < 0) {
			ifd_debug(6, "open_devstat: Error opening \"%s\": %s",
				  devstat, strerror(errno));
			free(devstat);
			return -1;
		}
		free(devstat);
		ifd_debug(6, "open_cntrl0stat: devstat_fd=%d", devstat_fd);
	}
	return 0;
}

/*
 * Open device control status interface
 */
static int open_cntrl0stat(char *name)
{
	/* Open cntrl0stat to retrieve control pipe status. */
	if (!cntrl0stat_fd) {
		char *cntrl0stat;

		if ((cntrl0stat = calloc(1, strlen(name) + 5)) == NULL) {
			ct_error("out of memory");
			return -1;
		}
		strcpy(cntrl0stat, name);
		strcat(cntrl0stat, "stat");

		ifd_debug(6,
			  "open_cntrl0stat: open control pipe status: \"%s\"",
			  cntrl0stat);

		if ((cntrl0stat_fd = open(cntrl0stat, O_RDONLY | O_EXCL)) < 0) {
			ifd_debug(6,
				  "open_cntrl0stat: Error opening \"%s\": %s",
				  cntrl0stat, strerror(errno));
			free(cntrl0stat);
			return 0;
		}
		free(cntrl0stat);
		ifd_debug(6, "open_cntrl0stat: cntrl0stat_fd=%d",
			  cntrl0stat_fd);
	}
	return 1;
}

/*
 * Open interface endpoint
 */
int open_ep(char *name, int interface, int endpoint, int direction, int flags)
{
	char filename[256];
	char intdirep[32];

	if (interfaces[interface][endpoint].ep_fd[direction]) {
		ifd_debug(6, "open_ep: endpoint already opened");
		return 0;
	}
	sprintf((char *)&intdirep, "if%d%s%d",
		interface, direction ? "in" : "out", endpoint);

	memset((char *)&filename, 0, strlen(name) + 2);
	strcpy((char *)&filename, name);
	filename[strlen(name) - 6] = '\0';
	strcat((char *)&filename, (char *)&intdirep);

	if ((interfaces[interface][endpoint].ep_fd[direction] =
	     open(filename,
		  direction ? O_RDONLY | flags : O_WRONLY | flags)) < 0) {
		ifd_debug(6, "open_ep: error opening \"%s\": %s", filename,
			  strerror(errno));
		interfaces[interface][endpoint].ep_fd[direction] = 0;
		return -1;
	}
#if 0
	strcat((char *)&filename, "stat");
	if ((interfaces[interface][endpoint].ep_stat_fd[direction] =
	     open(filename, O_RDONLY | O_NONBLOCK)) < 0) {
		ifd_debug(6, "open_ep: error opening \"%s\": %s", filename,
			  strerror(errno));
		close(interfaces[interface][endpoint].ep_fd[direction]);
		interfaces[interface][endpoint].ep_fd[direction] = 0;
		interfaces[interface][endpoint].ep_stat_fd[direction] = 0;
		return -1;
	}
#endif
	return 0;
}

void close_ep(int interface, int endpoint, int direction)
{
	if (interfaces[interface][endpoint].ep_fd[direction]) {
		close(interfaces[interface][endpoint].ep_fd[direction]);
		interfaces[interface][endpoint].ep_fd[direction] = 0;
	}
	if (interfaces[interface][endpoint].ep_stat_fd[direction]) {
		close(interfaces[interface][endpoint].ep_stat_fd[direction]);
		interfaces[interface][endpoint].ep_stat_fd[direction] = 0;
	}
}

/*
 * Prepare a USB control request.  Please see USB 2.0 spec section 9.4.
 */
static void
prepare_usb_control_req(usb_request_t * req, uint8_t bmRequestType,
			uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
			uint16_t wLength)
{
	(*req).bmRequestType = bmRequestType;
	(*req).bRequest = bRequest;

#ifdef _BIG_ENDIAN
	/* Sparc is big endian, USB little endian */
	(*req).wValue = BYTESWAP(wValue);
	(*req).wIndex = BYTESWAP(wIndex);
	(*req).wLength = BYTESWAP(wLength);
#else
	(*req).wValue = wValue;
	(*req).wIndex = wIndex;
	(*req).wLength = wLength;
#endif				/* _BIG_ENDIAN */
}

/*
 * Poll for presence of USB device
 */
int ifd_sysdep_usb_poll_presence(ifd_device_t * dev, struct pollfd *pfd)
{
	int devstat = 0;

	pfd->fd = -1;
	if (open_devstat(dev->name) < 0) {
		ifd_debug(1,
			  "ifd_sysdep_usb_poll_presence: cannot open devstat device for %s",
			  dev->name);
		return 0;
	}
	if (read(devstat_fd, &devstat, sizeof(devstat))) {
		switch (devstat) {
		case USB_DEV_STAT_ONLINE:
			ifd_debug(1, "devstat: ONLINE (%d)", devstat);
			break;
		case USB_DEV_STAT_DISCONNECTED:
			ifd_debug(1, "devstat: DISCONNECTED (%d)", devstat);
			return 0;
		default:
			ifd_debug(1, "devstat: %d", devstat);
			return 0;
		}
	}
	return 1;
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
int
ifd_sysdep_usb_control(ifd_device_t * dev,
		       unsigned int requesttype,
		       unsigned int request,
		       unsigned int value,
		       unsigned int index, void *data, size_t len, long timeout)
{
	int bytes_to_process;
	int bytes_processed;
	int failed = 0;
	usb_request_t *usb_control_req;
	char *recv_data;

	ifd_debug(6, "ifd_sysdep_usb_control:"
		  " requestType = 0x%02x"
		  " request = 0x%02x"
		  " value = 0x%02x"
		  " index = 0x%02x", requesttype, request, value, index);

	bytes_to_process = USB_REQUEST_SIZE +
	    ((requesttype & USB_EP_DIR_MASK) == USB_EP_DIR_OUT ? len : 0);
	if ((usb_control_req = calloc(1, bytes_to_process)) == NULL) {
		ct_error("out of memory");
		return -1;
	}
	if ((recv_data = calloc(1, len)) == NULL) {
		ct_error("out of memory");
		free(usb_control_req);
		return -1;
	}

	if (!open_cntrl0stat(dev->name))
		return -1;

	/* Initialize the USB control request */
	prepare_usb_control_req(usb_control_req, requesttype, request, value,
				index, len);

	/* Add any additional */
	if (((requesttype & USB_EP_DIR_MASK) == USB_EP_DIR_OUT) && len) {
		ifd_debug(6, "ifd_sysdep_usb_control: copying output data : %s",
			  ct_hexdump(data, len));
		memcpy(usb_control_req->data, data, len);
	}

	/* Send request down the control pipe to the device. */
	bytes_processed = write(dev->fd, usb_control_req, bytes_to_process);
	if (bytes_processed != bytes_to_process) {
		ifd_debug(6, "ifd_sysdep_usb_control: write failed: %s",
			  strerror(errno));
		ct_error("usb_control write failed: %s", strerror(errno));
		failed = IFD_ERROR_COMM_ERROR;
		goto cleanup;
	}

	/* Read the return data from the device. */
	bytes_processed = read(dev->fd, recv_data, len);
	if (bytes_processed < 0) {
		ifd_debug(6, "ifd_sysdep_usb_control: read failed: %s",
			  strerror(errno));
		ct_error("usb_control read failed: %s", strerror(errno));
		failed = IFD_ERROR_COMM_ERROR;
		goto cleanup;
	}
	if (bytes_processed) {
		ifd_debug(6, "ifd_sysdep_usb_control: input data[%d] : %s",
			  bytes_processed, ct_hexdump(recv_data,
						      bytes_processed));
		if ((requesttype & USB_EP_DIR_MASK) == USB_EP_DIR_IN)
			memcpy(data, recv_data, bytes_processed);
	} else
		ifd_debug(6, "ifd_sysdep_usb_control: input data[%d]",
			  bytes_processed);

      cleanup:
	if (recv_data)
		free(recv_data);
	if (usb_control_req)
		free(usb_control_req);
	if (failed)
		return failed;

	return bytes_processed;
}

int ifd_sysdep_usb_set_configuration(ifd_device_t * dev, int config)
{
	ct_debug
	    ("ifd_sysdep_usb_set_configuration: config=%d (not yet implemented)",
	     config);
	return 0;
}

int ifd_sysdep_usb_set_interface(ifd_device_t * dev, int ifc, int alt)
{
	ct_debug
	    ("ifd_sysdep_usb_set_interface: alt=%d ifc=%d (not yet implemented)",
	     alt, ifc);
	return 0;
}

int ifd_sysdep_usb_claim_interface(ifd_device_t * dev, int interface)
{
	ct_debug
	    ("ifd_sysdep_usb_claim_interface: interface=%d (not yet implented)",
	     interface);
	return 0;
}

int ifd_sysdep_usb_release_interface(ifd_device_t * dev, int interface)
{
	ct_debug("ifd_sysdep_usb_release_interface: not implented yet");
	return 0;
}

int
ifd_sysdep_usb_bulk(ifd_device_t * dev, int ep, void *buffer, size_t len,
		    long timeout)
{
	int bytes_to_process;
	int bytes_processed;
	int direction = (ep & USB_EP_DIR_MASK) == USB_EP_DIR_IN ? 1 : 0;
	int endpoint = (ep & ~USB_EP_DIR_MASK);

	ct_debug("ifd_sysdep_usb_bulk: endpoint=%d direction=%d", endpoint,
		 direction);
	if (open_ep(dev->name, 0, endpoint, direction, 0)) {
		ct_debug("ifd_sysdep_usb_bulk: opening endpoint failed");
		return -1;
	}
	if (direction) {
		if ((bytes_to_process =
		     read(interfaces[0][endpoint].ep_fd[direction], buffer,
			  len)) < 0) {
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
		     write(interfaces[0][endpoint].ep_fd[direction], buffer,
			   bytes_to_process)) != bytes_to_process) {
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

int
ifd_sysdep_usb_begin_capture(ifd_device_t * dev,
			     int type, int ep, size_t maxpacket,
			     ifd_usb_capture_t ** capret)
{
	ifd_usb_capture_t *cap;
	int direction = (ep & USB_EP_DIR_MASK) == USB_EP_DIR_IN ? 1 : 0;
	int endpoint = (ep & ~USB_EP_DIR_MASK);

	if (!(cap = (ifd_usb_capture_t *) calloc(1, sizeof(*cap) + maxpacket))) {
		ct_error("out of memory");
		return -1;
	}
	cap->type = type;
	cap->endpoint = ep;
	cap->maxpacket = maxpacket;

	if (!interfaces[0][endpoint].ep_fd[direction]) {
		if (open_ep(dev->name, 0, endpoint, direction, O_NONBLOCK)) {
			ct_debug
			    ("ifd_sysdep_usb_begin_capture: opening endpoint failed");
			return -1;
		}
	}
	*capret = cap;
	return 0;
}

int ifd_sysdep_usb_capture_event(ifd_device_t * dev, ifd_usb_capture_t * cap,
			   void *buffer, size_t len)
{
	return IFD_ERROR_NOT_SUPPORTED;
}

int
ifd_sysdep_usb_capture(ifd_device_t * dev,
		       ifd_usb_capture_t * cap,
		       void *buffer, size_t len, long timeout)
{
	struct timeval begin;
	int bytes_to_process = 0;
	int direction =
	    (cap->endpoint & USB_EP_DIR_MASK) == USB_EP_DIR_IN ? 1 : 0;
	int endpoint = (cap->endpoint & ~USB_EP_DIR_MASK);

	gettimeofday(&begin, NULL);
	do {
		struct pollfd pfd;
		long wait;

		if ((wait = (timeout - ifd_time_elapsed(&begin))) <= 0)
			return IFD_ERROR_TIMEOUT;

		pfd.fd = interfaces[0][endpoint].ep_fd[direction];
		pfd.events = POLL_IN;
		if (poll(&pfd, 1, wait) != 1)
			continue;

		if ((bytes_to_process =
		     read(interfaces[0][endpoint].ep_fd[direction], buffer,
			  len)) < 0) {
			ifd_debug(6, "ifd_sysdep_usb_bulk: read failed: %s",
				  strerror(errno));
			ct_error("usb_bulk read failed: %s", strerror(errno));
			return IFD_ERROR_COMM_ERROR;
		}
	} while (!bytes_to_process);
	ct_debug("ifd_sysdep_usb_capture: read buffer[%d]=%s", bytes_to_process,
		 ct_hexdump(buffer, bytes_to_process));
	return bytes_to_process;
}

int ifd_sysdep_usb_end_capture(ifd_device_t * dev, ifd_usb_capture_t * cap)
{
	int direction =
	    (cap->endpoint & USB_EP_DIR_MASK) == USB_EP_DIR_IN ? 1 : 0;
	int endpoint = (cap->endpoint & ~USB_EP_DIR_MASK);
	close_ep(0, endpoint, direction);
	if (cap)
		free(cap);
	return 0;
}

int ifd_sysdep_usb_open(const char *device)
{
	return open(device, O_RDWR);
}

int ifd_sysdep_usb_reset(ifd_device_t * dev)
{
	/* not implemented so far */
	return -1;
}

/*
 * Scan the /dev/usb directory to see if there is any control pipe matching:
 *
 *            /dev/usb/<vendor>.<product>/<instance>/cntrl0
 *
 * If a suitable driver is found for this <vendor> <product> combination
 * an ifd handler is spawned for every instance.
 *
 */
int ifd_scan_usb(void)
{
	DIR *usb_device_root;
	struct dirent *device_type;

	ifd_debug(1, "ifd_scan_usb:");
	usb_device_root = opendir(USB_DEVICE_ROOT);
	do {
		int vendor = 0, product = 0;
		char device_type_root_name[256];
		DIR *device_type_root;
		struct dirent *device_instance;
		ifd_devid_t id;
		const char *driver;

		errno = 0;
		if ((device_type = readdir(usb_device_root)) == NULL)
			continue;
		if (strncmp(device_type->d_name, ".", 1) == 0)
			continue;
		if (sscanf(device_type->d_name, "%x.%x", &vendor, &product) !=
		    2)
			continue;

		ifd_debug(1, "ifd_scan_usb: found device tree usb:%04x/%04x\n",
			  vendor, product);

		id.type = IFD_DEVICE_TYPE_USB;
		id.num = 2;
		id.val[0] = vendor;
		id.val[1] = product;

		/* FIXME: if we don't find a driver with vendor/product
		 * then check for the interface type (ccid) and use
		 * driver ccid... */

		if (!(driver = ifd_driver_for_id(&id)))
			continue;

		ifd_debug(1,
			  "ifd_scan_usb: found driver type \"%s\" for usb:%04x/%04x\n",
			  driver, vendor, product);

		sprintf(device_type_root_name, "%s/%s", USB_DEVICE_ROOT,
			device_type->d_name);
		if (!(device_type_root = opendir(device_type_root_name)))
			continue;

		do {
			int device = -1;
			char device_instance_cntrl0_name[256];
			char typedev[256];
			struct stat dummy;

			errno = 0;
			if ((device_instance =
			     readdir(device_type_root)) == NULL)
				continue;

			if (strncmp(device_instance->d_name, ".", 1) == 0)
				continue;
			ifd_debug(1, "ifd_scan_usb: \tfound device %s\n",
				  device_instance->d_name);
			if (sscanf(device_instance->d_name, "%d", &device) != 1)
				continue;

			sprintf(device_instance_cntrl0_name, "%s/%d/cntrl0",
				device_type_root_name, device);
			if (stat(device_instance_cntrl0_name, &dummy) == 0) {
				ifd_debug(1,
					  "ifd_scan_usb: \t\tfound device instance %s\n",
					  device_instance_cntrl0_name);
				snprintf(typedev, sizeof(typedev),
					 "usb:%s", device_instance_cntrl0_name);
				ifd_spawn_handler(driver, typedev, -1);
			}
		} while (device_instance != NULL);
		if (errno != 0)
			perror("error reading directory");

		closedir(device_type_root);
	} while (device_type != NULL);

	if (errno != 0)
		ifd_debug(1, "ifd_scan_usb: error reading directory");

	closedir(usb_device_root);
	return 0;
}
#endif				/* sun && !sunray */
