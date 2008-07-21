/*
 * Eutron Crypto Idendity IT-Sec driver
 *
 * Copyright (C) 2003, Andreas Jellinghaus <aj@dungeon.inka.de>
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 * Copyright 2006, Chaskiel Grundman <cg2v@andrew.cmu.edu>
 */

#include "internal.h"
#include "atr.h"
#include "usb-descriptors.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define EUTRON_OUT IFD_USB_ENDPOINT_OUT | IFD_USB_TYPE_VENDOR | IFD_USB_RECIP_ENDPOINT
#define EUTRON_IN IFD_USB_ENDPOINT_IN | IFD_USB_TYPE_VENDOR | IFD_USB_RECIP_ENDPOINT

#define EUTRON_CMD_WRITE 0x1
#define EUTRON_CMD_READ 0x2
#define EUTRON_CMD_ATR 0x9
#define EUTRON_CMD_SETPARAM 0x65

typedef struct eut_priv {
	unsigned char readbuffer[500];
	int head;
	int tail;
} eut_priv_t;
/*
 * Initialize the device
 */
static int eutron_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	eut_priv_t *priv;
	ifd_device_params_t params;

	reader->name = "Eutron CryptoIdendity";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("eutron: device %s is not a USB device", device_name);
		ifd_device_close(dev);
		return -1;
	}

	params = dev->settings;
	params.usb.interface = 0;
	if (ifd_device_set_parameters(dev, &params) < 0) {
		ct_error("eutron: setting parameters failed", device_name);
		ifd_device_close(dev);
		return -1;
	}

	priv = (eut_priv_t *) calloc(1, sizeof(eut_priv_t));
	if (!priv) {
		ct_error("out of memory");
		ifd_device_close(dev);
		return IFD_ERROR_NO_MEMORY;
	}

	reader->driver_data = priv;

	reader->device = dev;

	return 0;
}

/*
 * Power up the reader
 */
static int eutron_activate(ifd_reader_t * reader)
{
	return 0;
}

static int eutron_deactivate(ifd_reader_t * reader)
{
	return -1;
}

/*
 * Card status - always present
 */
static int eutron_card_status(ifd_reader_t * reader, int slot, int *status)
{
	*status = IFD_CARD_PRESENT;
	return 0;
}

/*
 * Reset - nothing to be done?
 * We should do something to make it come back with all state zapped
 */
static int eutron_card_reset(ifd_reader_t * reader, int slot, void *atr,
			     size_t size)
{
	ifd_device_t *dev = reader->device;
	unsigned char buffer[IFD_MAX_ATR_LEN + 100];
	int rc, lr, c, atrlen;

	if (ifd_usb_control(dev, EUTRON_OUT, 0xa3, 0, 0, NULL, 0, -1) != 0
	    || ifd_usb_control(dev, EUTRON_OUT, 0xa1, 0, 0, NULL, 0, -1) != 0
	    || ifd_usb_control(dev, EUTRON_OUT, 0xa2, 0, 0, NULL, 0, -1) != 0
	    || ifd_usb_control(dev, EUTRON_OUT, 0xa0, 0, 0, NULL, 0, -1) != 0)
		goto failed;
	/* flush any leftover buffered data */
	while (ifd_usb_control(dev, EUTRON_IN, EUTRON_CMD_READ, 0, 0,
			       buffer, IFD_MAX_ATR_LEN + 100, 1000) > 0) ;
	if (ifd_usb_control(dev, EUTRON_OUT, EUTRON_CMD_ATR, 0, 0, NULL, 0, -1)
	    != 0)
		goto failed;

	for (lr = 0, c = 0; c < 20; c++) {
		rc = ifd_usb_control(dev, EUTRON_IN, EUTRON_CMD_READ, 0, 0,
				     &buffer[lr], IFD_MAX_ATR_LEN - lr, 1000);

		if (rc < 0)
			goto failed;
		lr += rc;

		rc = ifd_atr_complete(buffer, lr);

		if (rc)
			break;

		if (lr > IFD_MAX_ATR_LEN)
			goto failed;
		usleep(100000);
	}
	if (c >= 20)
		goto failed;

	atrlen = lr;
	memcpy(atr, buffer, atrlen);

	return atrlen;

      failed:
	ct_error("eutron: failed to activate token");
	return -1;
}

/*
 * Send/receive routines
 */
static int eutron_send(ifd_reader_t * reader, unsigned int dad,
		       const unsigned char *buffer, size_t len)
{
	return ifd_usb_control(reader->device, EUTRON_OUT, EUTRON_CMD_WRITE, 0,
			       0, (void *)buffer, len, 1000);
}

