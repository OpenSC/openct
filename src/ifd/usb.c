/*
 * USB device handling
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <sys/poll.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

/*
 * Send/receive USB control block
 */
int ifd_usb_control(ifd_device_t * dev, unsigned int requesttype,
		    unsigned int request, unsigned int value,
		    unsigned int idx, void *buffer, size_t len, long timeout)
{
	int n;

	if (dev->type != IFD_DEVICE_TYPE_USB)
		return -1;
	if (timeout < 0)
		timeout = 10000;

	if ((ct_config.debug >= 3) && !(requesttype & 0x80)) {
		ifd_debug(4,
			  "usb req type=x%02x req=x%02x val=x%04x ind=x%04x len=%u",
			  requesttype, request, value, idx, len);
		if (len)
			ifd_debug(4, "send %s", ct_hexdump(buffer, len));
	}

	n = ifd_sysdep_usb_control(dev, requesttype, request, value, idx,
				   buffer, len, timeout);

	if ((ct_config.debug >= 3) && (requesttype & 0x80)) {
		ifd_debug(4,
			  "usb req type=x%02x req=x%02x val=x%04x ind=x%04x len=%d",
			  requesttype, request, value, idx, n);
		if (n > 0)
			ifd_debug(4, "recv %s", ct_hexdump(buffer, n));
	}

	return n;
}

/*
 * USB frame capture
 */
int ifd_usb_begin_capture(ifd_device_t * dev, int type, int endpoint,
			  size_t maxpacket, ifd_usb_capture_t ** capret)
{
	if (dev->type != IFD_DEVICE_TYPE_USB)
		return -1;

	if (ct_config.debug >= 5)
		ifd_debug(5, "usb capture type=%d ep=x%x maxpacket=%u",
			  type, endpoint, maxpacket);
	return ifd_sysdep_usb_begin_capture(dev, type, endpoint, maxpacket,
					    capret);
}

int ifd_usb_capture_event(ifd_device_t * dev, ifd_usb_capture_t * cap, void *buffer,
		    size_t len)
{
	int rc;

	if (dev->type != IFD_DEVICE_TYPE_USB)
		return -1;

	ifd_debug(5, "called.");
	rc = ifd_sysdep_usb_capture_event(dev, cap, buffer, len);
	if (ct_config.debug >= 3) {
		if (rc < 0)
			ifd_debug(1, "usb event capture: %s", ct_strerror(rc));
		if (rc > 0)
			ifd_debug(5, "usb event capture: recv %s",
				  ct_hexdump(buffer, rc));
		if (rc == 0)
			ifd_debug(5, "usb event capture: rc=%d (timeout?)", rc);
	}
	return rc;
}

int ifd_usb_capture(ifd_device_t * dev, ifd_usb_capture_t * cap, void *buffer,
		    size_t len, long timeout)
{
	int rc;

	if (dev->type != IFD_DEVICE_TYPE_USB)
		return -1;

	ifd_debug(5, "called, timeout=%ld ms.", timeout);
	rc = ifd_sysdep_usb_capture(dev, cap, buffer, len, timeout);
	if (ct_config.debug >= 3) {
		if (rc < 0)
			ifd_debug(1, "usb capture: %s", ct_strerror(rc));
		if (rc > 0)
			ifd_debug(5, "usb capture: recv %s",
				  ct_hexdump(buffer, rc));
		if (rc == 0)
			ifd_debug(5, "usb capture: rc=%d (timeout?)", rc);
	}
	return rc;
}

int ifd_usb_end_capture(ifd_device_t * dev, ifd_usb_capture_t * cap)
{
	ifd_debug(5, "called.");

	if (dev->type != IFD_DEVICE_TYPE_USB)
		return -1;
	return ifd_sysdep_usb_end_capture(dev, cap);
}

/*
 * Set usb params (for now, endpoint for transceive)
 */
