/* from libusb 0.1.6a: descriptors.c
 * Copyright (c) 2001 Johannes Erdfelt <johannes@erdfelt.com>
 *
 * This library is covered by the LGPL, read LICENSE for details.
 */
/*
 * This code looks surprisingly similar to the code I wrote for the Linux
 * kernel. It's not a coincidence :)
 */

#include "internal.h"
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include "usb-descriptors.h"

static int ifd_usb_parse_endpoint(struct ifd_usb_endpoint_descriptor *endpoint,
				  unsigned char *buffer, int size)
{
	struct ifd_usb_descriptor_header *header;
	unsigned char *begin;
	int parsed = 0, len, numskipped;

	header = (struct ifd_usb_descriptor_header *)buffer;

	/* Everything should be fine being passed into here, but we sanity */
	/*  check JIC */
	if (header->bLength > size) {
		ct_debug("ran out of descriptors parsing");
		return -1;
	}

	if (header->bDescriptorType != IFD_USB_DT_ENDPOINT) {
		ct_debug
		    ("unexpected descriptor 0x%X, expecting endpoint descriptor, type 0x%X",
		     endpoint->bDescriptorType, IFD_USB_DT_ENDPOINT);
		return parsed;
	}
	if (header->bLength == IFD_USB_DT_ENDPOINT_AUDIO_SIZE)
		memcpy(endpoint, buffer, IFD_USB_DT_ENDPOINT_AUDIO_SIZE);
	else
		memcpy(endpoint, buffer, IFD_USB_DT_ENDPOINT_SIZE);

	IFD_USB_LE16_TO_CPU(endpoint->wMaxPacketSize);

	buffer += header->bLength;
	size -= header->bLength;
	parsed += header->bLength;

	/* Skip over the rest of the Class Specific or Vendor Specific */
	/*  descriptors */
	begin = buffer;
	numskipped = 0;
	while (size >= sizeof(struct ifd_usb_descriptor_header)) {
		header = (struct ifd_usb_descriptor_header *)buffer;

		if (header->bLength < 2) {
			ct_debug("invalid descriptor length of %d",
				 header->bLength);
			return -1;
		}

		/* If we find another "proper" descriptor then we're done  */
		if ((header->bDescriptorType == IFD_USB_DT_ENDPOINT) ||
		    (header->bDescriptorType == IFD_USB_DT_INTERFACE) ||
		    (header->bDescriptorType == IFD_USB_DT_CONFIG) ||
		    (header->bDescriptorType == IFD_USB_DT_DEVICE))
			break;

		ct_debug("in p_ep: skipping descriptor 0x%X",
			 header->bDescriptorType);
		numskipped++;

		buffer += header->bLength;
		size -= header->bLength;
		parsed += header->bLength;
	}

	if (numskipped)
		ct_debug
		    ("skipped %d class/vendor specific endpoint descriptors",
		     numskipped);

	/* Copy any unknown descriptors into a storage area for drivers */
	/*  to later parse */
	len = (int)(buffer - begin);
	if (!len) {
		endpoint->extra = NULL;
		endpoint->extralen = 0;
		return parsed;
	}

	endpoint->extra = (unsigned char *)malloc(len);
	if (!endpoint->extra) {
		ct_error("out of memory");
		endpoint->extralen = 0;
		return parsed;
	}

	memcpy(endpoint->extra, begin, len);
	endpoint->extralen = len;

	return parsed;
}

