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
	int			icc_proto[IFD_MAX_SLOTS];
} kaan_status_t;

typedef struct kaan_apdu {
	unsigned char		cse, cla, ins, p1, p2, lc, le;
	unsigned int		sw;
	void *			snd_buf;
	void *			rcv_buf;
	size_t			snd_len, rcv_len;
} kaan_apdu_t;

static int		kaan_reset_ct(ifd_reader_t *reader);
static int		kaan_get_units(ifd_reader_t *reader);
static int		kaan_display(ifd_reader_t *, const char *);
static void		kaan_apdu_init(kaan_apdu_t *,
				unsigned char cse,
				unsigned char cla,
				unsigned char ins,
				unsigned char p1,
				unsigned char p2);
static int		kaan_apdu_xcv(ifd_reader_t *,
				kaan_apdu_t *);
static int		kaan_get_tlv(kaan_apdu_t *,
				unsigned char tag,
				unsigned char **ptr);

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
	reader->slot[0].dad = 0x02;
	reader->slot[1].dad = 0x32;

	if (!(dev = ifd_device_open(device_name)))
		return -1;

	if (ifd_device_type(dev) == IFD_DEVICE_TYPE_SERIAL
	 && ifd_device_get_parameters(dev, &params) >= 0) {
		/* The default configuration for Kobil serial
		 * readers is 8E1 at 9600bps */
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

	/* Reset the CT */
	if (kaan_reset_ct(reader) < 0)
		return -1;

	/* Get list of functional units */
	if (kaan_get_units(reader) < 0)
		return -1;

	/* Clear the display */
	if (kaan_display(reader, "Kublai Kaan 0.1\nWelcome!") < 0)
		return -1;

	return 0;
}

/*
 * Reset the card reader
 */
int
kaan_reset_ct(ifd_reader_t *reader)
{
	kaan_apdu_t apdu;

	kaan_apdu_init(&apdu, IFD_APDU_CASE_1, 0x20, 0x10, 0x00, 0x00);
	return kaan_apdu_xcv(reader, &apdu);
}

/*
 * Get functional units
 */
int
kaan_get_units(ifd_reader_t *reader)
{
	unsigned char buffer[16], *units;
	kaan_apdu_t apdu;
	int n;

	kaan_apdu_init(&apdu, IFD_APDU_CASE_2S, 0x20, 0x13, 0x00, 0x81);
	apdu.rcv_buf = buffer;
	apdu.rcv_len = sizeof(buffer);
	apdu.le = 0;
	n = kaan_apdu_xcv(reader, &apdu);
	if (n < 0)
		return n;

	if ((n = kaan_get_tlv(&apdu, 0x81, &units)) < 0)
		return 0;

	while (n--) {
		switch (units[n]) {
		case 0x01: /* ICC1 */
			break;
		case 0x02: /* ICC2 */
			reader->nslots = 2;
			break;
		case 0x40: /* Display */
			reader->flags |= IFD_READER_KEYPAD;
			break;
		case 0x50: /* Display */
			reader->flags |= IFD_READER_DISPLAY;
			break;
		}
	}

	return 0;
}

/*
 * Output a string to the display
 */
int
kaan_display(ifd_reader_t *reader, const char *string)
{
	unsigned char	data[256];
	kaan_apdu_t	apdu;
	int		len = 0;

	if (!(reader->flags & IFD_READER_DISPLAY))
		return 0;

	kaan_apdu_init(&apdu, IFD_APDU_CASE_3S, 0x20, 0x17, 0x40, 00);
	if ((len = string? strlen(string) : 0) > 32)
		len = 32;
	data[0] = 0x50;
	data[1] = len;
	memcpy(data+2, string, len);

	apdu.snd_buf = data;
	apdu.snd_len = apdu.lc = len + 2;

	return kaan_apdu_xcv(reader, &apdu);
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
	unsigned char	buffer[6], *byte;
	kaan_apdu_t	apdu;
	int		n;

	DEBUG("slot=%d", slot);

	kaan_apdu_init(&apdu, IFD_APDU_CASE_2S, 0x20, 0x13, slot+1, 0x80);
	apdu.rcv_buf = &buffer;
	apdu.rcv_len = sizeof(buffer);
	if (kaan_apdu_xcv(reader, &apdu) < 0)
		return -1;
	if ((n = kaan_get_tlv(&apdu, 0x80, &byte)) >= 0) {
		if (*byte & 0x01)
			*status |= IFD_CARD_PRESENT;
	}
	return 0;
}

/*
 * Reset card and get ATR
 */
static int
kaan_card_reset(ifd_reader_t *reader, int slot, void *result, size_t size)
{
	unsigned char	buffer[64];
	kaan_apdu_t	apdu;
	int		n;

	kaan_apdu_init(&apdu, IFD_APDU_CASE_2S, 0x20, 0x10, slot+1, 0x01);
	apdu.rcv_buf = &buffer;
	apdu.rcv_len = sizeof(buffer);
	if ((n = kaan_apdu_xcv(reader, &apdu)) < 0)
		return -1;

	if (apdu.sw != 0x9000 && apdu.sw != 0x9001) {
		ifd_error("unable to reset card, SW=%04x", apdu.sw);
		return -1;
	}

	if ((unsigned int) n > size)
		n = size;
	memcpy(result, buffer, n);

	return n;
}

/*
 * Select a protocol for communication with the ICC.
 * Note that we continue to communicate with the terminal
 * using T=1; internally, the terminal talks to the
 * card using whatever protocol we selected.
 */
