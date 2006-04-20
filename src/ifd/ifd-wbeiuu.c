/*
 * Driver for WB Electronics' Infinity USB Unlimited card readers
 *
 * Copyright (C) 2006, Juan Carlos Borrás <jcborras@gmail.com>
 */

#include "internal.h"
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/poll.h>

#define PHS_CONV_DIRECT	0
#define PHS_CONV_INDIRECT 1
#define TIMEOUT	1000

/* table for indirect to direct byte mode conversion */
static const uint8_t dir_conv_table[0x100] = {
	0xff, 0x7f, 0xbf, 0x3f, 0xdf, 0x5f, 0x9f, 0x1f,
	0xef, 0x6f, 0xaf, 0x2f, 0xcf, 0x4f, 0x8f, 0xf,
	0xf7, 0x77, 0xb7, 0x37, 0xd7, 0x57, 0x97, 0x17,
	0xe7, 0x67, 0xa7, 0x27, 0xc7, 0x47, 0x87, 0x7,
	0xfb, 0x7b, 0xbb, 0x3b, 0xdb, 0x5b, 0x9b, 0x1b,
	0xeb, 0x6b, 0xab, 0x2b, 0xcb, 0x4b, 0x8b, 0xb,
	0xf3, 0x73, 0xb3, 0x33, 0xd3, 0x53, 0x93, 0x13,
	0xe3, 0x63, 0xa3, 0x23, 0xc3, 0x43, 0x83, 0x3,
	0xfd, 0x7d, 0xbd, 0x3d, 0xdd, 0x5d, 0x9d, 0x1d,
	0xed, 0x6d, 0xad, 0x2d, 0xcd, 0x4d, 0x8d, 0xd,
	0xf5, 0x75, 0xb5, 0x35, 0xd5, 0x55, 0x95, 0x15,
	0xe5, 0x65, 0xa5, 0x25, 0xc5, 0x45, 0x85, 0x5,
	0xf9, 0x79, 0xb9, 0x39, 0xd9, 0x59, 0x99, 0x19,
	0xe9, 0x69, 0xa9, 0x29, 0xc9, 0x49, 0x89, 0x9,
	0xf1, 0x71, 0xb1, 0x31, 0xd1, 0x51, 0x91, 0x11,
	0xe1, 0x61, 0xa1, 0x21, 0xc1, 0x41, 0x81, 0x1,
	0xfe, 0x7e, 0xbe, 0x3e, 0xde, 0x5e, 0x9e, 0x1e,
	0xee, 0x6e, 0xae, 0x2e, 0xce, 0x4e, 0x8e, 0xe,
	0xf6, 0x76, 0xb6, 0x36, 0xd6, 0x56, 0x96, 0x16,
	0xe6, 0x66, 0xa6, 0x26, 0xc6, 0x46, 0x86, 0x6,
	0xfa, 0x7a, 0xba, 0x3a, 0xda, 0x5a, 0x9a, 0x1a,
	0xea, 0x6a, 0xaa, 0x2a, 0xca, 0x4a, 0x8a, 0xa,
	0xf2, 0x72, 0xb2, 0x32, 0xd2, 0x52, 0x92, 0x12,
	0xe2, 0x62, 0xa2, 0x22, 0xc2, 0x42, 0x82, 0x2,
	0xfc, 0x7c, 0xbc, 0x3c, 0xdc, 0x5c, 0x9c, 0x1c,
	0xec, 0x6c, 0xac, 0x2c, 0xcc, 0x4c, 0x8c, 0xc,
	0xf4, 0x74, 0xb4, 0x34, 0xd4, 0x54, 0x94, 0x14,
	0xe4, 0x64, 0xa4, 0x24, 0xc4, 0x44, 0x84, 0x4,
	0xf8, 0x78, 0xb8, 0x38, 0xd8, 0x58, 0x98, 0x18,
	0xe8, 0x68, 0xa8, 0x28, 0xc8, 0x48, 0x88, 0x8,
	0xf0, 0x70, 0xb0, 0x30, 0xd0, 0x50, 0x90, 0x10,
	0xe0, 0x60, 0xa0, 0x20, 0xc0, 0x40, 0x80, 0x0
};

