/*
 * Solaris specific functions
 *
 * Copyright (C) 2004 William Wanders <william@wanders.org>
 *
 */

#include "internal.h"
#if defined (sun)
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <openct/driver.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/systeminfo.h>
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
    uint8_t	bmRequestType;	/* Request Type 		*/
    uint8_t 	bRequest;	/* Setup Request		*/
    uint16_t	wValue;		/* Request Info			*/
    uint16_t	wIndex;		/* Index/Offset Info		*/
    uint16_t	wLength;	/* Number of bytes in transfer	*/
    uint8_t	data[1];	/* Outgoing Data 		*/
} usb_request_t;

/*
 * Globals
 */
static int cntrl0stat_fd=0;
static int devstat_fd=0;

/*
 * Defines
 */
#define USB_DEVICE_ROOT	"/dev/usb"
#define BYTESWAP(in)	(((in & 0xFF) << 8) + ((in & 0xFF00) >> 8))

static int
open_devstat(char *name)
{
    /* Open devstat to retrieve control pipe status. */
    if(!devstat_fd) {
	char *devstat;

	devstat=malloc(strlen(name) + 2);
	memset(devstat,0,strlen(name) + 2);
	strcpy(devstat, name);
	strcpy(devstat+strlen(name)-6, "devstat");

	ifd_debug(6, "open_devstat: open device status: \"%s\"", devstat);

	if((devstat_fd = open(devstat, O_RDONLY|O_EXCL|O_NONBLOCK)) < 0) {
	    ifd_debug(6, "open_devstat: Error opening \"%s\": %s", devstat, strerror(errno));
	    free(devstat);
	    return -1;
	}
	free(devstat);
	ifd_debug(6, "open_cntrl0stat: devstat_fd=%d", devstat_fd);
    }
    return 0;
}

static int
open_cntrl0stat(char *name)
{
    /* Open cntrl0stat to retrieve control pipe status. */
    if(!cntrl0stat_fd) {
	char *cntrl0stat;

	cntrl0stat=malloc(strlen(name) + 5);
	memset(cntrl0stat,0,strlen(name) + 5);
	strcpy(cntrl0stat, name);
	strcat(cntrl0stat, "stat");

	ifd_debug(6, "open_cntrl0stat: open control pipe status: \"%s\"", cntrl0stat);

	if((cntrl0stat_fd = open(cntrl0stat, O_RDONLY|O_EXCL)) < 0) {
	    ifd_debug(6, "open_cntrl0stat: Error opening \"%s\": %s", cntrl0stat, strerror(errno));
	    free(cntrl0stat);
	    return 0;
	}
	free(cntrl0stat);
	ifd_debug(6, "open_cntrl0stat: cntrl0stat_fd=%d", cntrl0stat_fd);
    }
    return 1;
}

/*
 * Return true if running on a SPARC system.
 */
#define IS_SPARC_BUFSIZE	6
static boolean_t
is_sparc(void)
{
    char buf[IS_SPARC_BUFSIZE];
    sysinfo(SI_ARCHITECTURE, buf, IS_SPARC_BUFSIZE);

    return (strcmp(buf, "sparc") == 0);
}

/*
 * Prepare a USB control request.  Please see USB 2.0 spec section 9.4.
 */
static void
prepare_usb_control_req(usb_request_t *req, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength)
{
    (*req).bmRequestType	= bmRequestType;
    (*req).bRequest		= bRequest;

    /* Sparc is big endian, USB little endian */
    if (is_sparc()) {
	(*req).wValue	= BYTESWAP(wValue);
	(*req).wIndex	= BYTESWAP(wIndex);
	(*req).wLength	= BYTESWAP(wLength);
    } else {
	(*req).wValue	= wValue;
	(*req).wIndex	= wIndex;
	(*req).wLength	= wLength;
    }
}

int
ifd_sysdep_device_type(const char *name)
{
    /*
     * A USB device that is supported through the UGEN driver
     * is typically indicated by its control endpoint.
     * Eg /dev/usb/<vendor>.<product>/<instance>/cntrl0
     */
    ifd_debug(6, "ifd_sysdep_device_type: name=\"%s\"", name);

    if (!name || name[0] != '/') {
	ifd_debug(6, "ifd_sysdep_device_type: device name does not start with \"/\"");
	return -1;
    }

    if (strncmp(name, "/dev/usb/", 9) == 0) {
	ifd_debug(6, "ifd_sysdep_device_type: detected USB device");
	return IFD_DEVICE_TYPE_USB;
    }

    ifd_debug(6, "ifd_sysdep_device_type: No USB device detected");
    return -1;
}

/*
 * Poll for presence of USB device
 */
