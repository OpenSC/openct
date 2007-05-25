/*
 * Driver for Kobil Kaan Professional and Telesec B1
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 * Copyright (C) 2003 Michael Haardt (B1 support)
 */

#include "internal.h"
#include <sys/poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ctbcs.h"

/* Freeze after that many seconds of inactivity */
#define FREEZE_DELAY		5

enum {
	TYPE_KAAN,
	TYPE_B1
};

/*
 * CT status
 */
typedef struct kaan_status {
	int reader_type;
	ifd_protocol_t *p;
	time_t last_activity;
	unsigned int frozen:1;
	int icc_proto[OPENCT_MAX_SLOTS];
} kaan_status_t;

static int kaan_reset_ct(ifd_reader_t * reader);
static int kaan_get_units(ifd_reader_t * reader);
static int kaan_freeze(ifd_reader_t * reader);
static int kaan_sync_detect(ifd_reader_t * reader, int nslot);
static int kaan_set_protocol(ifd_reader_t *, int, int);
static int __kaan_apdu_xcv(ifd_reader_t *,
			   const unsigned char *, size_t,
			   unsigned char *, size_t, time_t, int);
static int kaan_get_tlv_from_file(ifd_reader_t *,
				  unsigned int, unsigned int,
				  unsigned char, unsigned char *, size_t);
static int kaan_get_tlv(unsigned char *, size_t,
			unsigned char tag, unsigned char **ptr);
static int kaan_select_file(ifd_reader_t *, unsigned char,
			    unsigned int, size_t *);
static int kaan_read_binary(ifd_reader_t *, unsigned char,
			    unsigned int, unsigned char *, size_t);
static int kaan_update_binary(ifd_reader_t *, unsigned char,
			      unsigned int, const unsigned char *, size_t);
static int kaan_check_sw(const char *, const unsigned char *, int);
static int kaan_get_sw(const unsigned char *, unsigned int, unsigned short *);
static int kaan_select_app(ifd_reader_t * reader, int nad,
			   const void *, size_t);

#define kaan_apdu_xcv(reader, sbuf, slen, rbug, rlen, timeout) \
	__kaan_apdu_xcv(reader, sbuf, slen, rbug, rlen, timeout, 1)

/*
 * Initialize the device
 */
static int kaan_open(ifd_reader_t * reader, const char *device_name)
{
	kaan_status_t *st;
	ifd_device_t *dev;
	ifd_device_params_t params;
	int r;

	reader->name = "Kobil Kaan PRO";
	reader->nslots = 1;

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
	if (ifd_device_type(dev) == IFD_DEVICE_TYPE_USB) {
        	params = dev->settings;
		params.usb.interface = 0;
		if (ifd_device_set_parameters(dev, &params) < 0) {
			ct_error("kaan: setting parameters failed", device_name);
			ifd_device_close(dev);
			return -1;
		}
	}

	reader->device = dev;
	if ((st = (kaan_status_t *) calloc(1, sizeof(*st))) == NULL) {
		ct_error("out of memory");
		return IFD_ERROR_NO_MEMORY;
	}
	st->reader_type = TYPE_KAAN;
	st->icc_proto[0] = -1;
	st->icc_proto[1] = -1;

	reader->driver_data = st;
	if (!(st->p = ifd_protocol_new(IFD_PROTOCOL_T1, reader, 0x12))) {
		/* Something is badly hosed */
		ct_error("unable to get T1 protocol handler");
		return IFD_ERROR_GENERIC;
	}

	/* Force a T=1 resync. We don't know what state the reader's
	 * T=1 engine is in. */
	if ((r = ifd_protocol_resynchronize(st->p, 0x12)) < 0)
		return r;

	/* Reset the CT */
	if ((r = kaan_reset_ct(reader)) < 0)
		return r;

	/* Get list of functional units */
	if ((r = kaan_get_units(reader)) < 0)
		return r;

#if 0
	/* Clear the display */
	reader->ops->display(reader, "");
#endif

	return 0;
}

/*
 * Initialize the device
 */