static int ifd_usb_parse_interface(struct ifd_usb_interface *interface,
				   unsigned char *buffer, int size)
{
	int i, len, numskipped, retval, parsed = 0;
	struct ifd_usb_descriptor_header *header;
	struct ifd_usb_interface_descriptor *ifp;
	unsigned char *begin;

	interface->num_altsetting = 0;

	while (size > 0) {
		interface->altsetting = (struct ifd_usb_interface_descriptor *)
		    realloc(interface->altsetting,
			    sizeof(struct ifd_usb_interface_descriptor)
			    * (interface->num_altsetting + 1)
		    );
		if (!interface->altsetting) {
			ct_error("out of memory");
			return -1;
		}

		ifp = interface->altsetting + interface->num_altsetting;
		interface->num_altsetting++;

		memcpy(ifp, buffer, IFD_USB_DT_INTERFACE_SIZE);

		/* Skip over the interface */
		buffer += ifp->bLength;
		parsed += ifp->bLength;
		size -= ifp->bLength;

		begin = buffer;
		numskipped = 0;

		/* Skip over any interface, class or vendor descriptors */
		while (size >= sizeof(struct ifd_usb_descriptor_header)) {
			header = (struct ifd_usb_descriptor_header *)buffer;

			if (header->bLength < 2) {
				ct_debug("invalid descriptor length of %d",
					 header->bLength);
				return -1;
			}

			/* If we find another "proper" descriptor then we're done */
			if ((header->bDescriptorType == IFD_USB_DT_INTERFACE) ||
			    (header->bDescriptorType == IFD_USB_DT_ENDPOINT) ||
			    (header->bDescriptorType == IFD_USB_DT_CONFIG) ||
			    (header->bDescriptorType == IFD_USB_DT_DEVICE))
				break;

			numskipped++;

			buffer += header->bLength;
			parsed += header->bLength;
			size -= header->bLength;
		}

		if (numskipped)
			ct_debug
			    ("skipped %d class/vendor specific interface descriptors",
			     numskipped);

		/* Copy any unknown descriptors into a storage area for */
		/*  drivers to later parse */
		len = (int)(buffer - begin);
		if (!len) {
			ifp->extra = NULL;
			ifp->extralen = 0;
		} else {
			ifp->extra = (unsigned char *)malloc(len);
			if (!ifp->extra) {
				ct_error("out of memory");
				ifp->extralen = 0;
				return -1;
			}
			memcpy(ifp->extra, begin, len);
			ifp->extralen = len;
		}

		/* Did we hit an unexpected descriptor? */
		header = (struct ifd_usb_descriptor_header *)buffer;
		if ((size >= sizeof(struct ifd_usb_descriptor_header)) &&
		    ((header->bDescriptorType == IFD_USB_DT_CONFIG) ||
		     (header->bDescriptorType == IFD_USB_DT_DEVICE)))
			return parsed;

		if (ifp->bNumEndpoints > IFD_USB_MAXENDPOINTS) {
			ct_debug("too many endpoints");
			return -1;
		}

		ifp->endpoint = (struct ifd_usb_endpoint_descriptor *)
		    malloc(ifp->bNumEndpoints *
			   sizeof(struct ifd_usb_endpoint_descriptor));
		if (!ifp->endpoint) {
			ct_debug("couldn't allocate memory for ifp->endpoint");
			return -1;
		}

		memset(ifp->endpoint, 0, ifp->bNumEndpoints *
		       sizeof(struct ifd_usb_endpoint_descriptor));

		for (i = 0; i < ifp->bNumEndpoints; i++) {
			header = (struct ifd_usb_descriptor_header *)buffer;

			if (header->bLength > size) {
				ct_debug("ran out of descriptors parsing");
				return -1;
			}

			retval =
			    ifd_usb_parse_endpoint(ifp->endpoint + i, buffer,
						   size);
			if (retval < 0)
				return retval;

			buffer += retval;
			parsed += retval;
			size -= retval;
		}

		/* We check to see if it's an alternate to this one */
		ifp = (struct ifd_usb_interface_descriptor *)buffer;
		if (size < IFD_USB_DT_INTERFACE_SIZE ||
		    ifp->bDescriptorType != IFD_USB_DT_INTERFACE ||
		    !ifp->bAlternateSetting)
			return parsed;
	}

	return parsed;
}

