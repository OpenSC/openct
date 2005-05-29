/*
 * USB Descriptor parsing functions
 *
 * Copyright (c) 2000-2001 Johannes Erdfelt <johannes@erdfelt.com>
 * Copyright 2003, Chaskiel Grundman <cg2v@andrew.cmu.edu>
 *
 * This header file is purely internal to OpenCT.
 */
#ifndef __USB_DESCRIPTORS_H__
#define __USB_DESCRIPTORS_H__

#include <openct/types.h>

/*
 * Descriptor types
 */
#define IFD_USB_DT_DEVICE                   0x01
#define IFD_USB_DT_CONFIG                   0x02
#define IFD_USB_DT_STRING                   0x03
#define IFD_USB_DT_INTERFACE                0x04
#define IFD_USB_DT_ENDPOINT                 0x05

/*
 * Descriptor sizes per descriptor type
 */
#define IFD_USB_DT_DEVICE_SIZE              18
#define IFD_USB_DT_CONFIG_SIZE              9
#define IFD_USB_DT_INTERFACE_SIZE           9
#define IFD_USB_DT_ENDPOINT_SIZE            7
#define IFD_USB_DT_ENDPOINT_AUDIO_SIZE      9	/* Audio extension */
#define IFD_USB_DT_HUB_NONVAR_SIZE          7

struct ifd_usb_descriptor_header {
	uint8_t bLength;
	uint8_t bDescriptorType;
};

/* String descriptor */
struct ifd_usb_string_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wData[1];
};

/* Endpoint descriptor */
#define IFD_USB_MAXENDPOINTS        32
struct ifd_usb_endpoint_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
	uint8_t bRefresh;
	uint8_t bSynchAddress;

	unsigned char *extra;	/* Extra descriptors */
	int extralen;
};
#define IFD_USB_ENDPOINT_ADDRESS_MASK       0x0f	/* in bEndpointAddress */
#define IFD_USB_ENDPOINT_DIR_MASK           0x80

#define IFD_USB_ENDPOINT_TYPE_MASK          0x03	/* in bmAttributes */
#define IFD_USB_ENDPOINT_TYPE_CONTROL       0
#define IFD_USB_ENDPOINT_TYPE_ISOCHRONOUS   1
#define IFD_USB_ENDPOINT_TYPE_BULK          2
#define IFD_USB_ENDPOINT_TYPE_INTERRUPT     3

/* Interface descriptor */
#define IFD_USB_MAXINTERFACES       32
struct ifd_usb_interface_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;

	struct ifd_usb_endpoint_descriptor *endpoint;

	unsigned char *extra;	/* Extra descriptors */
	int extralen;
};

#define IFD_USB_MAXALTSETTING       128	/* Hard limit */
struct ifd_usb_interface {
	struct ifd_usb_interface_descriptor *altsetting;

	int num_altsetting;
};

/* Configuration descriptor information.. */
#define IFD_USB_MAXCONFIG           8
struct ifd_usb_config_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
	uint8_t bmAttributes;
	uint8_t MaxPower;

	struct ifd_usb_interface *interface;

	unsigned char *extra;	/* Extra descriptors */
	int extralen;
};

/* Device descriptor */
struct ifd_usb_device_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
};
#define IFD_USB_REQ_GET_DESCRIPTOR          0x06

#define IFD_USB_TYPE_STANDARD               (0x00 << 5)
#define IFD_USB_TYPE_CLASS                  (0x01 << 5)
#define IFD_USB_TYPE_VENDOR                 (0x02 << 5)
#define IFD_USB_TYPE_RESERVED               (0x03 << 5)

#define IFD_USB_RECIP_DEVICE                0x00
#define IFD_USB_RECIP_INTERFACE             0x01
#define IFD_USB_RECIP_ENDPOINT              0x02
#define IFD_USB_RECIP_OTHER                 0x03

#define IFD_USB_ENDPOINT_IN                 0x80
#define IFD_USB_ENDPOINT_OUT                0x00

#define IFD_USB_LE16_TO_CPU(x) do { unsigned char *y=(unsigned char *)(&x); x = (y[1]  << 8) | y[0] ; } while(0)

extern int ifd_usb_get_device(ifd_device_t * dev,
			      struct ifd_usb_device_descriptor *d);

extern int ifd_usb_get_config(ifd_device_t * dev,
			      int n, struct ifd_usb_config_descriptor *c);
extern void ifd_usb_free_configuration(struct ifd_usb_config_descriptor *c);

#endif
