/*
 * Driver for Kobil Kaan Professional
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <string.h>
#include "internal.h"

/*
 * CT status
 */
typedef struct kaan_status {
	ifd_protocol_t *	p;
	int			icc_proto[OPENCT_MAX_SLOTS];
} kaan_status_t;

static int		kaan_reset_ct(ifd_reader_t *reader);
static int		kaan_get_units(ifd_reader_t *reader);
static int		kaan_display(ifd_reader_t *, const char *);
static int		kaan_build_display_args(unsigned char *, size_t,
				unsigned int, const char *);
static int		kaan_apdu_xcv(ifd_reader_t *,
				const unsigned char *, size_t,
				unsigned char *, size_t,
				time_t);
static int		kaan_get_tlv(unsigned char *, size_t,
				unsigned char tag,
				unsigned char **ptr);
static int		kaan_check_sw(const char *,
				unsigned char *, int);
static int		kaan_get_sw(unsigned char *,
				unsigned int,
				unsigned short *);

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
		ct_error("unable to get T1 protocol handler");
		return -1;
	}

	ifd_protocol_set_parameter(st->p, IFD_PROTOCOL_T1_RESYNCH, 0);

	/* Reset the CT */
	if (kaan_reset_ct(reader) < 0)
		return -1;

	/* Get list of functional units */
	if (kaan_get_units(reader) < 0)
		return -1;

#if 0
	/* Clear the display */
	if (kaan_display(reader, "Kublai Kaan 0.1\nWelcome!") < 0)
		return -1;
#endif

	return 0;
}

/*
 * Reset the card reader
 */
int
kaan_reset_ct(ifd_reader_t *reader)
{
	unsigned char	cmd[] = { 0x20, 0x10, 0x00, 0x00 };
	unsigned char	sw[2];
	int		rc;

	rc = kaan_apdu_xcv(reader, cmd, sizeof(cmd), sw, sizeof(sw), 0);
	return kaan_check_sw("kaan_reset_ct", sw, rc);
}

/*
 * Get functional units
 */
int
kaan_get_units(ifd_reader_t *reader)
{
	unsigned char	cmd[] = { 0x20, 0x13, 0x00, 0x81, 0x00 };
	unsigned char	buffer[16], *units;
	int		rc, n;

	rc = kaan_apdu_xcv(reader, cmd, sizeof(cmd), buffer, sizeof(buffer), 0);
	if (rc < 0 || (rc = kaan_check_sw("kaan_get_units", buffer, rc)) < 0)
		return rc;

	if ((n = kaan_get_tlv(buffer, rc, 0x81, &units)) < 0)
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
	unsigned char	buffer[256] = { 0x20, 0x17, 0x40, 00 };
	int		rc, n;

	if (!(reader->flags & IFD_READER_DISPLAY))
		return 0;
	if (string == NULL)
		return IFD_ERROR_INVALID_ARG;

	n = kaan_build_display_args(buffer, sizeof(buffer), 0, string);
	if (n < 0)
		return n;

	rc = kaan_apdu_xcv(reader, buffer, n, buffer, sizeof(buffer), 0);
	return kaan_check_sw("kaan_display", buffer, rc);
}

/*
 * Power up the reader
 */
static int
kaan_activate(ifd_reader_t *reader)
{
	ifd_debug(1, "called.");
	return 0;
}

static int
kaan_deactivate(ifd_reader_t *reader)
{
	ifd_debug(1, "called.");
	return 0;
}

/*
 * Get the card status
 */