static int usb_set_params(ifd_device_t * dev,
			  const ifd_device_params_t * params)
{

	ifd_debug(1, "called. config x%02x ifc x%02x eps x%02x/x%02x",
		  params->usb.configuration, params->usb.interface,
		  params->usb.ep_o, params->usb.ep_i);
	if (params->usb.interface != -1 && params->usb.interface > 255)
		return IFD_ERROR_INVALID_ARG;
	if (params->usb.ep_o != -1 && (params->usb.ep_o & ~0x7F))
		return IFD_ERROR_INVALID_ARG;
	if ((params->usb.ep_i != -1 && (params->usb.ep_i & ~0xFF))
	    || !(params->usb.ep_i & 0x80))
		return IFD_ERROR_INVALID_ARG;

	if (dev->settings.usb.interface != -1)
		ifd_sysdep_usb_release_interface(dev,
						 dev->settings.usb.interface);

	if (params->usb.configuration != -1
	    && ifd_sysdep_usb_set_configuration(dev, params->usb.configuration))
		return -1;

	if (params->usb.interface != -1) {
		if (ifd_sysdep_usb_claim_interface(dev, params->usb.interface))
			return -1;
		if (params->usb.altsetting != -1
		    && ifd_sysdep_usb_set_interface(dev,
						    params->usb.interface,
						    params->usb.altsetting))
			return -1;
	}

	dev->settings = *params;
	return 0;
}

static int usb_send(ifd_device_t * dev, const unsigned char *send,
		    size_t sendlen)
{
	if (dev->settings.usb.ep_o == -1)
		return IFD_ERROR_NOT_SUPPORTED;
	if (ct_config.debug >= 3) {
		ifd_debug(4, "usb send to=x%02x", dev->settings.usb.ep_o);
		if (sendlen)
			ifd_debug(4, "send %s", ct_hexdump(send, sendlen));
	}

	return ifd_sysdep_usb_bulk(dev,
				   dev->settings.usb.ep_o,
				   (unsigned char *)send, sendlen, 10000);
}

static int usb_recv(ifd_device_t * dev, unsigned char *recv, size_t recvlen,
		    long timeout)
{
	int rc;

	if (dev->settings.usb.ep_i == -1)
		return IFD_ERROR_NOT_SUPPORTED;

	rc = ifd_sysdep_usb_bulk(dev, dev->settings.usb.ep_i,
				 recv, recvlen, timeout);
	if (rc >= 0 && ct_config.debug >= 4) {
		ifd_debug(4, "usb recv from=x%02x", dev->settings.usb.ep_i);
		if (rc > 0)
			ifd_debug(4, "recv %s", ct_hexdump(recv, rc));
	}

	return rc;
}

static int usb_reset(ifd_device_t * dev)
{
	int rc;

	rc = ifd_sysdep_usb_reset(dev);

	return rc;
}

static int usb_get_eventfd(ifd_device_t * dev, short *events)
{
	int rc;

	rc = ifd_sysdep_usb_get_eventfd(dev, events);

	return rc;
}

static struct ifd_device_ops ifd_usb_ops;

/*
 * Open USB device
 */
ifd_device_t *ifd_open_usb(const char *device)
{
	ifd_device_t *dev;
	int fd;

	if ((fd = ifd_sysdep_usb_open(device)) < 0) {
		ct_error("Unable to open USB device %s: %m", device);
		return NULL;
	}

	ifd_usb_ops.poll_presence = ifd_sysdep_usb_poll_presence;
	ifd_usb_ops.set_params = usb_set_params;
	ifd_usb_ops.send = usb_send;
	ifd_usb_ops.recv = usb_recv;
	ifd_usb_ops.reset = usb_reset;
	ifd_usb_ops.get_eventfd = usb_get_eventfd;

	dev = ifd_device_new(device, &ifd_usb_ops, sizeof(*dev));
	dev->type = IFD_DEVICE_TYPE_USB;
	dev->timeout = 10000;
	dev->fd = fd;
	dev->settings.usb.configuration = -1;
	dev->settings.usb.interface = -1;
	dev->settings.usb.altsetting = -1;
	dev->settings.usb.ep_o = -1;
	dev->settings.usb.ep_i = -1;

	return dev;
}