static int b1_open(ifd_reader_t * reader, const char *device_name)
{
	kaan_status_t *st;
	ifd_device_t *dev;
	ifd_device_params_t params;
	unsigned long w;
	int r;

	reader->name = "DTAG/T-TeleSec B1 standard";
	reader->nslots = 1;

	if (!(dev = ifd_device_open(device_name)))
		return -1;

	if (ifd_device_type(dev) == IFD_DEVICE_TYPE_SERIAL) {
		if (ifd_device_get_parameters(dev, &params) >= 0) {
			/* The default configuration for B1 serial
			 * readers is 8E1 at 9600 bps */
			params.serial.bits = 8;
			params.serial.parity = IFD_SERIAL_PARITY_EVEN;
			params.serial.stopbits = 1;
			params.serial.dtr = 0;
			params.serial.rts = 0;
			ifd_device_set_parameters(dev, &params);
			/* wait 35+-15 = 50 ms for DSR low */
			poll((struct pollfd *)0, 0, 50);
			if (ifd_serial_get_dsr(dev))
				return -1;
			/* wait further 300 ms until setting DTR */
			poll((struct pollfd *)0, 0, 300);
			/* set DTR */
			params.serial.dtr = 1;
			ifd_device_set_parameters(dev, &params);
			/* wait until DSR is set, which may take up to 5 s (typically 0.8 s) */
			for (w = 0; w <= 5000; w += 210) {
				poll((struct pollfd *)0, 0, 210);
				if (ifd_serial_get_dsr(dev))
					break;
			}
			if (w > 5000)
				return -1;
		} else
			return -1;
	}

	reader->device = dev;
	if ((st = (kaan_status_t *) calloc(1, sizeof(*st))) == NULL) {
		ct_error("out of memory");
		return IFD_ERROR_NO_MEMORY;
	}
	st->reader_type = TYPE_B1;
	st->icc_proto[0] = -1;
	st->icc_proto[1] = -1;

	reader->driver_data = st;
	if (!(st->p = ifd_protocol_new(IFD_PROTOCOL_T1, reader, 0x12))) {
		ct_error("unable to get T1 protocol handler");
		return IFD_ERROR_GENERIC;
	}

	/* Reset the CT */
	if ((r = kaan_reset_ct(reader)) < 0)
		return r;

	/* Get list of functional units */
	if ((r = kaan_get_units(reader)) < 0)
		return r;

	return 0;
}

/*
 * Reset the card reader
 */
static int kaan_reset_ct(ifd_reader_t * reader)
{
	unsigned char cmd1[] = { 0x20, 0x10, 0x00, 0x00 };
	unsigned char cmd2[] = { 0x20, 0x11, 0x00, 0x00 };
	unsigned char resp[2];
	int rc;
	unsigned short sw;

	if ((rc =
	     kaan_apdu_xcv(reader, cmd1, sizeof(cmd1), resp, sizeof(resp),
			   0)) < 0) {
		ct_error("kaan_reset_ct: %s", ct_strerror(rc));
		return rc;
	}
	ifd_debug(1, "kaan_reset_ct: rc=%d", rc);
	if ((rc = kaan_get_sw(resp, rc, &sw)) < 0)
		return rc;
	if (sw == 0x6b00) {
		/* Reset for older readers */
		if ((rc =
		     kaan_apdu_xcv(reader, cmd2, sizeof(cmd2), resp,
				   sizeof(resp), 0)) < 0) {
			ct_error("kaan_reset_ct: %s", ct_strerror(rc));
			return rc;
		}
		if ((rc = kaan_get_sw(resp, rc, &sw)) < 0)
			return rc;
	}
	if (sw != 0x9000) {
		ct_error("kaan_reset_ct: failure, status code %04X", sw);
		return IFD_ERROR_COMM_ERROR;
	}
	return rc;
}

/*
 * Get functional units
 */