static int wbeiuu_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	char buf[8] = { 0x04, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x30 };

	reader->name = "WB Electronics Infinity USB Unlimited";
	reader->nslots = 2;
	if (!(dev = ifd_device_open(device_name)))
		return -1;

	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("wbeiuu: device %s is not a USB device", device_name);
		ifd_device_close(dev);
		return -1;
	}

	reader->device = dev;
	dev->timeout = 2000;

	ifd_debug(1, "%s:%d Checkpoint", __FILE__, __LINE__);
	if (ifd_usb_control(dev, 0x03, 0x02, 0x02, 0x00, NULL, 0, 1000) < 0) {
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		ifd_device_close(dev);
		return -1;
	}

	ifd_debug(1, "%s:%d Checkpoint", __FILE__, __LINE__);
	if (ifd_sysdep_usb_bulk(dev, 0x02, buf, 8, 5000) < 0) {
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		ifd_device_close(dev);
		return -1;
	}

	return 0;
}

static int wbeiuu_close(ifd_reader_t * reader)
{
	ifd_debug(1, "%s:%d wbeiuu_close()", __FILE__, __LINE__);
	ifd_device_close(reader->device);
	return 0;
}

static int wbeiuu_activate(ifd_reader_t * reader)
{
	char cmd[4];
	char product_name[17];
	char firmware_version[5];
	char loader_version[5];

	cmd[0] = 0x02;		// GET_PRODUCT_NAME
	if (ifd_sysdep_usb_bulk(reader->device, 0x02, &cmd, 1, 1000) < 0) {
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		ifd_device_close(reader->device);
		return -1;
	}

	if (ifd_sysdep_usb_bulk(reader->device, 0x82, &product_name, 16, 1000) <
	    0) {
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		ifd_device_close(reader->device);
		return -1;
	}
	product_name[16] = '\0';
	ifd_debug(1, "%s:%d Product Name: %s", __FILE__, __LINE__,
		  product_name);

	cmd[0] = 0x01;		// GET_FIRMWARE_VERSION
	if (ifd_sysdep_usb_bulk(reader->device, 0x02, &cmd, 1, 1000) < 0) {
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		ifd_device_close(reader->device);
		return -1;
	}

	if (ifd_sysdep_usb_bulk
	    (reader->device, 0x82, &firmware_version, 4, 1000) < 0) {
		ifd_device_close(reader->device);
		return -1;
	}
	firmware_version[4] = '\0';
	ifd_debug(1, "%s:%d Firmware_version: %s", __FILE__, __LINE__,
		  firmware_version);

	cmd[0] = 0x50;		// GET_LOADER_VERSION
	if (ifd_sysdep_usb_bulk(reader->device, 0x02, &cmd, 1, 1000) < 0) {
		ifd_device_close(reader->device);
		return -1;
	}

	if (ifd_sysdep_usb_bulk(reader->device, 0x82, &loader_version, 4, 1000)
	    < 0) {
		ifd_device_close(reader->device);
		return -1;
	}
	loader_version[4] = '\0';
	ifd_debug(1, "%s:%d Loader version: %s", __FILE__, __LINE__,
		  loader_version);

	cmd[0] = 0x49;		// IUU_UART_ENABLE
	cmd[1] = 0x02;		// 9600  
	cmd[2] = 0x98;		// bps
	cmd[3] = 0x21;		// one stop bit, even parity

	if (ifd_sysdep_usb_bulk(reader->device, 0x02, &cmd, 4, 1000) < 0) {
		ifd_debug(1, "%s:%d Bailing Out", __FILE__, __LINE__);
		ifd_device_close(reader->device);
		return -1;
	}

	return 0;
}

static int wbeiuu_deactivate(ifd_reader_t * reader)
{
	unsigned char buf = 0x4A;	// IUU_UART_DISABLE

	ifd_debug(1, "%s:%d wbeiuu_deactivate()", __FILE__, __LINE__);

	if (ifd_sysdep_usb_bulk(reader->device, 0x02, &buf, 1, 1000) < 0) {
		ifd_device_close(reader->device);
		return -1;
	}

	return 0;
}

static int wbeiuu_change_parity(ifd_reader_t * reader, int parity)
{
	ifd_debug(1, "%s:%d wbeiuu_chage_parity()", __FILE__, __LINE__);
	return 0;
}

static int wbeiuu_change_speed(ifd_reader_t * reader, unsigned int speed)
{
	ifd_debug(1, "%s:%d wbeiuu_chage_speed()", __FILE__, __LINE__);
	return 0;
}

