/*
 * OpenCT driver for ePass3000 device
 *
 * Copyright (C) 2008, EnterSafe <jingmin@FTsafe.com>
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
	unsigned char TagH;
	unsigned char TagL;
	unsigned char CommandH;
	unsigned char CommandL;
	unsigned char LengthH;
	unsigned char LengthL;
	unsigned char Value[1];
} epass3k_command_t;

typedef struct {
	unsigned char TagH;
	unsigned char TagL;
	unsigned char StatusH;
	unsigned char StatusL;
	unsigned char LengthH;
	unsigned char LengthL;
	unsigned char Value[1];
} epass3k_status_t;

#define TIMEOUT				200000
#define USB_BULK_IN 			0x81
#define USB_BULK_OUT 			2
#define EPASS3K_COMMAND_SIZE		7
#define EPASS3K_STATUS_SIZE		7
#define TOKEN_TYPE_ID_LENGTH		64
#define EPASS3K_COMMAND_GET_ATR		(unsigned char)0X01
#define EPASS3K_COMMAND_TRANSMIT_APDU	(unsigned char)0X02

/*
 * Initialize the device
 */
static int epass3k_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	ifd_device_params_t params;
	ifd_debug(1, "%s:%d epass3k_open()", __FILE__, __LINE__);

	reader->name = "FT SCR2000A";	/* ePass3000 reader name */
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;

	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("ePass3000: device %s is not a USB device",
			 device_name);
		ifd_device_close(dev);
		return -1;
	}

	params = dev->settings;
	params.usb.ep_o = USB_BULK_OUT;
	params.usb.ep_i = USB_BULK_IN;
	if (ifd_device_set_parameters(dev, &params) < 0) {
		ct_error("ePass3000: setting parameters failed", device_name);
		ifd_device_close(dev);
		return -1;
	}

	reader->device = dev;
	dev->timeout = TIMEOUT;

	return 0;
}

static int epass3k_activate(ifd_reader_t * reader)
{
	ifd_debug(1, "%s:%d epass3k_activate()", __FILE__, __LINE__);
	return 0;
}

static int epass3k_deactivate(ifd_reader_t * reader)
{
	ifd_debug(1, "%s:%d epass3k_deactivate()", __FILE__, __LINE__);
	return -1;
}

static int epass3k_change_parity(ifd_reader_t * reader, int parity)
{
	ifd_debug(1, "%s:%d epass3k_change_parity()", __FILE__, __LINE__);
	return 0;
}

static int epass3k_change_speed(ifd_reader_t * reader, unsigned int speed)
{
	ifd_debug(1, "%s:%d epass3k_change_speed()", __FILE__, __LINE__);
	return 0;
}

static int epass3k_set_protocol(ifd_reader_t * reader, int nslot, int proto)
{
	ifd_debug(1, "%s:%d epass3k_set_protocol()", __FILE__, __LINE__);
	ifd_protocol_t *protocol;
	ifd_slot_t *slot;

	if (IFD_PROTOCOL_T0 != proto)
		return IFD_ERROR_NOT_SUPPORTED;

	slot = &reader->slot[nslot];

	slot->proto = ifd_protocol_new(proto, reader, slot->dad);
	if (slot->proto == NULL) {
		ct_error("acr: unable to create protocol");
		return -1;
	}
	ifd_protocol_set_parameter(slot->proto, IFD_PROTOCOL_BLOCK_ORIENTED, 1);
	return 1;
}

static int epass3k_card_reset(ifd_reader_t * reader, int slot, void *atr,
			      size_t atr_len)
{
	int ret;
	epass3k_command_t *pepass3k_send = NULL;
	epass3k_status_t *pepass3k_receive = NULL;

	ifd_debug(1, "%s:%d epass3k_card_reset()", __FILE__, __LINE__);
	pepass3k_send = (epass3k_command_t *) malloc(EPASS3K_COMMAND_SIZE);
	pepass3k_receive =
	    (epass3k_status_t *) malloc(EPASS3K_STATUS_SIZE +
					TOKEN_TYPE_ID_LENGTH);
	if ((NULL == pepass3k_send) || (NULL == pepass3k_receive)) {
		return -1;
	}

	memset(pepass3k_send, 0, EPASS3K_COMMAND_SIZE);
	pepass3k_send->TagH = 'R';
	pepass3k_send->TagL = '6';
	pepass3k_send->CommandH = 0x00;
	pepass3k_send->CommandL = EPASS3K_COMMAND_GET_ATR;
	pepass3k_send->LengthH = 0x00;
	pepass3k_send->LengthL = 0x00;
	ret =
	    ifd_device_send(reader->device, (unsigned char *)pepass3k_send,
			    EPASS3K_COMMAND_SIZE - 1);
	if (ret != EPASS3K_COMMAND_SIZE - 1) {
		free(pepass3k_send);
		pepass3k_send = NULL;
		free(pepass3k_receive);
		pepass3k_receive = NULL;
		return -1;
	}

	memset(pepass3k_receive, 0, EPASS3K_STATUS_SIZE + TOKEN_TYPE_ID_LENGTH);
	pepass3k_receive->TagH = 'R';
	pepass3k_receive->TagL = '6';
	pepass3k_receive->LengthH = 0x00;
	pepass3k_receive->LengthL = 0x20;
	ret =
	    ifd_device_recv(reader->device, (unsigned char *)pepass3k_receive,
			    EPASS3K_STATUS_SIZE + TOKEN_TYPE_ID_LENGTH,
			    TIMEOUT);
	if (ret < EPASS3K_STATUS_SIZE) {
		free(pepass3k_send);
		pepass3k_send = NULL;
		free(pepass3k_receive);
		pepass3k_receive = NULL;
		return -1;
	}

	if (atr_len <
	    pepass3k_receive->LengthH * 256 + pepass3k_receive->LengthL - 5) {
		free(pepass3k_send);
		pepass3k_send = NULL;
		free(pepass3k_receive);
		pepass3k_receive = NULL;
		return -1;
	}
	if (NULL != atr) {
		memcpy(atr, pepass3k_receive->Value + 2,
		       pepass3k_receive->LengthH * 256 +
		       pepass3k_receive->LengthL - 5);
	}
	ret = pepass3k_receive->LengthH * 256 + pepass3k_receive->LengthL - 5;

	free(pepass3k_send);
	pepass3k_send = NULL;
	free(pepass3k_receive);
	pepass3k_receive = NULL;

	return ret;
}