static int
kaan_set_protocol(ifd_reader_t *reader, int nslot, int proto)
{
	kaan_status_t	*st = (kaan_status_t *) reader->driver_data;
	unsigned char	cmd[] = { 0x22, 0x01, 0x00 };
	ifd_slot_t	*slot;
	kaan_apdu_t	apdu;
	int		n;

	DEBUG("proto=%d", proto);

	switch (proto) {
	case IFD_PROTOCOL_T0: cmd[2]    = 0x01; break;
	case IFD_PROTOCOL_T1: cmd[2]    = 0x02; break;
	case IFD_PROTOCOL_I2C: cmd[2]   = 0x80; break;
	case IFD_PROTOCOL_3WIRE: cmd[2] = 0x81; break;
	case IFD_PROTOCOL_2WIRE: cmd[2] = 0x82; break;
	default:
		DEBUG("kaan_set_protocol: protocol %d not supported", proto);
		return -1;
	}

	kaan_apdu_init(&apdu, IFD_APDU_CASE_3S, 0x80, 0x60, nslot+1, 0x00);
	apdu.snd_buf = cmd;
	apdu.snd_len = apdu.lc = sizeof(cmd);

	if ((n = kaan_apdu_xcv(reader, &apdu)) < 0)
		return n;

	if (apdu.sw != 0x9000) {
		ifd_error("%s: unable to select protocol, SW=0x%04x",
				reader->name, apdu.sw);
		return -1;
	}

	slot = &reader->slot[nslot];
	slot->proto = ifd_protocol_new(IFD_PROTOCOL_TRANSPARENT,
				reader, slot->dad);
	if (slot->proto == NULL) {
		ifd_error("%s: internal error", reader->name);
		return -1;
	}

	st->icc_proto[nslot] = proto;
	return 0;
}

/*
 * APDU exchange with ICC
 */
static int
kaan_transparent(ifd_reader_t *reader, int dad, ifd_apdu_t *apdu)
{
	kaan_status_t	*st = (kaan_status_t *) reader->driver_data;
	ifd_apdu_t	tpdu = *apdu;
	int		nslot, cse, n, prot;

	nslot = (dad == 0x02)? 0 : 1;
	prot = st->icc_proto[nslot];

	cse = ifd_apdu_case(apdu, NULL, NULL);
	if (prot == IFD_PROTOCOL_T0) {
		if (cse == IFD_APDU_CASE_4S)
			tpdu.snd_len--;
	}

	if ((n = ifd_protocol_transceive(st->p, dad, &tpdu)) < 2) {
		ifd_error("kaan: T=1 protocol failure, rc=%d", n);
		return -1;
	}

	if (cse == IFD_APDU_CASE_4S && n == 2) {
		unsigned char	*sw = tpdu.rcv_buf;
		unsigned char	cmd[5];

		if (sw[0] == 0x61 && sw[1]) {
			cmd[0] = ((unsigned char *) tpdu.snd_buf)[0];
			cmd[1] = 0xC0;
			cmd[2] = 0x00;
			cmd[3] = 0x00;
			cmd[4] = sw[1];

			tpdu.snd_buf = cmd;
			tpdu.snd_len = 5;
			tpdu.rcv_len = sw[1] + 2;

			if ((n = ifd_protocol_transceive(st->p, dad, &tpdu)) < 2) {
				ifd_error("kaan: T=1 protocol failure, rc=%d", n);
				return -1;
			}
		}
	}

	apdu->rcv_len = tpdu.rcv_len;
	return tpdu.rcv_len;
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

	if (IFD_APDU_CASE_LC(apdu->cse)) {
		sbuf[n++] = apdu->lc;
		if (n + apdu->lc >= sizeof(sbuf)) {
			ifd_error("kaan_apdu_xcv: buffer too small");
			return -1;
		}
		memcpy(sbuf + n, apdu->snd_buf, apdu->lc);
		n += apdu->lc;
	}

	if (IFD_APDU_CASE_LE(apdu->cse))
		sbuf[n++] = apdu->le;

	tpdu.snd_buf = sbuf;
	tpdu.snd_len = n;
	tpdu.rcv_buf = sbuf;
	tpdu.rcv_len = sizeof(sbuf);

	if ((rc = ifd_protocol_transceive(st->p, 0x12, &tpdu)) < 0
	 || rc < 2 || rc - 2 > apdu->rcv_len) {
		ifd_error("kaan: T=1 protocol failure, rc=%d", rc);
		return -1;
	}

	apdu->sw = (sbuf[rc-2] << 8) | sbuf[rc-1];
	memcpy(apdu->rcv_buf, sbuf, rc - 2);
	apdu->rcv_len = rc - 2;

	return rc - 2;
}

/*
 * Send/receive T=1 apdu
 * This is just for the communication with the card reader.
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
 * Extract data from TLV encoded result
 */
int
kaan_get_tlv(kaan_apdu_t *apdu, unsigned char tag, unsigned char **res)
{
	unsigned char *p = apdu->rcv_buf;
	unsigned int n, len;

	n = apdu->rcv_len;
	while (n >= 2) {
		len = p[1];
		if (len + 2 > n)
			break;
		if (p[0] == tag) {
			*res = p + 2;
			return len;
		}
		p += len + 2;
		n -= len + 2;
	}
	return -1;
}

/*
 * build an ISO apdu
 */
void
kaan_apdu_init(kaan_apdu_t *apdu,
		unsigned char cse, unsigned char cla,
		unsigned char ins, unsigned char p1, unsigned char p2)
{
	memset(apdu, 0, sizeof(*apdu));
	apdu->cse = cse;
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
	.card_reset	= kaan_card_reset,
	.send		= kaan_send,
	.recv		= kaan_recv,
	.set_protocol	= kaan_set_protocol,
	.transparent	= kaan_transparent,
};

/*
 * Initialize this module
 */
void
ifd_init_module(void)
{
	ifd_driver_register("kaan", &kaan_driver);
}