static int wbeiuu_card_reset(ifd_reader_t * reader, int slot, void *atr,
			     size_t atr_len)
{
	unsigned char buf[256];
	size_t len = 0x00;

	ifd_debug(1, "%s:%d wbeiuu_card_reset()", __FILE__, __LINE__);

	// Flushing (do we have to flush the uart too?)
	if (ifd_sysdep_usb_bulk(reader->device, 0x82, &buf, 256, 1000) < 0) {
		//ifd_device_close(reader->device);
		ifd_debug(1, "%s:%d Less than expected flushed.", __FILE__,
			  __LINE__);
		//return -1;
	}
	// Resetting card
	buf[0] = 0x52;		// IUU_RST_SET
	buf[1] = 0x06;		// IUU_DELAY_MS
	buf[2] = 0x0c;		// milliseconds
	buf[3] = 0x53;		// IUU_RST_CLEAR

	if (ifd_sysdep_usb_bulk(reader->device, 0x02, &buf, 4, 1000) < 0) {
		ifd_device_close(reader->device);
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		return -1;
	}
	usleep(100000);		// waiting for the IUU uart to be filled by the card

	buf[0] = 0x56;		// IUU_UART_RX;

	if (ifd_sysdep_usb_bulk(reader->device, 0x02, &buf, 1, 1000) < 0) {
		ifd_device_close(reader->device);
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		return -1;
	}

	if (ifd_sysdep_usb_bulk(reader->device, 0x82, &len, 1, 1000) < 0) {
		ifd_device_close(reader->device);
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		return -1;
	}

	if (ifd_sysdep_usb_bulk(reader->device, 0x82, &buf, len, 1000) < 0) {
		ifd_device_close(reader->device);
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		return -1;
	}

	memcpy(atr, buf, len);
	return len;
}

static int wbeiuu_card_status(ifd_reader_t * reader, int slot, int *status)
{
	char st = 0x03;		// IUU_GET_STATE_REGISTER

	ifd_debug(1, "%s:%d wbeiuu_card_status()", __FILE__, __LINE__);

	if (ifd_sysdep_usb_bulk(reader->device, 0x02, &st, 1, 1000) < 0) {
		ifd_device_close(reader->device);
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		return -1;
	}

	if (ifd_sysdep_usb_bulk(reader->device, 0x82, &st, 1, 1000) < 0) {
		ifd_device_close(reader->device);
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		return -1;
	}
	ifd_debug(1, "%s:%d Status register: %x", __FILE__, __LINE__, st);

	if (st == 0x01 || st == 0x04)
		*status = IFD_CARD_PRESENT;

	return 0;
}

static int wbeiuu_send(ifd_reader_t * reader, unsigned int dad,
		       const unsigned char *buffer, size_t len)
{
	unsigned char buf[3];

	ifd_debug(1, "%s:%d wbeiuu_send()", __FILE__, __LINE__);

	buf[0] = 0x5e;		// IUU_UART_ESC;
	buf[1] = 0x04;		// IUU_UART_TX;

	if (len > 255) {
		ifd_device_close(reader->device);
		ifd_debug(1, "%s:%d Bailing out: len>255 = %d", __FILE__,
			  __LINE__, len);
		return -1;
	}
	buf[2] = len;

	if (ifd_sysdep_usb_bulk(reader->device, 0x02, &buf, len, 1000) < 0) {
		ifd_device_close(reader->device);
		ifd_debug(1, "%s:%d Bailing out.", __FILE__, __LINE__);
		return -1;
	}

	if (ifd_sysdep_usb_bulk(reader->device, 0x02, &buffer, len, 1000) < 0) {
		ifd_device_close(reader->device);
		ifd_debug(1, "%s:%d Bailing out.", __FILE__, __LINE__);
		return -1;
	}

	return 0;
}

static int wbeiuu_recv(ifd_reader_t * reader, unsigned int dad,
		       unsigned char *buffer, size_t len, long timeout)
{
	ifd_debug(1, "%s:%d wbeiuu_recv()", __FILE__, __LINE__);
	return 0;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops wbeiuu_driver;

void ifd_wbeiuu_register(void)
{
	wbeiuu_driver.open = wbeiuu_open;
	wbeiuu_driver.close = wbeiuu_close;
	wbeiuu_driver.activate = wbeiuu_activate;
	wbeiuu_driver.deactivate = wbeiuu_deactivate;
	wbeiuu_driver.card_reset = wbeiuu_card_reset;
	wbeiuu_driver.card_status = wbeiuu_card_status;
	wbeiuu_driver.change_parity = wbeiuu_change_parity;
	wbeiuu_driver.change_speed = wbeiuu_change_speed;
	wbeiuu_driver.send = wbeiuu_send;
	wbeiuu_driver.recv = wbeiuu_recv;
	ifd_driver_register("wbeiuu", &wbeiuu_driver);
}
