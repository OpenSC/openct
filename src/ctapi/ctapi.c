/*
 * CTAPI front-end for libopenct
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdlib.h>
#include <openct/openct.h>
#include <openct/ifd.h>
#include <openct/buffer.h>
#include <openct/logging.h>
#include <openct/conf.h>
#include <openct/error.h>
#include "ctapi.h"

static struct CardTerminal
{
	unsigned short ctn;
	ct_handle *h;
	ct_lock_handle lock;
	struct CardTerminal *next;
} *cardTerminals;

/*
 * Functions for setting the SW
 */
static int
ctapi_put_sw(ct_buf_t *bp, unsigned int sw)
{
	unsigned char	temp[2];

	temp[0] = sw >> 8;
	temp[1] = sw & 0xff;

	if (ct_buf_put(bp, temp, 2) < 0)
		return ERR_INVALID;
	return 2;
}

static int
ctapi_error(ct_buf_t *bp, unsigned int sw)
{
	ct_buf_clear(bp);
	return ctapi_put_sw(bp, sw);
}

static int
ctapi_reset(ct_handle *h, char p1, char p2,
		ct_buf_t *rbuf,
		time_t timeout, const char *message)
{
	unsigned int	atrlen = 0;
	unsigned char	atr[64];
	int		rc;

	switch (p1) {
	case CTBCS_UNIT_INTERFACE1:
	case CTBCS_UNIT_INTERFACE2:
		rc = ct_card_reset(h, p1 - CTBCS_UNIT_INTERFACE1,
				atr, sizeof(atr));
		break;

        case CTBCS_UNIT_CT:
		/* Reset is already performed during CT_init() */
        	rc = 0;
        	break;

	default:
		/* Unknown unit */
		return ctapi_error(rbuf, CTBCS_SW_BAD_PARAMS);
	}

	if (rc < 0)
		return ERR_TRANS;
	
	switch (p2 & 0xF) {
	case CTBCS_P2_RESET_NO_RESP:
		atrlen = 0;
		break;
	case CTBCS_P2_RESET_GET_ATR:
		atrlen = rc;
		break;
	case CTBCS_P2_RESET_GET_HIST:
		ct_error("CTAPI RESET: P2=GET_HIST not supported yet");
		return ctapi_error(rbuf, CTBCS_SW_BAD_PARAMS);
	}

	if (ct_buf_put(rbuf, atr, atrlen) < 0
	 || ctapi_put_sw(rbuf, 0x9000) < 0)
		return ctapi_error(rbuf, CTBCS_SW_BAD_LENGTH);

	return 0;
}

/*
 * Request ICC
 */
static int
ctapi_request_icc(ct_handle *h, char p1, char p2,
		ct_buf_t *sbuf, ct_buf_t *rbuf)
{
	time_t		timeout = 0;
	char		msgbuf[256], *message;

	switch (p2 >> 4) {
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
		return ctapi_error(rbuf, CTBCS_SW_BAD_PARAMS);
	}

	/* XXX use ct_tlv_* functions */
	while (ct_buf_avail(sbuf)) {
		unsigned char	type, len, val;

		if (ct_buf_get(sbuf, &type, 1) < 0
		 || ct_buf_get(sbuf, &len, 1) < 0
		 || ct_buf_avail(sbuf) < len)
			goto bad_length;

		if (type == 0x50) {
			ct_buf_get(sbuf, msgbuf, len);
			msgbuf[len] = '\0';
		} else if (type == 0x80) {
			if (len != 1)
				goto bad_length;
			ct_buf_get(sbuf, &val, 1);
			timeout = val;
		} else {
			/* Ignore unknown tag */
			ct_buf_get(sbuf, NULL, len);
		}
	}

	/* ctapi_reset does all the rest of the work */
	return ctapi_reset(h, p1, p2, rbuf, timeout, message);

bad_length:
	return ctapi_error(rbuf, CTBCS_SW_BAD_LENGTH);
}

static int
ctapi_status(ct_handle *h, ct_buf_t *rbuf)
{
	unsigned int	n;

	for (n = 0; n < 2; n++) {
		unsigned char	c;
		int		status;

		if (ct_card_status(h, n, &status) < 0)
			break;

		c = (status & IFD_CARD_PRESENT)
			? CTBCS_DATA_STATUS_CARD_CONNECT
			: CTBCS_DATA_STATUS_NOCARD;
		if (ct_buf_put(rbuf, &c, 1) < 0)
			goto bad_length;
	}

	if (ctapi_put_sw(rbuf, 0x9000) < 0)
		goto bad_length;

	return 0;

bad_length:
	return ctapi_error(rbuf, CTBCS_SW_BAD_LENGTH);
}

/*
 * Handle CTBCS messages
 */