static int
kaan_card_status(ifd_reader_t *reader, int slot, int *status)
{
	unsigned char	buffer[16] = { 0x20, 0x13, slot+1, 0x80, 0x00 };
	unsigned char	*byte;
	int		rc, n;

	ifd_debug(1, "slot=%d", slot);

	if ((rc = kaan_apdu_xcv(reader, buffer, 5, buffer, sizeof(buffer), 0)) < 0
	 || (rc = kaan_check_sw("kaan_card_status", buffer, rc)) < 0)
		return rc;
	if ((n = kaan_get_tlv(buffer, rc, 0x80, &byte)) >= 0) {
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
	unsigned char	buffer[64] = { 0x20, 0x10, slot+1, 0x01, 0x00 };
	unsigned short	sw;
	int		rc;

	if ((rc = kaan_apdu_xcv(reader, buffer, 5, buffer, sizeof(buffer), 0)) < 0)
		return rc;

	if ((rc = kaan_get_sw(buffer, rc, &sw)) < 0)
		return rc;

	if ((sw & 0xFF00) != 0x9000) {
		ifd_debug(1, "kaan_card_reset: unable to reset card, sw=0x%04x", sw);
		return IFD_ERROR_COMM_ERROR;
	}

	if ((unsigned int) rc > size)
		rc = size;
	memcpy(result, buffer, rc);
	return rc;
}

/*
 * Request ICC
 */
int
kaan_card_request(ifd_reader_t *reader, int slot,
			time_t timeout, const char *message,
			void *atr, size_t atr_len)
{
	unsigned char	buffer[256] = { 0x20, 0x17, slot+1, 0x01, 0x00 };
	unsigned short	sw;
	int		n, rc;

	n = kaan_build_display_args(buffer, sizeof(buffer)-1, timeout, message);
	if (n < 0)
		return n;
	buffer[n++] = 0x00;

	rc = kaan_apdu_xcv(reader, buffer, n, buffer, sizeof(buffer), timeout);
	if (rc < 0)
		return rc;

	if ((rc = kaan_get_sw(buffer, rc, &sw)) < 0)
		return rc;

	if ((sw & 0xFF00) != 0x9000) {
		ifd_debug(1, "kaan_card_reset: unable to reset card, sw=0x%04x", sw);
		return IFD_ERROR_COMM_ERROR;
	}


	if ((unsigned int) rc > atr_len)
		rc = atr_len;
	memcpy(atr, buffer, rc);
	return rc;
}

/*
 * Helper function add message/timeout arguments to command
 * buffer
 */
int
kaan_build_display_args(unsigned char *cmd, size_t size, unsigned int timeout, const char *message)
{
	ct_buf_t	buf;
	unsigned int	n;

	/* Initialize buffer and skip APDU */
	ct_buf_init(&buf, cmd, size);
	ct_buf_put(&buf, NULL, 5);

	if (timeout) {
		if (ct_buf_putc(&buf, 0x80) < 0
		 || ct_buf_putc(&buf, 1) < 0
		 || ct_buf_putc(&buf, timeout) < 0)
			return -1;
	}
	if (message == NULL) {
		/* No message */
		cmd[3] |= 0xF0;
	} else if (strcmp(message, "@")) {
		if ((n = strlen(message)) > 32)
			n = 32;

		if (ct_buf_putc(&buf, 0x50) < 0
		 || ct_buf_putc(&buf, n) < 0
		 || ct_buf_put(&buf, message, n) < 0)
			return -1;

	}

	n = ct_buf_avail(&buf);
	cmd[4] = n - 5;
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
	unsigned char	cmd[] = { 0x80, 0x60, nslot+1, 0x00, 0x03, 0x22, 0x01, 0x00 };
	unsigned char	sw[2];
	ifd_slot_t	*slot;
	int		rc;

	ifd_debug(1, "proto=%d", proto);

	switch (proto) {
	case IFD_PROTOCOL_T0: cmd[7]    = 0x01; break;
	case IFD_PROTOCOL_T1: cmd[7]    = 0x02; break;
	case IFD_PROTOCOL_I2C: cmd[7]   = 0x80; break;
	case IFD_PROTOCOL_3WIRE: cmd[7] = 0x81; break;
	case IFD_PROTOCOL_2WIRE: cmd[7] = 0x82; break;
	default:
		ifd_debug(1, "kaan_set_protocol: protocol %d not supported", proto);
		return -1;
	}

	if ((rc = kaan_apdu_xcv(reader, cmd, sizeof(cmd), sw, sizeof(sw), 0)) < 0
	 || (rc = kaan_check_sw("kaan_set_protocol", sw, rc)) < 0)
		return rc;

	slot = &reader->slot[nslot];
	slot->proto = ifd_protocol_new(IFD_PROTOCOL_TRANSPARENT,
				reader, slot->dad);
	if (slot->proto == NULL) {
		ct_error("%s: internal error", reader->name);
		return -1;
	}

	st->icc_proto[nslot] = proto;
	return 0;
}

/*
 * APDU exchange with ICC
 */
static int
kaan_transparent(ifd_reader_t *reader, int dad,
		const void *sbuf, size_t slen,
		void *rbuf, size_t rlen)
{
	kaan_status_t	*st = (kaan_status_t *) reader->driver_data;
	ifd_iso_apdu_t	iso;
	int		rc, nslot, n, prot;

	nslot = (dad == 0x02)? 0 : 1;
	prot = st->icc_proto[nslot];

	/* Parse the APDU; extract class byte, case, etc */
	if ((rc = ifd_iso_apdu_parse(sbuf, slen, &iso)) < 0)
		return rc;

	if (prot == IFD_PROTOCOL_T0) {
		if (iso.cse == IFD_APDU_CASE_4S)
			slen--;
	}

	n = ifd_protocol_transceive(st->p, dad, sbuf, slen, rbuf, rlen);

	if (iso.cse == IFD_APDU_CASE_4S && n == 2) {
		unsigned char	cmd[5], *sw;

		sw = (unsigned char *) rbuf;
		if (sw[0] == 0x61) {
			cmd[0] = iso.cla;
			cmd[1] = 0xC0;
			cmd[2] = 0x00;
			cmd[3] = 0x00;
			cmd[4] = sw[1];

			n = ifd_protocol_transceive(st->p, dad,
						cmd, 5, rbuf, rlen);
		}
	}

	if (n < 0)
		return n;
	if (n < 2) {
		ct_error("kaan: T=1 protocol failure, not enough bytes for SW");
		return IFD_ERROR_COMM_ERROR;
	}

	return n;
}

/*
 * APDU exchange with terminal
 */
int
kaan_apdu_xcv(ifd_reader_t *reader,
		const unsigned char *sbuf, size_t slen,
		unsigned char *rbuf, size_t rlen,
		time_t timeout)
{
	kaan_status_t	*st = (kaan_status_t *) reader->driver_data;
	long		orig_timeout = 0;
	int		rc;

	/* Override timeout if needed */
	if (timeout) {
		ifd_protocol_get_parameter(st->p,
				IFD_PROTOCOL_RECV_TIMEOUT,
				&orig_timeout);
		ifd_protocol_set_parameter(st->p,
				IFD_PROTOCOL_RECV_TIMEOUT,
				timeout * 1000);
	}

	rc = ifd_protocol_transceive(st->p, 0x12, sbuf, slen, rbuf, rlen);
	if (rc < 0)
		return rc;
	if (rc < 2) {
		ct_error("kaan: T=1 protocol failure, rc=%d", rc);
		rc = IFD_ERROR_COMM_ERROR;
	}

	if (orig_timeout) {
		ifd_protocol_set_parameter(st->p,
				IFD_PROTOCOL_RECV_TIMEOUT,
				orig_timeout);
	}

	return rc;
}

/*
 * Check status word returned by Kaan
 */
int
kaan_check_sw(const char *msg, unsigned char *buf, int rc)
{
	unsigned short	sw;

	if (rc < 0) {
		ct_error("%s: %s", msg, ct_strerror(rc));
	} else if ((rc = kaan_get_sw(buf, rc, &sw)) >= 0) {
		if (sw != 0x9000) {
			ct_error("%s: failure, status code %04X",
					msg, sw);
			rc = IFD_ERROR_COMM_ERROR;
		}
	}
	return rc;
}

int
kaan_get_sw(unsigned char *buf, unsigned int n, unsigned short *sw)
{
	if (n < 2) {
		ifd_debug(1, "response too short (%d bytes)", n);
		return IFD_ERROR_COMM_ERROR;
	}

	n -= 2;
	*sw = (buf[n] << 8) | buf[n+1];
	return n;
}

/*
 * Send/receive T=1 apdu
 * This is just for the communication with the card reader.
 */
static int
kaan_send(ifd_reader_t *reader, unsigned int dad, const unsigned char *buffer, size_t len)
{
	return ifd_device_send(reader->device, buffer, len);
}

static int
kaan_recv(ifd_reader_t *reader, unsigned int dad, unsigned char *buffer, size_t len, long timeout)
{
	return ifd_device_recv(reader->device, buffer, len, timeout);
}

/*
 * Extract data from TLV encoded result
 */
int
kaan_get_tlv(unsigned char *buf, size_t len,
		unsigned char tag, unsigned char **res)
{
	unsigned char *p = buf;
	unsigned int n;

	n = len;
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
 * Driver operations
 */
static struct ifd_driver_ops	kaan_driver = {
	.open		= kaan_open,
	.activate	= kaan_activate,
	.deactivate	= kaan_deactivate,
	.card_status	= kaan_card_status,
	.card_reset	= kaan_card_reset,
	.card_request	= kaan_card_request,
	.output		= kaan_display,
	.send		= kaan_send,
	.recv		= kaan_recv,
	.set_protocol	= kaan_set_protocol,
	.transparent	= kaan_transparent,
};

/*
 * Initialize this module
 */
void
ifd_kaan_register(void)
{
	ifd_driver_register("kaan", &kaan_driver);
}