static int epass3k_card_status(ifd_reader_t * reader, int slot, int *status)
{
	ifd_debug(1, "%s:%d epass3k_card_status()", __FILE__, __LINE__);
	*status = IFD_CARD_PRESENT;
	return 0;
}

static int epass3k_send(ifd_reader_t * reader, unsigned int dad,
			const unsigned char *buffer, size_t len)
{
	int ret = 0;
	ifd_debug(1, "%s:%d epass3k_send()", __FILE__, __LINE__);

	epass3k_command_t *pepass3k_send = NULL;
	pepass3k_send =
	    (epass3k_command_t *) malloc(EPASS3K_COMMAND_SIZE + len);
	if (NULL == pepass3k_send) {
		return -1;
	}
	memset(pepass3k_send, 0, EPASS3K_COMMAND_SIZE + len);
	pepass3k_send->TagH = 'R';
	pepass3k_send->TagL = '6';
	pepass3k_send->CommandH = 0x00;
	pepass3k_send->CommandL = EPASS3K_COMMAND_TRANSMIT_APDU;
	pepass3k_send->LengthH = len / 256;
	pepass3k_send->LengthL = len % 256;
	memcpy(pepass3k_send->Value, buffer, len);

	ret =
	    ifd_device_send(reader->device, (unsigned char *)pepass3k_send,
			    EPASS3K_COMMAND_SIZE + len - 1);

	if (NULL != pepass3k_send) ;
	{
		free(pepass3k_send);
		pepass3k_send = NULL;
	}

	if (ret != EPASS3K_COMMAND_SIZE + len - 1) {
		return -1;
	}
	return 0;
}

static int epass3k_recv(ifd_reader_t * reader, unsigned int dad,
			unsigned char *buffer, size_t len, long timeout)
{
	int ret = 0;
	ifd_debug(1, "%s:%d epass3k_recv()", __FILE__, __LINE__);
	epass3k_status_t *pepass3k_receive = NULL;
	pepass3k_receive =
	    (epass3k_status_t *) malloc(EPASS3K_STATUS_SIZE + len);
	memset(pepass3k_receive, 0, EPASS3K_STATUS_SIZE + len);
	if (NULL == pepass3k_receive) {
		return -1;
	}
	memset(pepass3k_receive, 0, EPASS3K_STATUS_SIZE + len);
	pepass3k_receive->TagH = 'R';
	pepass3k_receive->TagL = '6';
	pepass3k_receive->LengthH = len / 256;
	pepass3k_receive->LengthL = len % 256;
	ret =
	    ifd_device_recv(reader->device, (unsigned char *)pepass3k_receive,
			    EPASS3K_STATUS_SIZE + len, TIMEOUT);

	if (ret < EPASS3K_STATUS_SIZE) {
		if (pepass3k_receive)
			free(pepass3k_receive);
		return -1;
	}
	if ((NULL != buffer)
	    && (pepass3k_receive->LengthH * 256 + pepass3k_receive->LengthL <=
		len)) {
		memcpy(buffer, pepass3k_receive->Value,
		       pepass3k_receive->LengthH * 256 +
		       pepass3k_receive->LengthL);
	}

	ret = pepass3k_receive->LengthH * 256 + pepass3k_receive->LengthL;
	if (pepass3k_receive)
		free(pepass3k_receive);

	return ret;
}

static struct ifd_driver_ops epass3k_driver;

void ifd_epass3k_register(void)
{
	epass3k_driver.open = epass3k_open;
	epass3k_driver.activate = epass3k_activate;
	epass3k_driver.deactivate = epass3k_deactivate;
	epass3k_driver.card_reset = epass3k_card_reset;
	epass3k_driver.card_status = epass3k_card_status;
	epass3k_driver.change_parity = epass3k_change_parity;
	epass3k_driver.change_speed = epass3k_change_speed;
	epass3k_driver.send = epass3k_send;
	epass3k_driver.recv = epass3k_recv;
	epass3k_driver.set_protocol = epass3k_set_protocol;
	ifd_driver_register("ePass3000", &epass3k_driver);
}
