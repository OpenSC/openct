/*
 * CTAPI front-end for libifd
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <ifd/core.h>
#include <ifd/buffer.h>
#include <ifd/logging.h>
#include <ifd/conf.h>
#include <ifd/error.h>
#include "ctapi.h"

static int	ifd_ctapi_control(ifd_reader_t *, ifd_apdu_t *);
static int	ifd_ctapi_reset(ifd_reader_t *,
			ifd_iso_apdu_t *, ifd_buf_t *,
			time_t, const char *);
static int	ifd_ctapi_request_icc(ifd_reader_t *,
			ifd_iso_apdu_t *, ifd_buf_t *);
static int	ifd_ctapi_status(ifd_reader_t *,
			ifd_iso_apdu_t *, ifd_buf_t *);
static int	ifd_ctapi_error(ifd_buf_t *, unsigned int);
static int	ifd_ctapi_put_sw(ifd_buf_t *, unsigned int);


/*
 * Initialize card terminal #N.
 * As all the terminals are configured by libifd internally,
 * we ignore the port number
 */
char
CT_init(unsigned short ctn, unsigned short pn)
{
	static int	first_time = 1;
	ifd_reader_t	*reader;

	/* When doing first time initialization, init the
	 * library */
	if (first_time) {
		first_time = 0;
		ifd_init();
	}

	if (!(reader = ifd_reader_by_index(ctn)))
		return ERR_INVALID;

	/* FIXME: just activate, or reset as well? */
	if (ifd_activate(reader) < 0)
		return ERR_INVALID;

	return OK;
}

char
CT_close(unsigned short ctn)
{
	ifd_reader_t	*reader;

	if (!(reader = ifd_reader_by_index(ctn)))
		return ERR_INVALID;
	if (reader)
		ifd_deactivate(reader);
	return OK;
}

char
CT_data(unsigned short ctn,
	unsigned char  *dad,
	unsigned char  *sad,
	unsigned short lc,
	unsigned char  *cmd,
	unsigned short *lr,
	unsigned char  *rsp)
{
	ifd_reader_t	*reader;
	ifd_apdu_t	apdu;
	int		rc;

	if (!(reader = ifd_reader_by_index(ctn))
	 || !sad || !dad)
		return ERR_INVALID;

	apdu.snd_len = lc;
	apdu.snd_buf = cmd;
	apdu.rcv_len = *lr;
	apdu.rcv_buf = rsp;

	if (ifd_config.debug > 1) {
		ifd_debug("CT_data(dad=%d lc=%u lr=%u cmd=%s",
				*dad, lc, *lr, ifd_hexdump(cmd, lc));
	}
	switch (*dad) {
	case 0:
		rc = ifd_card_command(reader, 0, &apdu);
		break;
	case 1:
		rc = ifd_ctapi_control(reader, &apdu);
		break;
	case 2:
		ifd_error("CT-API: host talking to itself - "
			  "needs professional help?");
		return ERR_INVALID;
	case 3:
		rc = ifd_card_command(reader, 1, &apdu);
		break;
	default:
		ifd_error("CT-API: unknown DAD %u", *dad);
		return ERR_INVALID;
	}

	/* Somewhat simplistic error translation */
	if (rc < 0)
		return ERR_INVALID;

	*lr = rc;
	return OK;
}

/*
 * Handle CTBCS messages
 */
int
ifd_ctapi_control(ifd_reader_t *reader, ifd_apdu_t *apdu)
{
	ifd_iso_apdu_t	iso;
	ifd_buf_t	rbuf;
	int		rc;

	if (apdu->rcv_len < 2)
		return ERR_INVALID;

	if (ifd_apdu_to_iso(apdu, &iso) < 0) {
		ifd_error("Unable to parse CTBCS APDU");
		return ERR_INVALID;
	}

	ifd_buf_init(&rbuf, apdu->rcv_buf, apdu->rcv_len);

	if (iso.cla != CTBCS_CLA) {
		ifd_error("Bad CTBCS APDU, cla=0x%02x", iso.cla);
		ifd_ctapi_error(&rbuf, CTBCS_SW_BAD_CLASS);
		goto out;
	}

	switch (iso.ins) {
	case CTBCS_INS_RESET:
		rc = ifd_ctapi_reset(reader, &iso, &rbuf, 0, NULL);
		break;
	case CTBCS_INS_REQUEST_ICC:
		rc = ifd_ctapi_request_icc(reader, &iso, &rbuf);
		break;
	case CTBCS_INS_STATUS:
		rc = ifd_ctapi_status(reader, &iso, &rbuf);
		break;
	default:
		ifd_error("Bad CTBCS APDU, ins=0x%02x", iso.ins);
		rc = ifd_ctapi_error(&rbuf, CTBCS_SW_BAD_INS);
	}

	if (rc < 0)
		return rc;

	if (ifd_buf_avail(&rbuf) > iso.le + 2)
		ifd_ctapi_error(&rbuf, CTBCS_SW_BAD_LENGTH);

out:	apdu->rcv_len = ifd_buf_avail(&rbuf);
	return apdu->rcv_len;
}