int
ifd_sysdep_usb_poll_presence(ifd_device_t *dev, struct pollfd *pfd)
{
    int	devstat = 0;

    pfd->fd = -1;
    open_devstat(dev->name);
    if(read(devstat_fd, &devstat, sizeof (devstat))) {
	switch(devstat) {
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
    int			 bytes_to_process;
    int			 bytes_processed;
    int			 failed=0;
    usb_request_t	*usb_control_req;

    ifd_debug(6,"ifd_sysdep_usb_control:"
		" requestType = 0x%02x"
		" request = 0x%02x"
		" value = 0x%02x"
		" index = 0x%02x",
		requesttype, request, value, index);

    bytes_to_process=(sizeof(usb_request_t)-1) + (requesttype == 0x40 ? len : 0);
    ifd_debug(6, "ifd_sysdep_usb_control: usb_control_req=malloc(%d) [%d][%d]",
	bytes_to_process, (sizeof(usb_request_t)-1), (requesttype == 0x40 ? len : 0));
    usb_control_req=malloc(bytes_to_process);
    memset(usb_control_req,0,bytes_to_process);

    if(!open_cntrl0stat(dev->name)) return -1;

    /* Initialize the USB control request */
    prepare_usb_control_req(usb_control_req, requesttype, request, value, index, len);

    /* Add any additional */
    if(requesttype == 0x40 && len) {
	ifd_debug(6, "ifd_sysdep_usb_control: copying output data : %s", ct_hexdump(data,len));
	memcpy(usb_control_req->data,data,len);
    }

    /* Send request down the control pipe to the device. */
    bytes_processed = write(dev->fd, usb_control_req, bytes_to_process);
    if (bytes_processed != bytes_to_process) {
	ifd_debug(6, "ifd_sysdep_usb_control: write failed: %s", strerror(errno));
	ct_error("usb_control write failed: %s", strerror(errno));
	failed=IFD_ERROR_COMM_ERROR;
	goto cleanup;
    }

    /* Read the return data from the device. */
    bytes_processed = read(dev->fd, data, len);
    if (bytes_processed < 0) {
	ifd_debug(6, "ifd_sysdep_usb_control: read failed: %s", strerror(errno));
	ct_error("usb_control read failed: %s", strerror(errno));
	failed=IFD_ERROR_COMM_ERROR;
	goto cleanup;
    }

    cleanup:
    if(usb_control_req)
	free(usb_control_req);
    if(failed)
	return failed;

    return bytes_processed;
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

int
ifd_sysdep_usb_bulk(ifd_device_t *dev, int ep, void *buffer, size_t len, long timeout)
{
    return -1;
}

/*
 * USB URB capture
 */
struct ifd_usb_capture {
    int			type;
    int			endpoint;
    size_t		maxpacket;
    unsigned int	interface;
};

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
 * Scan the /dev/usb directory to see if there is any control pipe matching:
 *
 *            /dev/usb/<vendor>.<product>/<instance>/cntrl0
 *
 * If a suitable driver is found for this <vendor> <product> combination
 * an ifd handler is spawned for every instance.
 *
 */
int
ifd_scan_usb(void)
{
    DIR *usb_device_root;
    struct dirent *device_type;

    ifd_debug(1, "ifd_scan_usb:");
    usb_device_root=opendir(USB_DEVICE_ROOT);
    do {
	errno = 0;
	if ((device_type=readdir(usb_device_root))!=NULL) {
	    int vendor=0,product=0;
	    if (strncmp(device_type->d_name, ".", 1) == 0)
		continue;
	    if(sscanf(device_type->d_name,"%x.%x", &vendor, &product)==2) {
		char		 device_type_root_name[256];
		DIR		*device_type_root;
		struct dirent	*device_instance;
		ifd_devid_t	 id;
		const char	*driver;

	    	ifd_debug(1, "ifd_scan_usb: found device tree usb:%04x/%04x\n", vendor, product);

		id.type = IFD_DEVICE_TYPE_USB;
		id.num  = 2;
		id.val[0] = vendor;
		id.val[1] = product;

		if (!(driver = ifd_driver_for_id(&id)))
		    continue;

	    	ifd_debug(1, "ifd_scan_usb: found driver type \"%s\" for usb:%04x/%04x\n", driver, vendor, product);

		sprintf(device_type_root_name, "%s/%s", USB_DEVICE_ROOT, device_type->d_name);
		if(!(device_type_root=opendir(device_type_root_name)))
		    continue;

		do {
		    errno = 0;
		    if ((device_instance=readdir(device_type_root))!=NULL) {

			int device=-1;
			if (strncmp(device_instance->d_name, ".", 1) == 0)
			    continue;
			ifd_debug(1, "ifd_scan_usb: \tfound device %s\n", device_instance->d_name);
			if(sscanf(device_instance->d_name,"%d", &device)==1) {
			    char device_instance_cntrl0_name[256];
			    struct stat dummy;

			    sprintf(device_instance_cntrl0_name,"%s/%d/cntrl0",
				device_type_root_name, device);
			    if(stat(device_instance_cntrl0_name, &dummy)==0) {
				ifd_debug(1, "ifd_scan_usb: \t\tfound device instance %s\n",
				    device_instance_cntrl0_name);
				ifd_spawn_handler(driver,
				    device_instance_cntrl0_name, -1);
			    }
			}
		    }
		} while (device_instance != NULL);
		if (errno != 0)
		    perror("error reading directory");

		closedir(device_type_root);
	    }
	}
    } while (device_type != NULL);

    if (errno != 0)
	ifd_debug(1, "ifd_scan_usb: error reading directory");

    closedir(usb_device_root);
    return 0;
}
#endif