static int kaan_get_units(ifd_reader_t * reader)
{
	unsigned char cmd[] = { 0x20, 0x13, 0x00, 0x81, 0x00 };
	unsigned char buffer[16], *units;
	int rc, n;
	unsigned short sw;

	reader->slot[0].dad = 0x02;
	if ((rc =
	     kaan_apdu_xcv(reader, cmd, sizeof(cmd), buffer, sizeof(buffer),
			   0)) < 0) {
		ct_error("kaan_get_units: %s", ct_strerror(rc));
		return rc;
	}
	if ((rc = kaan_get_sw(buffer, rc, &sw)) < 0)
		return rc;
	if (sw != 0x9000) {
		return 0;
	}
	if ((n = kaan_get_tlv(buffer, rc, 0x81, &units)) < 0)
		return 0;

	while (n--) {
		switch (units[n]) {
		case 0x01:	/* ICC1 */
			break;
		case 0x02:	/* ICC2 */
			reader->slot[1].dad = 0x32;
			reader->nslots = 2;
			break;
		case 0x40:	/* Display */
			reader->flags |= IFD_READER_KEYPAD;
			break;
		case 0x50:	/* Display */
			reader->flags |= IFD_READER_DISPLAY;
			break;
		}
	}

	return 0;
}

/*
 * Power up the reader
 */
static int kaan_activate(ifd_reader_t * reader)
{
	ifd_debug(1, "called.");
	return 0;
}

static int kaan_deactivate(ifd_reader_t * reader)
{
	ifd_debug(1, "called.");
	return 0;
}

/*
 * Get the card status
 */
static int kaan_card_status(ifd_reader_t * reader, int slot, int *status)
{
	kaan_status_t *st = (kaan_status_t *) reader->driver_data;
	unsigned char buffer[16] = { 0x20, 0x13, 0x00, 0x80, 0x00 };
	unsigned char *byte;
	int rc, n;

	buffer[2] = slot + 1;
	ifd_debug(1, "slot=%d", slot);
	if (!st->frozen && st->last_activity + FREEZE_DELAY < time(NULL)
	    && ifd_device_type(reader->device) == IFD_DEVICE_TYPE_SERIAL) {
		if ((rc = kaan_freeze(reader)) < 0)
			return rc;
		usleep(10000);
		st->frozen = 1;
	}

	if (st->frozen) {
		/* Get the DSR status */
		if (!ifd_serial_get_dsr(reader->device)) {
			*status = reader->slot[slot].status;
			return 0;
		}

		/* Activity detected - go on an get status */
		st->last_activity = time(NULL);
		st->frozen = 0;
	}

	rc = __kaan_apdu_xcv(reader, buffer, 5, buffer, sizeof(buffer), 0, 0);
	if ((rc = kaan_check_sw("kaan_card_status", buffer, rc)) < 0)
		return rc;
	if (buffer[0] == 0x80) {
		if ((n = kaan_get_tlv(buffer, rc, 0x80, &byte)) >= 0) {
			if (*byte & 0x01)
				*status |= IFD_CARD_PRESENT;
		}
	} else /* older implementations may return only value part */
    if (buffer[0] & 0x01)
		*status |= IFD_CARD_PRESENT;
	return 0;
}

/*
 * Get the card status
 */
static int b1_card_status(ifd_reader_t * reader, int slot, int *status)
{
        kaan_status_t *st = (kaan_status_t *) reader->driver_data;
        unsigned char buffer[16];
        int rc;

        buffer[2] = slot + 1;
        ifd_debug(1, "slot=%d", slot);
        if (!st->frozen && st->last_activity + FREEZE_DELAY < time(NULL)
            && ifd_device_type(reader->device) == IFD_DEVICE_TYPE_SERIAL) {
                if ((rc = kaan_freeze(reader)) < 0)
                        return rc;
                usleep(10000);
                st->frozen = 1;
        }

        if (st->frozen) {
                /* Get the DSR status */
                if (!ifd_serial_get_dsr(reader->device)) {
                        *status = reader->slot[slot].status;
                        return 0;
                }

                /* Activity detected - go on an get status */
                st->last_activity = time(NULL);
                st->frozen = 0;
        }

        rc = kaan_get_tlv_from_file(reader,
                                    0x7F70 | (slot),
                                    0x7021 | (slot << 8), 0x21, buffer, 1);
        if (rc < 0)
                return rc;

        ct_error("buffer[0] = %i\n", buffer[0] );
        if (buffer[0]) {
                *status |= IFD_CARD_PRESENT;
        }
        return 0;
}

