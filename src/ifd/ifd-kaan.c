/*
 * Driver for Kobil Kaan Professional
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <string.h>
#include <ifd/core.h>
#include <ifd/driver.h>
#include <ifd/device.h>
#include <ifd/logging.h>
#include <ifd/config.h>
#include <ifd/error.h>

#define DEBUG(fmt, args...) \
	do { ifd_debug("%s: " fmt, __FUNCTION__ , ##args); } while (0)

/*
 * CT status
 */
typedef struct kaan_status {
	ifd_protocol_t *	p;
} kaan_status_t;

typedef struct kaan_apdu {
	unsigned char		cla, ins, p1, p2, lc, le, sw1, sw2;
	void *			snd_buf;
	void *			rcv_buf;
	size_t			snd_len, rcv_len;
} kaan_apdu_t;

static void		kaan_apdu_init(kaan_apdu_t *,
				unsigned char cla,
				unsigned char ins,
				unsigned char p1,
				unsigned char p2);
static int		kaan_apdu_xcv(ifd_reader_t *,
				kaan_apdu_t *);

/*
 * Initialize the device
 */
static int
kaan_open(ifd_reader_t *reader, const char *device_name)
{
	kaan_status_t *st;
	ifd_device_t *dev;
	ifd_device_params_t params;

	reader->name = "Kobil Kaan PRO";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;

	if (ifd_device_type(dev) == IFD_DEVICE_TYPE_SERIAL
	 && ifd_device_get_parameters(dev, &params) >= 0) {
		params.serial.bits = 8;
		params.serial.parity = IFD_SERIAL_PARITY_EVEN;
		params.serial.stopbits = 1;
		ifd_device_set_parameters(dev, &params);
	}

	reader->device = dev;
	if ((st = (kaan_status_t *) calloc(1, sizeof(*st))) == NULL)
		return -1;

	reader->driver_data = st;
	if (!(st->p = ifd_protocol_new(IFD_PROTOCOL_T1, reader, 0x12))) {
		ifd_error("unable to get T1 protocol handler");
		return -1;
	}

	ifd_protocol_set_parameter(st->p, IFD_PROTOCOL_T1_RESYNCH, NULL);
	return 0;
}

/*
 * Power up the reader
 */
static int
kaan_activate(ifd_reader_t *reader)
{
	DEBUG("called.");
	return 0;
}

static int
kaan_deactivate(ifd_reader_t *reader)
{
	DEBUG("called.");
	return 0;
}

/*
 * Get the card status
 */
static int
kaan_card_status(ifd_reader_t *reader, int slot, int *status)
{
	kaan_apdu_t	apdu;
	unsigned char	byte;

	DEBUG("slot=%d", slot);

	kaan_apdu_init(&apdu, 0x20, 0x13, slot+1, 0x80);
	apdu.le = apdu.rcv_len = 1;
	apdu.rcv_buf = &byte;
	if (kaan_apdu_xcv(reader, &apdu) < 0)
		return -1;
	if (byte & 0x01)
		*status |= IFD_CARD_PRESENT;
	return 0;
}

/*
 * Send/receive T=1 apdu
 */
static int
kaan_send(ifd_reader_t *reader, unsigned int dad, const void *buffer, size_t len)
{
	return ifd_device_send(reader->device, buffer, len);
}

static int
kaan_recv(ifd_reader_t *reader, unsigned int dad, void *buffer, size_t len, long timeout)
{
	return ifd_device_recv(reader->device, buffer, len, timeout);
}

/*
 * APDU exchange with terminal
 */
int
kaan_apdu_xcv(ifd_reader_t *reader, kaan_apdu_t *apdu)
{
	kaan_status_t	*st = (kaan_status_t *) reader->driver_data;
	ifd_apdu_t	tpdu;
	unsigned char	sbuf[256+6];
	unsigned int	n = 0;
	int		rc;

	if (apdu->lc > apdu->snd_len)
		return -1;

	sbuf[n++] = apdu->cla;
	sbuf[n++] = apdu->ins;
	sbuf[n++] = apdu->p1;
	sbuf[n++] = apdu->p2;

	if (apdu->lc) {
		sbuf[n++] = apdu->lc;
		if (n + apdu->lc >= sizeof(sbuf)) {
			ifd_error("kaan_apdu_xcv: buffer too small");
			return -1;
		}
		memcpy(apdu + n, apdu->snd_buf, apdu->lc);
		n += apdu->lc;
	}

	/*
	if (apdu->le)
		sbuf[n++] = apdu->le;
	 */

	tpdu.snd_buf = sbuf;
	tpdu.snd_len = n;
	tpdu.rcv_buf = sbuf;
	tpdu.rcv_len = apdu->rcv_len <= 256? apdu->rcv_len : 256;

	if ((rc = ifd_protocol_transceive(st->p, 0x12, &tpdu)) < 0
	 || rc < 2 || rc - 2 > apdu->rcv_len) {
		ifd_error("kaan: T=1 protocol failure, rc=%d", rc);
		return -1;
	}

	apdu->sw1 = sbuf[0];
	apdu->sw2 = sbuf[1];
	memcpy(apdu->rcv_buf, sbuf + 2, rc - 2);
	apdu->rcv_len = rc - 2;

	return 0;
}

/*
 * build an ISO apdu
 */
void
kaan_apdu_init(kaan_apdu_t *apdu, unsigned char cla,
		unsigned char ins, unsigned char p1, unsigned char p2)
{
	memset(apdu, 0, sizeof(*apdu));
	apdu->cla = cla;
	apdu->ins = ins;
	apdu->p1  = p1;
	apdu->p2  = p2;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops	kaan_driver = {
	.open		= kaan_open,
	.activate	= kaan_activate,
	.deactivate	= kaan_deactivate,
	.card_status	= kaan_card_status,
	.send		= kaan_send,
	.recv		= kaan_recv,
};

/*
 * Initialize this module
 */
void
ifd_init_module(void)
{
	ifd_driver_register("kaan", &kaan_driver);
}