static int ifd_usb_parse_configuration(struct ifd_usb_config_descriptor *config,
				       unsigned char *buffer)
{
	int i, retval, size;
	struct ifd_usb_descriptor_header *header;

	memcpy(config, buffer, IFD_USB_DT_CONFIG_SIZE);
	IFD_USB_LE16_TO_CPU(config->wTotalLength);
	size = config->wTotalLength;

	if (config->bNumInterfaces > IFD_USB_MAXINTERFACES) {
		ct_debug("too many interfaces");
		return -1;
	}

	config->interface = (struct ifd_usb_interface *)
	    malloc(config->bNumInterfaces * sizeof(struct ifd_usb_interface));
	if (!config->interface) {
		ct_debug("out of memory");
		return -1;
	}

	memset(config->interface, 0,
	       config->bNumInterfaces * sizeof(struct ifd_usb_interface));

	buffer += config->bLength;
	size -= config->bLength;

	config->extra = NULL;
	config->extralen = 0;

	for (i = 0; i < config->bNumInterfaces; i++) {
		int numskipped, len;
		unsigned char *begin;

		/* Skip over the rest of the Class Specific or Vendor */
		/*  Specific descriptors */
		begin = buffer;
		numskipped = 0;
		while (size >= sizeof(struct ifd_usb_descriptor_header)) {
			header = (struct ifd_usb_descriptor_header *)buffer;

			if ((header->bLength > size) || (header->bLength < 2)) {
				ct_debug("invalid descriptor length of %d",
					 header->bLength);
				return -1;
			}

			/* If we find another "proper" descriptor then we're done */
			if ((header->bDescriptorType == IFD_USB_DT_ENDPOINT) ||
			    (header->bDescriptorType == IFD_USB_DT_INTERFACE) ||
			    (header->bDescriptorType == IFD_USB_DT_CONFIG) ||
			    (header->bDescriptorType == IFD_USB_DT_DEVICE))
				break;

			ct_debug("skipping descriptor 0x%X",
				 header->bDescriptorType);
			numskipped++;

			buffer += header->bLength;
			size -= header->bLength;
		}

		if (numskipped)
			ct_debug
			    ("skipped %d class/vendor specific endpoint descriptors",
			     numskipped);

		/* Copy any unknown descriptors into a storage area for */
		/*  drivers to later parse */
		len = (int)(buffer - begin);
		if (len) {
			/* FIXME: We should realloc and append here */
			if (!config->extralen) {
				config->extra = (unsigned char *)malloc(len);
				if (!config->extra) {
					ct_debug
					    ("couldn't allocate memory for config extra descriptors");
					config->extralen = 0;
					return -1;
				}

				memcpy(config->extra, begin, len);
				config->extralen = len;
			}
		}

		retval =
		    ifd_usb_parse_interface(config->interface + i, buffer,
					    size);
		if (retval < 0)
			return retval;

		buffer += retval;
		size -= retval;
	}

	return size;
}

int ifd_usb_get_device(ifd_device_t * dev, struct ifd_usb_device_descriptor *d)
{
	unsigned char devd[18];
	int r;

	/* 0x6  == USB_REQ_GET_DESCRIPTOR
	 * 0x1  == USB_DT_DEVICE
	 */
	r = ifd_usb_control(dev, 0x80, 0x6, 0x100, 0, devd, 18, 10000);
	if (r <= 0) {
		ct_error("cannot get descriptors");
		return 1;
	}
	memcpy(d, devd, sizeof(devd));
	d->bcdUSB = devd[3] << 8 | devd[2];
	d->idVendor = devd[9] << 8 | devd[8];
	d->idProduct = devd[11] << 8 | devd[10];
	d->bcdDevice = devd[13] << 8 | devd[12];

	return 0;
}

int ifd_usb_get_config(ifd_device_t * dev, int n,
		       struct ifd_usb_config_descriptor *ret)
{
	unsigned char *b;
	unsigned short len;
	int r;
	memset(ret, 0, sizeof(struct ifd_usb_config_descriptor));

	/* 0x6  == USB_REQ_GET_DESCRIPTOR
	 * 0x2  == USB_DT_CONFIG
	 */
	r = ifd_usb_control(dev, 0x80, 0x6, 0x200 | n, 0, ret, 8, 1000);
	if (r <= 0) {
		ct_error("cannot get descriptors");
		return 1;
	}

	IFD_USB_LE16_TO_CPU(ret->wTotalLength);
	len = ret->wTotalLength;
	b = (unsigned char *)malloc(len);
	if (!b) {
		ct_error("cannot malloc descriptor buffer");
		return 1;
	}

	r = ifd_usb_control(dev, 0x80, 0x6, 0x200 | n, 0, b, len, 1000);
	if (r < len) {
		ct_error("cannot get descriptors");
		free(b);
		return 1;
	}
	r = ifd_usb_parse_configuration(ret, b);
	free(b);
	if (r < 0) {
		return 1;
	}
	return 0;
}

void ifd_usb_free_configuration(struct ifd_usb_config_descriptor *cf)
{
	int i, j, k;
	if (!cf->interface)
		return;

	for (i = 0; i < cf->bNumInterfaces; i++) {
		struct ifd_usb_interface *ifp = &cf->interface[i];

		if (!ifp->altsetting)
			break;

		for (j = 0; j < ifp->num_altsetting; j++) {
			struct ifd_usb_interface_descriptor *as =
			    &ifp->altsetting[j];

			if (as->extra) {
				free(as->extra);
				as->extra = NULL;
			}

			if (!as->endpoint)
				break;

			for (k = 0; k < as->bNumEndpoints; k++) {
				if (as->endpoint[k].extra) {
					free(as->endpoint[k].extra);
					as->endpoint[k].extra = NULL;
				}
			}
			free(as->endpoint);
			as->endpoint = NULL;
		}

		free(ifp->altsetting);
		ifp->altsetting = NULL;
	}

	free(cf->interface);
	cf->interface = NULL;
}