/*
 * Send the Freeze command to the reader
 */
static int kaan_freeze(ifd_reader_t * reader)
{
	unsigned char freeze[16] = { 0x80, 0x70, 0x00, 0x00, 0x00, 0x30, 00 };
	unsigned int m, n;
	int rc;

	ifd_debug(1, "trying to freeze reader");
	for (n = 0, m = 7; n < reader->nslots; n++, m++) {
		freeze[m] = n + 1;
		if (reader->slot[n].status != 0)
			freeze[m] |= 0x02;
	}
	freeze[6] = n;
	freeze[4] = n + 2;

	rc = __kaan_apdu_xcv(reader, freeze, m, freeze, sizeof(freeze), 0, 0);
	return kaan_check_sw("kaan_card_freeze", freeze, rc);
}

/*
 * Common code for card_reset and card_request
 */
static int kaan_do_reset(ifd_reader_t * reader, int nslot,
			 const unsigned char *cmd, size_t cmd_len,
			 unsigned char *atr, size_t atr_len,
			 unsigned int timeout)
{
	kaan_status_t *st = (kaan_status_t *) reader->driver_data;
	unsigned char buffer[64];
	unsigned short sw;
	size_t got;
	int rc;

	st->icc_proto[nslot] = -1;
	if ((rc =
	     kaan_apdu_xcv(reader, cmd, cmd_len, buffer, sizeof(buffer),
			   timeout)) < 0)
		return rc;

	if ((rc = kaan_get_sw(buffer, rc, &sw)) < 0)
		return rc;

	if ((got = rc) > atr_len)
		got = atr_len;

	switch (sw) {
	case 0x9000:
	case 0x62a6:
		/* Synchronous ICC, CT has already done everything we need
		 * to know. Now just get the info from the CT. */
		memcpy(atr, buffer, got);
		if ((rc = kaan_sync_detect(reader, nslot)) < 0)
			return rc;

		if (got == 4 && st->reader_type == TYPE_B1) {
			/* Try to select KVK file.  This is required for
			 * B1 readers and failure is ignored.
			 */
			static unsigned char aid[] =
			    { 0xd2, 0x80, 0x00, 0x00, 0x01, 0x01 };
			kaan_select_app(reader, 0x02, aid, sizeof(aid));
		}
		break;
	case 0x62a5:
		/* ATR was read, but protocol is unknown.  B1 readers */
		/* use that for phone cards, because their size can not */
		/* automatically be detected.  Chose the largest possible */
		/* size. */
		memcpy(atr, buffer, got);
		if (got == 4 && st->reader_type == TYPE_B1)
			kaan_set_protocol(reader, nslot, IFD_PROTOCOL_EUROCHIP);
		if ((rc = kaan_sync_detect(reader, nslot)) < 0)
			return rc;
		break;
	case 0x9001:
		/* asynchronous ICC, just copy the ATR */
		memcpy(atr, buffer, got);
		break;
	case 0x62a7:
		/* synchronous ICC, unknown proto - try to detect 
		 * the standard way */
		return ifd_sync_detect_icc(reader, nslot, atr, atr_len);
	default:
		ifd_debug(1, "kaan_card_reset: unable to reset card, sw=0x%04x",
			  sw);
		return IFD_ERROR_COMM_ERROR;
	}

	return got;
}

/*
 * Reset card and get ATR
 */
static int kaan_card_reset(ifd_reader_t * reader, int nslot, void *result,
			   size_t size)
{
	unsigned char cmd[5] = { 0x20, 0x10, 0x00, 0x01, 0x00 };

	cmd[2] = nslot + 1;
	ifd_debug(1, "called.");
	return kaan_do_reset(reader, nslot, cmd, 5, (unsigned char *)result,
			     size, 0);
}

/*
 * Request ICC
 */