static int eutron_recv(ifd_reader_t * reader, unsigned int dad,
		       unsigned char *buffer, size_t len, long timeout)
{
	int rc, c, rbs;
	eut_priv_t *priv = reader->driver_data;

	ct_debug("eutron_recv: len=%d", len);
	if (len <= priv->head - priv->tail) {
		memcpy(buffer, priv->readbuffer + priv->tail, len);
		priv->tail += len;
		ct_debug("eutron_recv: returning buffered data, %d bytes left",
			 priv->head - priv->tail);
		return len;
	}

	/* move the data to the beginning of the buffer, so there's a big
	 * contiguous chunk */
	memmove(priv->readbuffer, &priv->readbuffer[priv->tail],
	       priv->head - priv->tail);
	priv->head -= priv->tail;
	/* since we set tail=0 here, the rest of the function can ignore it */
	priv->tail = 0;
	for (c = 0; c < 30; c++) {
		rbs = 499 - priv->head;
		if (rbs == 0)
			break;

		rc = ifd_usb_control(reader->device, EUTRON_IN, EUTRON_CMD_READ,
				     0, 0, &priv->readbuffer[priv->head], rbs,
				     timeout);

		if (rc < 0)
			goto failed;
		priv->head += rc;

		if (priv->head >= len)
			break;
		usleep(100000);
	}
	if (len > priv->head)
		return -1;
	memcpy(buffer, priv->readbuffer, len);
	priv->tail += len;
	if (priv->head - priv->tail)
		ct_debug("eutron_recv: buffering %d bytes of data",
			 priv->head - priv->tail);

	return len;
      failed:
	ct_error("eutron: receive failed");
	return -1;
}

static int eutron_set_protocol(ifd_reader_t * reader, int nslot, int proto)
{
	ifd_slot_t *slot;
	ifd_atr_info_t atr_info;
	unsigned char pts[7], ptsret[7];
	int ptslen, ptsrlen, r, c, speedparam;

	slot = &reader->slot[nslot];
	if (proto != IFD_PROTOCOL_T0 && proto != IFD_PROTOCOL_T1) {
		ct_error("%s: protocol not supported", reader->name);
		return -1;
	}

	r = ifd_atr_parse(&atr_info, slot->atr, slot->atr_len);
	if (r < 0) {
		ct_error("%s: Bad ATR", reader->name);
		return r;
	}

	/* if the card supports T=1, prefer it, even if
	 * it is not the default protocol */
	if (atr_info.supported_protocols & 0x2) {
		proto = IFD_PROTOCOL_T1;
	}

	/* XXX disable baud change */
	atr_info.TA[0] = -1;
	/* ITSEC-P does not respond correctly to request with PTS2 present */
	atr_info.TC[0] = -1;

	ptslen = ifd_build_pts(&atr_info, proto, pts, 7);
	if (ptslen < 0) {
		return r;
	}
	if (eutron_send(reader, slot->dad, pts, ptslen) != ptslen)
		return IFD_ERROR_COMM_ERROR;

	for (ptsrlen = 0, c = 0; c < 20; c++) {
		r = ifd_usb_control(reader->device, EUTRON_IN, EUTRON_CMD_READ,
				    0, 0, &ptsret[ptsrlen],
				    sizeof(ptsret) - ptsrlen, 1000);

		if (r < 0)
			return IFD_ERROR_COMM_ERROR;
		ptsrlen += r;
		if (ifd_pts_complete(ptsret, ptsrlen))
			break;

		if (ptsrlen >= 7)
			return IFD_ERROR_COMM_ERROR;
		usleep(100000);
	}
	if (c >= 20)
		return IFD_ERROR_TIMEOUT;

	r = ifd_verify_pts(&atr_info, proto, ptsret, ptsrlen);
	if (r < 0) {
		ct_error("%s: Protocol selection failed", reader->name);
		return r;
	}

	if (atr_info.TA[0] != -1)
		speedparam = atr_info.TA[0];
	else
		speedparam = 1;
	if (ifd_usb_control
	    (reader->device, EUTRON_OUT, EUTRON_CMD_SETPARAM, speedparam, 0,
	     NULL, 0, -1) != 0
	    || ifd_usb_control(reader->device, EUTRON_OUT, 0xa1, 0, 0, NULL, 0,
			       -1) != 0
	    || ifd_usb_control(reader->device, EUTRON_OUT, 0xa0, 0, 0, NULL, 0,
			       -1) != 0)
		return IFD_ERROR_COMM_ERROR;

	slot->proto = ifd_protocol_new(proto, reader, slot->dad);
	if (slot->proto == NULL) {
		ct_error("%s: internal error", reader->name);
		return -1;
	}
	/* device is not guaranteed to return whole frames */
	ifd_protocol_set_parameter(slot->proto, IFD_PROTOCOL_BLOCK_ORIENTED, 0);
	/* Enable larger transfers */
	if (proto == IFD_PROTOCOL_T1 && atr_info.TA[2] != -1) {
		ifd_protocol_set_parameter(slot->proto, IFD_PROTOCOL_T1_IFSC,
					   atr_info.TA[2]);
		if (t1_negotiate_ifsd(slot->proto, slot->dad, atr_info.TA[2]) >
		    0)
			ifd_protocol_set_parameter(slot->proto,
						   IFD_PROTOCOL_T1_IFSD,
						   atr_info.TA[2]);

	}
	return 0;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops eutron_driver;

/*
 * Initialize this module
 */
void ifd_eutron_register(void)
{
	eutron_driver.open = eutron_open;
	eutron_driver.activate = eutron_activate;
	eutron_driver.deactivate = eutron_deactivate;
	eutron_driver.card_status = eutron_card_status;
	eutron_driver.card_reset = eutron_card_reset;
	eutron_driver.send = eutron_send;
	eutron_driver.recv = eutron_recv;
	eutron_driver.set_protocol = eutron_set_protocol;

	ifd_driver_register("eutron", &eutron_driver);
}