static int
ctapi_control(ct_handle *h,
		const unsigned char *cmd, size_t cmd_len,
		void *rsp, size_t rsp_len)
{
	ct_buf_t	sbuf, rbuf;
	int		rc;
	int le = 0;

	if (rsp_len < 2)
		return ERR_INVALID;

	ct_buf_set(&sbuf, (void *) cmd, cmd_len);
	ct_buf_init(&rbuf, rsp, rsp_len);

	if (cmd_len == 4)
	{
		le = 0;
		ct_buf_get(&sbuf, NULL, 4);
	}
	else if (cmd_len == 5+cmd[4])
	{
		le = 0;
		ct_buf_get(&sbuf, NULL, 5);
	}
	else
	{
		le = cmd[4];
		ct_buf_get(&sbuf, NULL, 5);
	}
        if (le == 0) le = 256;

	if (cmd[0] != CTBCS_CLA) {
		ct_error("Bad CTBCS APDU, cla=0x%02x", cmd[0]);
		ctapi_error(&rbuf, CTBCS_SW_BAD_CLASS);
		goto out;
	}

	switch (cmd[1]) {
	case CTBCS_INS_RESET: /* old reset command */
	case 0x10:            /* new reset command */
		rc = ctapi_reset(h, cmd[2], cmd[3], &rbuf, 0, NULL);
		break;
	case CTBCS_INS_REQUEST_ICC:
		rc = ctapi_request_icc(h, cmd[2], cmd[3], &sbuf, &rbuf);
		break;
	case CTBCS_INS_STATUS:
		rc = ctapi_status(h, &rbuf);
		break;
	default:
		ct_error("Bad CTBCS APDU, ins=0x%02x", cmd[1]);
		rc = ctapi_error(&rbuf, CTBCS_SW_BAD_INS);
	}

	if (rc < 0)
		return rc;

	if (ct_buf_avail(&rbuf) > le + 2)
		ctapi_error(&rbuf, CTBCS_SW_BAD_LENGTH);

out:	return ct_buf_avail(&rbuf);
}

/*
 * Initialize card terminal #N.
 * As all the terminals are configured by libifd internally,
 * we ignore the port number
 */
char
CT_init(unsigned short ctn, unsigned short pn)
{
	struct CardTerminal *ct;
	ct_handle *h;
	ct_lock_handle lock;

	if ((ct=(struct CardTerminal *) malloc(sizeof(struct CardTerminal)))==(struct CardTerminal*)0)
		return ERR_MEMORY;
	if (!(h = ct_reader_connect(pn))) {
		free(ct);
		return ERR_INVALID;
	}
        if (ct_card_lock(h, 0, IFD_LOCK_EXCLUSIVE, &lock) < 0) {
		free(ct);
		return ERR_HTSI;
	}
	ct->ctn=ctn;
	ct->h=h;
	ct->lock=lock;
	ct->next=cardTerminals;
	cardTerminals=ct;
	return OK;
}

char
CT_close(unsigned short ctn)
{
	struct CardTerminal **ct,*next;

	for (ct=&cardTerminals; *ct && (*ct)->ctn!=ctn; ct = &(*ct)->next);
	if ((*ct)==(struct CardTerminal*)0)
		return ERR_INVALID;
	ct_reader_disconnect((*ct)->h);
	next=(*ct)->next;
	free((*ct));
	*ct=(*ct)->next;
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
	struct CardTerminal **ct;
	int rc;

	for (ct=&cardTerminals; *ct && (*ct)->ctn!=ctn; ct = &(*ct)->next);
	if ((*ct)==(struct CardTerminal*)0 || !sad || !dad)
		return ERR_INVALID;

#if 0
		ct_debug("CT_data(dad=%d lc=%u lr=%u cmd=%s",
				*dad, lc, *lr, ct_hexdump(cmd, lc));
#endif

	switch (*dad) {
		case CTAPI_DAD_ICC1:
			rc = ct_card_transact((*ct)->h, 0,
				cmd, (size_t)lc, rsp, (size_t)*lr);
			break;
		case CTAPI_DAD_ICC2:
			rc = ct_card_transact((*ct)->h, 1,
				cmd, (size_t)lc, rsp, (size_t)*lr);
			break;
		case CTAPI_DAD_CT:
			rc = ctapi_control((*ct)->h,
				cmd, lc, rsp, *lr);
			break;
		case CTAPI_DAD_HOST:
			ct_error("CT-API: host talking to itself - "
				"needs professional help?");
			return ERR_INVALID;
		default:
			ct_error("CT-API: unknown DAD %u", *dad);
			return ERR_INVALID;
	}

	/* Somewhat simplistic error translation */
	if (rc < 0)
		return ERR_INVALID;

	*lr = rc;
	return OK;
}