static int kaan_card_request(ifd_reader_t * reader, int slot, time_t timeout,
			     const char *message, void *atr, size_t atr_len)
{
	ct_buf_t buf;
	unsigned char buffer[256] = { 0x20, 0x17, 0x00, 0x01, 0x00 };
	int n;

	buffer[2] = slot + 1;
	/* Build the APDU, which is basically a modified CTBCS OUTPUT command */
	ct_buf_init(&buf, buffer, sizeof(buffer) - 1);
	ctbcs_begin(&buf, 0x17, slot + 1, 0x01);
	ctbcs_add_timeout(&buf, timeout);
	ctbcs_add_message(&buf, message);
	if ((n = ctbcs_finish(&buf)) < 0)
		return n;
	buffer[n++] = 0x00;

	return kaan_do_reset(reader, slot, buffer, n, (unsigned char *)atr,
			     atr_len, timeout);
}

/*
 * Select a protocol for communication with the ICC.
 * Note that we continue to communicate with the terminal
 * using T=1; internally, the terminal talks to the
 * card using whatever protocol we selected.
 */
static int kaan_set_protocol(ifd_reader_t * reader, int nslot, int proto)
{
	kaan_status_t *st = (kaan_status_t *) reader->driver_data;
	unsigned char cmd[] =
	    { 0x80, 0x60, 0x00, 0x00, 0x03, 0x22, 0x01, 0x00 };
	unsigned char buffer[2];
	unsigned short sw;
	ifd_slot_t *slot;
	int rc;

	cmd[2] = nslot + 1;
	ifd_debug(1, "proto=%d", proto);

	switch (proto) {
	case IFD_PROTOCOL_T0:
		cmd[7] = 0x01;
		break;
	case IFD_PROTOCOL_T1:
		cmd[7] = 0x02;
		break;
	case IFD_PROTOCOL_I2C_SHORT:
		cmd[7] = 0x80;
		break;
	case IFD_PROTOCOL_I2C_LONG:
		cmd[7] = 0x80;
		break;
	case IFD_PROTOCOL_3WIRE:
		cmd[7] = 0x81;
		break;
	case IFD_PROTOCOL_2WIRE:
		cmd[7] = 0x82;
		break;
	case IFD_PROTOCOL_EUROCHIP:
		cmd[7] = 0x93;
		break;
	default:
		ifd_debug(1, "kaan_set_protocol: protocol %d not supported",
			  proto);
		return -1;
	}

	if ((rc =
	     kaan_apdu_xcv(reader, cmd, sizeof(cmd), buffer, sizeof(buffer),
			   0)) < 0 || (rc = kaan_get_sw(buffer, rc, &sw)) < 0)
		return rc;
	/* B1 returns 6985 for German KVK health care cards */
	if (sw != 0x9000 && sw != 0x6985) {
		ifd_debug(1,
			  "kaan_set_protocol: protocol %d not supported, sw=%04hx",
			  proto, sw);
		return -1;
	}

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
static int kaan_transparent(ifd_reader_t * reader, int dad, const void *sbuf,
			    size_t slen, void *rbuf, size_t rlen)
{
	kaan_status_t *st = (kaan_status_t *) reader->driver_data;
	ifd_iso_apdu_t iso;
	int rc, nslot, n, prot;

	nslot = (dad == 0x02) ? 0 : 1;
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
		unsigned char cmd[5], *sw;

		sw = (unsigned char *)rbuf;
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
 * Output a string to the display
 */
static int kaan_display(ifd_reader_t * reader, const char *string)
{
	unsigned char buffer[256] = { 0x20, 0x17, 0x40, 00 };
	int rc, n;

	if (!(reader->flags & IFD_READER_DISPLAY))
		return 0;

	n = ctbcs_build_output(buffer, sizeof(buffer), string);
	if (n < 0)
		return n;

	rc = kaan_apdu_xcv(reader, buffer, n, buffer, sizeof(buffer), 0);
	return kaan_check_sw("kaan_display", buffer, rc);
}

/*
 * Perform a PIN verification
 */
static int kaan_perform_verify(ifd_reader_t * reader, int nslot,
			       unsigned int timeout, const char *prompt,
			       const unsigned char *data, size_t data_len,
			       unsigned char *resp, size_t resp_len)
{
	unsigned char buffer[256];
	int n;
	unsigned short sw;

	if (!(reader->flags & IFD_READER_KEYPAD))
		return 0;

	n = ctbcs_build_perform_verify_apdu(buffer, sizeof(buffer),
					    nslot + 1, prompt, timeout,
					    data, data_len);
	if (n < 0)
		return n;

	n = kaan_apdu_xcv(reader, buffer, n, resp, resp_len, 0);
	if (n < 0) {
		ct_error("perform_verify failed: %s", ct_strerror(n));
		return n;
	}
	if ((n = kaan_get_sw(resp, n, &sw)) < 0)
		return n;

	switch (sw) {
	case 0x6400:
		ct_error("perform_verify failed: timeout");
		return IFD_ERROR_USER_TIMEOUT;
	case 0x6401:
		ct_error("perform_verify failed: user pressed cancel");
		return IFD_ERROR_USER_ABORT;
	case 0x6402:
		ct_error("perform_verify failed: PIN mismatch");
		return IFD_ERROR_PIN_MISMATCH;
	}

	return 2;
}

/*
 * Read from synchronous ICC
 */
static int kaan_sync_read(ifd_reader_t * reader, int slot, int proto,
			  unsigned short addr, unsigned char *data, size_t len)
{
	kaan_status_t *st = (kaan_status_t *) reader->driver_data;
	int r;

	ifd_debug(1, "called, addr=0x%04x, len=%u", addr, len);

	if (st->icc_proto[slot] != proto) {
		r = kaan_set_protocol(reader, slot, proto);
		if (r < 0)
			return r;
	}

	return kaan_read_binary(reader, reader->slot[slot].dad, addr, data,
				len);
}

static int kaan_sync_write(ifd_reader_t * reader, int slot, int proto,
			   unsigned short addr, const unsigned char *buffer,
			   size_t len)
{
	kaan_status_t *st = (kaan_status_t *) reader->driver_data;
	int r;

	ifd_debug(1, "called, addr=0x%04x, len=%u", addr, len);

	if (st->icc_proto[slot] != proto) {
		r = kaan_set_protocol(reader, slot, proto);
		if (r < 0)
			return r;
	}

	return kaan_update_binary(reader, reader->slot[slot].dad,
				  addr, buffer, len);
}

/*
 * Detect type and size of synchronous card.
 * When we get here, the CT has done most of the work for us
 * already, we just need to get the information from it.
 *
 * XXX - there does not seem to be a way to find out the size
 * of the card, so we have to resort to poking around the
 * card.
 */
static int kaan_sync_detect(ifd_reader_t * reader, int nslot)
{
	kaan_status_t *st = (kaan_status_t *) reader->driver_data;
	ifd_slot_t *slot = &reader->slot[nslot];
	unsigned char protocol;
	int rc;

	rc = kaan_get_tlv_from_file(reader,
				    0x7F70 | nslot,
				    0x7021 | (nslot << 8), 0x22, &protocol, 1);
	if (rc < 0)
		return rc;

	switch (protocol) {
	case 0x80:
		protocol = IFD_PROTOCOL_I2C_LONG;
		break;
	case 0x81:
		protocol = IFD_PROTOCOL_3WIRE;
		break;
	case 0x82:
		protocol = IFD_PROTOCOL_2WIRE;
		break;
	case 0x90:
	case 0x91:
	case 0x92:
	case 0x93:
		protocol = IFD_PROTOCOL_EUROCHIP;
		break;
	default:
		ct_error("kaan_sync_detect: unknown card protocol 0x%x",
			 protocol);
		return IFD_ERROR_NOT_SUPPORTED;
	}

	slot->proto = ifd_protocol_new(protocol, reader, slot->dad);
	st->icc_proto[nslot] = protocol;

	return 0;
}

/*
 * Read from config/status file
 */
static int kaan_get_tlv_from_file(ifd_reader_t * reader, unsigned int df_id,
			   unsigned int ef_id, unsigned char tag,
			   unsigned char *data, size_t len)
{
	unsigned char buffer[256 + 2], *ptr;
	size_t size;
	int rc;

	if ((rc = kaan_select_file(reader, 0x12, 0x3F00, &size)) < 0
	    || (rc = kaan_select_file(reader, 0x12, df_id, &size)) < 0
	    || (rc = kaan_select_file(reader, 0x12, ef_id, &size)) < 0)
		return rc;
	if (size > 256)
		size = 256;
	if ((rc = kaan_read_binary(reader, 0x12, 0, buffer, 256)) < 0)
		return rc;

	if ((rc = kaan_get_tlv(buffer, rc, tag, &ptr)) < 0)
		return rc;
	if ((size_t) rc > len)
		rc = len;
	memcpy(data, ptr, rc);
	return rc;
}

/*
 * Stuff to interface with the Kaan's internal file system
 */
static int kaan_select_file(ifd_reader_t * reader, unsigned char nad,
		     unsigned int fid, size_t * sizep)
{
	unsigned char cmd[] = { 0x00, 0xa4, 0x00, 0x00, 2, 0x00, 0x00 };
	unsigned char resp[64];
	int r;

	ifd_debug(1, "called, fid=0x%04x", fid);

	cmd[5] = fid >> 8;
	cmd[6] = fid & 0xFF;

	r = kaan_transparent(reader, nad, cmd, sizeof(cmd), resp, sizeof(resp));
	if (r < 0)
		return r;
	if ((r = kaan_check_sw("kaan_select_file", resp, r)) < 0)
		return r;

	if (sizep)
		*sizep = (resp[0] << 8) | resp[1];

	return 0;
}

static int kaan_select_app(ifd_reader_t * reader, int nad, const void *aid,
				size_t len)
{
	unsigned char cmd[32] = { 0x00, 0xa4, 0x04, 0x00 };
	unsigned char resp[64];
	int r;

	if (len > sizeof(cmd) - 5)
		return IFD_ERROR_BUFFER_TOO_SMALL;

	cmd[4] = len;
	memcpy(cmd + 5, aid, len);

	r = kaan_transparent(reader, nad, cmd, len + 5, resp, sizeof(resp));
	if (r < 0)
		return r;
	return kaan_check_sw("kaan_select_app", resp, r);
}

static int kaan_read_binary(ifd_reader_t * reader, unsigned char nad,
		     unsigned int offset, unsigned char *data, size_t len)
{
	unsigned char cmd[] = { 0x00, 0xB0, 0x00, 0x00, 0x00 };
	unsigned char buffer[258];
	size_t count, total = 0, got;
	unsigned short sw;
	int r;

	ifd_debug(1, "called, offset=0x%04x, len=%u", offset, len);
	while (total < len) {
		if ((count = len) > 256)
			count = 256;
		cmd[2] = offset >> 8;
		cmd[3] = offset & 0xFF;
		cmd[4] = count;

		r = kaan_transparent(reader, nad, cmd, sizeof(cmd), buffer,
				     sizeof(buffer));
		if (r < 0)
			return r;
		got = r - 2;

		if ((r = kaan_get_sw(buffer, r, &sw)) < 0)
			return r;

		switch (sw) {
		case 0x6B00:	/* offset outside of file */
			goto done;
		case 0x9000:
		case 0x6282:	/* EOF reached */
			memcpy(data + total, buffer, got);
			offset += got;
			total += got;
			break;
		default:
			ct_error("kaan_read_binary: "
				 "failure, status code %04X", sw);
			return IFD_ERROR_COMM_ERROR;
		}

		/* end of file? */
		if (got == 0 || sw == 0x6282)
			break;
	}

      done:
	return total;
}

static int kaan_update_binary(ifd_reader_t * reader, unsigned char nad,
		       unsigned int offset, const unsigned char *data,
		       size_t len)
{
	unsigned char cmd[256 + 5] = { 0x00, 0xD0, 0x00, 0x00, 0x00 };
	unsigned char resp[2];
	size_t count, total = 0;
	int r;

	ifd_debug(2, "called, offset=0x%04x, len=%u", offset, len);
	while (total < len) {
		if ((count = len) > 256)
			count = 256;
		cmd[2] = offset >> 8;
		cmd[3] = offset & 0xFF;
		cmd[4] = count;
		memcpy(cmd + 5, data + total, count);

		r = kaan_transparent(reader, nad, cmd, 5 + count, resp,
				     sizeof(resp));
		if (r < 0)
			return r;
		if ((r = kaan_check_sw("kaan_update_binary", resp, r)) < 0)
			return r;

		if (r == 0)
			break;

		offset += r;
		total += r;
	}

	return total;
}

/*
 * APDU exchange with terminal
 */
static int __kaan_apdu_xcv(ifd_reader_t * reader, const unsigned char *sbuf,
		    size_t slen, unsigned char *rbuf, size_t rlen,
		    time_t timeout, int activity)
{
	kaan_status_t *st = (kaan_status_t *) reader->driver_data;
	long orig_timeout = 0;
	int rc;

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

	if (timeout) {
		ifd_protocol_set_parameter(st->p,
					   IFD_PROTOCOL_RECV_TIMEOUT,
					   orig_timeout);
	}

	if (activity) {
		st->last_activity = time(NULL);
		st->frozen = 0;
	}

	return rc;
}

/*
 * Check status word returned by Kaan
 */
static int kaan_check_sw(const char *msg, const unsigned char *buf, int rc)
{
	unsigned short sw;

	if (rc < 0) {
		ct_error("%s: %s", msg, ct_strerror(rc));
	} else if ((rc = kaan_get_sw(buf, rc, &sw)) >= 0) {
		if (sw != 0x9000) {
			ct_error("%s: failure, status code %04X", msg, sw);
			rc = IFD_ERROR_COMM_ERROR;
		}
	}
	return rc;
}

static int kaan_get_sw(const unsigned char *buf, unsigned int n,
				unsigned short *sw)
{
	if (n < 2) {
		ifd_debug(1, "response too short (%d bytes)", n);
		return IFD_ERROR_COMM_ERROR;
	}

	n -= 2;
	*sw = (buf[n] << 8) | buf[n + 1];
	return n;
}

/*
 * Send/receive T=1 apdu
 * This is just for the communication with the card reader.
 */
static int kaan_send(ifd_reader_t * reader, unsigned int dad,
		     const unsigned char *buffer, size_t len)
{
	return ifd_device_send(reader->device, buffer, len);
}

static int kaan_recv(ifd_reader_t * reader, unsigned int dad,
		     unsigned char *buffer, size_t len, long timeout)
{
	return ifd_device_recv(reader->device, buffer, len, timeout);
}

/*
 * Extract data from TLV encoded result.
 * The length is encoded as a single byte in the range 0 to 254.
 * A value of 255 means a big endian two byte length value follows.
 */
static int kaan_get_tlv(unsigned char *buf, size_t len, unsigned char tag,
		 unsigned char **res)
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
static struct ifd_driver_ops kaan_driver, b1_driver;

/*
 * Initialize this module
 */
void ifd_kaan_register(void)
{
	kaan_driver.open = kaan_open;
	kaan_driver.activate = kaan_activate;
	kaan_driver.deactivate = kaan_deactivate;
	kaan_driver.card_status = kaan_card_status;
	kaan_driver.card_reset = kaan_card_reset;
	kaan_driver.card_request = kaan_card_request;
	kaan_driver.output = kaan_display;
	kaan_driver.perform_verify = kaan_perform_verify;
	kaan_driver.send = kaan_send;
	kaan_driver.recv = kaan_recv;
	kaan_driver.set_protocol = kaan_set_protocol;
	kaan_driver.transparent = kaan_transparent;
	kaan_driver.sync_read = kaan_sync_read;
	kaan_driver.sync_write = kaan_sync_write;

	b1_driver.open = b1_open;
	b1_driver.activate = kaan_activate;
	b1_driver.deactivate = kaan_deactivate;
	b1_driver.card_status = b1_card_status;
	b1_driver.card_reset = kaan_card_reset;
	b1_driver.card_request = kaan_card_request;
	b1_driver.output = kaan_display;
	b1_driver.perform_verify = kaan_perform_verify;
	b1_driver.send = kaan_send;
	b1_driver.recv = kaan_recv;
	b1_driver.set_protocol = kaan_set_protocol;
	b1_driver.transparent = kaan_transparent;
	b1_driver.sync_read = kaan_sync_read;
	b1_driver.sync_write = kaan_sync_write;

	ifd_driver_register("kaan", &kaan_driver);
	ifd_driver_register("b1", &b1_driver);
}
