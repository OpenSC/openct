/*
 * Generic CT-API functions, to be used by CTAPI driver shims
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include "ctapi.h"

static int	ifd_ctapi_reply(ifd_apdu_t *, unsigned int);
static int	ifd_ctapi_reset(ifd_reader_t *, ifd_apdu_t *);
static int	ifd_ctapi_request_icc(ifd_reader_t *, ifd_apdu_t *);
static int	ifd_ctapi_status(ifd_reader_t *, ifd_apdu_t *);

int
ifd_ctapi_control(ifd_reader_t *reader, ifd_apdu_t *apdu)
{
	ifd_iso_apdu_t	iso;

	if (ifd_apdu_to_iso(apdu, &iso) < 0) {
		ifd_error("Unable to parse CTBCS APDU");
		return ERR_INVALID;
	}

	if (iso.cla != CTBCS_CLA) {
		ifd_error("Bad CTBCS APDU, cla=0x%02x", iso.cla);
		return ifd_ctapi_reply(apdu, CTBCS_SW_BAD_CLASS);
	}

	switch (iso.ins) {
	case CTBCS_INS_RESET:
		return ifd_ctapi_reset(reader, apdu);
	case CTBCS_INS_REQUEST_ICC:
		return ifd_ctapi_request_icc(reader, apdu);
	case CTBCS_INS_STATUS:
		return ifd_ctapi_status(reader, apdu);
	}

	ifd_error("Bad CTBCS APDU, ins=0x%02x", iso.ins);
	return ifd_ctapi_reply(apdu, CTBCS_SW_BAD_INS);
}

static int
ifd_ctapi_reset(ifd_reader_t *reader, ifd_apdu_t *apdu)
{
	unsigned char	*p;
	unsigned int	atrlen = 0;
	unsigned char	atr[64];
	int		rc;

	p = (unsigned char *) apdu->snd_buf;
	switch (p[2]) {
	case CTBCS_UNIT_INTERFACE1:
		rc = ifd_card_reset(reader, 0, atr, sizeof(atr));
		break;
	case CTBCS_UNIT_INTERFACE2:
		rc = ifd_card_reset(reader, 0, atr, sizeof(atr));
		break;
	default:
		/* Unknown unit */
		return ifd_ctapi_reply(apdu, CTBCS_SW_BAD_PARAMS);
	}

	if (rc < 0)
		return ERR_TRANS;
	
	switch (p[3]) {
	case CTBCS_P2_RESET_NO_RESP:
		atrlen = 0;
		break;
	case CTBCS_P2_RESET_GET_ATR:
		atrlen = rc;
		break;
	case CTBCS_P2_RESET_GET_HIST:
		ifd_error("CTAPI RESET: P2=GET_HIST not supported yet");
		return ifd_ctapi_reply(apdu, CTBCS_SW_BAD_PARAMS);
	}

	if (apdu->rcv_len < 2 + atrlen)
		return ifd_ctapi_reply(apdu, CTBCS_SW_BAD_LENGTH);

	apdu->rcv_len = 2 + atrlen;

	p = (unsigned char *) apdu->rcv_buf;
	memcpy(p, atr, atrlen);
	p += atrlen;

	*p++ = 0x90;
	*p++ = 0x00;

	return atrlen + 2;
}

static int
ifd_ctapi_request_icc(ifd_reader_t *reader, ifd_apdu_t *apdu)
{
	/* Same as reset for now, until we implement request icc */
	return ifd_ctapi_reset(reader, apdu);
}

static int
ifd_ctapi_status(ifd_reader_t *reader, ifd_apdu_t *apdu)
{
	unsigned char	*p;
	unsigned int	n;

	if (apdu->rcv_len < 2 + reader->nslots)
		return ifd_ctapi_reply(apdu, CTBCS_SW_BAD_LENGTH);

	apdu->rcv_len = 2 + reader->nslots;
	p = (unsigned char *) apdu->rcv_buf;

	for (n = 0; n < reader->nslots; n++) {
		int	status;

		if (ifd_card_status(reader, n, &status) < 0)
			status = 0;

		*p++ = (status & IFD_CARD_PRESENT)
			? CTBCS_DATA_STATUS_CARD_CONNECT
			: CTBCS_DATA_STATUS_NOCARD;
	}
	*p++ = 0x90;
	*p++ = 0x00;

	return n + 2;
}

static int
ifd_ctapi_reply(ifd_apdu_t *apdu, unsigned int sw)
{
	unsigned char	*p;

	if (apdu->rcv_len < 2)
		return ERR_INVALID;
	
	p = (unsigned char *) apdu->rcv_buf;
	*p++ = sw >> 8;
	*p++ = sw & 0xff;

	apdu->rcv_len = 2;
	return 2;
}