/*
 * Request ICC
 */
static int
ifd_ctapi_request_icc(ifd_reader_t *reader,
		ifd_iso_apdu_t *iso, ifd_buf_t *rbuf)
{
	ifd_buf_t	sbuf;
	time_t		timeout = 0;
	char		msgbuf[256], *message;

	switch (iso->p2 >> 4) {
	case 0x00:
		/* use default label, or label specified
		 * in data. An empty message string indicates
		 * the default message */
		message = msgbuf;
		msgbuf[0] = '\0';
		break;
	case 0x0f:
		/* No message */
		message = NULL;
	default:
		return ifd_ctapi_error(rbuf, CTBCS_SW_BAD_PARAMS);
	}

	ifd_buf_set(&sbuf, iso->snd_buf, iso->lc);
	while (ifd_buf_avail(&sbuf)) {
		unsigned char	type, len, val;

		if (ifd_buf_get(&sbuf, &type, 1) < 0
		 || ifd_buf_get(&sbuf, &len, 1) < 0
		 || ifd_buf_avail(&sbuf) < len)
			goto bad_length;

		if (type == 0x50) {
			ifd_buf_get(&sbuf, msgbuf, len);
			msgbuf[len] = '\0';
		} else if (type == 0x80) {
			if (len != 1)
				goto bad_length;
			ifd_buf_get(&sbuf, &val, 1);
			timeout = val;
		} else {
			/* Ignore unknown tag */
			ifd_buf_get(&sbuf, NULL, len);
		}
	}

	/* ifd_ctapi_reset does all the rest of the work */
	return ifd_ctapi_reset(reader, iso, rbuf, timeout, message);

bad_length:
	return ifd_ctapi_error(rbuf, CTBCS_SW_BAD_LENGTH);
}

int
ifd_ctapi_reset(ifd_reader_t *reader, ifd_iso_apdu_t *iso,
		ifd_buf_t *rbuf,
		time_t timeout, const char *message)
{
	unsigned char	unit;
	unsigned int	atrlen = 0;
	unsigned char	atr[64];
	int		rc;

	unit = iso->p1;
	switch (unit) {
	case CTBCS_UNIT_INTERFACE1:
	case CTBCS_UNIT_INTERFACE2:
		rc = ifd_card_reset(reader, unit - CTBCS_UNIT_INTERFACE1,
				atr, sizeof(atr));
		break;

	default:
		/* Unknown unit */
		return ifd_ctapi_error(rbuf, CTBCS_SW_BAD_PARAMS);
	}

	if (rc < 0)
		return ERR_TRANS;
	
	switch (iso->p2 & 0xF) {
	case CTBCS_P2_RESET_NO_RESP:
		atrlen = 0;
		break;
	case CTBCS_P2_RESET_GET_ATR:
		atrlen = rc;
		break;
	case CTBCS_P2_RESET_GET_HIST:
		ifd_error("CTAPI RESET: P2=GET_HIST not supported yet");
		return ifd_ctapi_error(rbuf, CTBCS_SW_BAD_PARAMS);
	}

	if (ifd_buf_put(rbuf, atr, atrlen) < 0
	 || ifd_ctapi_put_sw(rbuf, 0x9000) < 0)
		return ifd_ctapi_error(rbuf, CTBCS_SW_BAD_LENGTH);

	return 0;
}

static int
ifd_ctapi_status(ifd_reader_t *reader, ifd_iso_apdu_t *iso, ifd_buf_t *rbuf)
{
	unsigned int	n;

	for (n = 0; n < reader->nslots; n++) {
		unsigned char	c;
		int		status;

		if (ifd_card_status(reader, n, &status) < 0)
			status = 0;

		c = (status & IFD_CARD_PRESENT)
			? CTBCS_DATA_STATUS_CARD_CONNECT
			: CTBCS_DATA_STATUS_NOCARD;
		if (ifd_buf_put(rbuf, &c, 1) < 0)
			goto bad_length;
	}

	if (ifd_ctapi_put_sw(rbuf, 0x9000) < 0)
		goto bad_length;

	return 0;

bad_length:
	return ifd_ctapi_error(rbuf, CTBCS_SW_BAD_LENGTH);
}

/*
 * Functions for setting the SW
 */
static int
ifd_ctapi_error(ifd_buf_t *bp, unsigned int sw)
{
	ifd_buf_clear(bp);
	return ifd_ctapi_put_sw(bp, sw);
}

int
ifd_ctapi_put_sw(ifd_buf_t *bp, unsigned int sw)
{
	unsigned char	temp[2];

	temp[0] = sw >> 8;
	temp[1] = sw & 0xff;

	if (ifd_buf_put(bp, temp, 2) < 0)
		return ERR_INVALID;
	return 2;
}
