/*
 * CTAPI front-end for libopenct
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <openct/openct.h>
#include <openct/ifd.h>
#include <openct/buffer.h>
#include <openct/logging.h>
#include <openct/conf.h>
#include <openct/error.h>

#include "ctapi.h"

/*
 * Functions for setting the SW
 */
static int ctapi_put_sw(ct_buf_t * bp, unsigned int sw)
{
	unsigned char temp[2];

	temp[0] = sw >> 8;
	temp[1] = sw & 0xff;

	if (ct_buf_put(bp, temp, 2) < 0)
		return ERR_INVALID;
	return 2;
}

static int ctapi_error(ct_buf_t * bp, unsigned int sw)
{
	ct_buf_clear(bp);
	return ctapi_put_sw(bp, sw);
}

struct CardTerminal;

struct CardTerminalFile {
	unsigned int id;
	int (*gen) (struct CardTerminal * ct, ct_buf_t * buf, off_t start,
		    size_t length, size_t * size);
	struct CardTerminalFile *dir[20];
};

static struct CardTerminal {
	unsigned short ctn;
	ct_handle *h;
	unsigned int slots;
	ct_lock_handle lock;
	unsigned char sync;
	struct CardTerminalFile mf;
	struct CardTerminalFile ctcf;
	struct CardTerminalFile ctdir;
	struct CardTerminalFile iccdir[16];
	struct CardTerminalFile hostcf;
	struct CardTerminalFile hoststatus;
	struct CardTerminalFile *cwd;
	struct CardTerminal *next;
} *cardTerminals;

static int put(ct_buf_t * buf, off_t * start, size_t * length, size_t * size,
	       const unsigned char *data, size_t data_len)
{
	*size += data_len;
	while (data_len--) {
		if (*start == 0) {
			if (*length > 0) {
				if (buf != (ct_buf_t *) 0
				    && ct_buf_put(buf, data, 1) < 0)
					return -1;
				++data;
				--(*length);
			}
		} else
			--(*start);
	}
	return 0;
}

static int dir(struct CardTerminal *ct, ct_buf_t * buf, off_t start,
	       size_t length, size_t * size)
{
	struct CardTerminalFile **entry;

	if (!size)
		return -1;

	*size = 0;
	for (entry = &ct->cwd->dir[0]; *entry; ++entry) {
		unsigned char r[5];
		int rc;

		r[0] = ((*entry)->id >> 8) & 0xff;
		r[1] = ((*entry)->id) & 0xff;
		r[2] = 0x01;
		r[3] = 0x00;
		r[4] = 0x00;
		if ((rc = put(buf, &start, &length, size, r, 5)) < 0)
			return rc;
	}
	return 0;
}

static int ctcf(struct CardTerminal *ct, ct_buf_t * buf, off_t start,
		size_t length, size_t * size)
{
	return 0;
}

static int hostcf(struct CardTerminal *ct, ct_buf_t * buf, off_t start,
		  size_t length, size_t * size)
{
	unsigned char data[2];
	const char *version = "OpenCT";
	int rc;

	if (!size)
		return -1;

	*size = 0;
	data[0] = 0x01;
	data[1] = strlen(version);
	if ((rc = put(buf, &start, &length, size, data, 2)) < 0)
		return rc;
	if ((rc =
	     put(buf, &start, &length, size, (const unsigned char *)version,
		 strlen(version))) < 0)
		return rc;
	return 0;
}

static int hoststatus(struct CardTerminal *ct, ct_buf_t * buf, off_t start,
		      size_t length, size_t * size)
{
	return 0;
}

static int CardTerminalFile_read(struct CardTerminal *ct, ct_buf_t * buf,
				 off_t offset, size_t len)
{
	int rc;
	size_t size;

	if ((rc = ct->cwd->gen(ct, buf, offset, len, &size)) < 0)
		return rc;
	if (offset > size) {
		return ctapi_error(buf, 0x6b00);
	} else if (offset + len >= size) {
		if (ctapi_put_sw(buf, 0x9000) < 0)
			return ctapi_error(buf, CTBCS_SW_BAD_LENGTH);
		return 0;
	} else {
		if (ctapi_put_sw(buf, 0x6282) < 0)
			return ctapi_error(buf, CTBCS_SW_BAD_LENGTH);
		else
			return 0;
	}
}

static int CardTerminalFile_select(struct CardTerminal *ct, int id,
				   ct_buf_t * buf)
{
	struct CardTerminalFile *cur = (struct CardTerminalFile *)0;
	unsigned char r[12] =
	    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90,
		0x00
	};
	size_t size = 0;

	if (id == 0x3f00)
		cur = &ct->mf;
	else if (id == 0xff10)
		cur = &ct->hostcf;
	else if (id == 0xff11)
		cur = &ct->hoststatus;
	else {
		struct CardTerminalFile **entry;

		for (entry = &ct->cwd->dir[0]; *entry && (*entry)->id != id;
		     ++entry) ;
		cur = *entry;
	}
	if (cur == (struct CardTerminalFile *)0)
		return ctapi_error(buf, 0x6a82);
	ct->cwd = cur;
	ct->cwd->gen(ct, (ct_buf_t *) 0, 0, 1024, &size);
	r[2] = r[0] = size >> 8;
	r[3] = r[1] = size & 0xff;
	r[4] = ct->cwd->dir[0] != (struct CardTerminalFile *)0 ? 0x88 : 0x08;
	return ct_buf_put(buf, r, 12);
}

static int ctapi_reset(struct CardTerminal *ct, char p1, char p2,
		       ct_buf_t * rbuf, time_t timeout, const char *message)
{
	unsigned int atrlen = 0;
	unsigned char atr[64];
	int rc;

	switch (p1) {
	case CTBCS_UNIT_INTERFACE1:
	case CTBCS_UNIT_INTERFACE2:
		rc = ct_card_reset(ct->h, p1 - CTBCS_UNIT_INTERFACE1,
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

	if (rc == 4)
		ct->sync |= 1 << (p1 - CTBCS_UNIT_INTERFACE1);
	else
		ct->sync &= ~(1 << (p1 - CTBCS_UNIT_INTERFACE1));

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

	if (ct_buf_put(rbuf, atr, atrlen) < 0 || ctapi_put_sw(rbuf, 0x9000) < 0)
		return ctapi_error(rbuf, CTBCS_SW_BAD_LENGTH);

	return 0;
}

/*
 * Request ICC
 */
static int ctapi_request_icc(struct CardTerminal *ct, char p1, char p2,
			     ct_buf_t * sbuf, ct_buf_t * rbuf)
{
	time_t timeout = 0;
	char msgbuf[256], *message;

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

	while (ct_buf_avail(sbuf)) {
		unsigned char type, len, val;

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
	return ctapi_reset(ct, p1, p2, rbuf, timeout, message);

      bad_length:
	return ctapi_error(rbuf, CTBCS_SW_BAD_LENGTH);
}

static int ctapi_set_interface_parameter(struct CardTerminal *ct, char p1,
					 char p2, ct_buf_t * sbuf,
					 ct_buf_t * rbuf)
{
	unsigned char proto = 0xff;
	unsigned int slot;

	if ((p1 == 0x00) || (p1 > 2))
		return ctapi_error(rbuf, CTBCS_SW_BAD_PARAMS);
	slot = p1 - 1;
	if (p2 != 0x00)
		return ctapi_error(rbuf, CTBCS_SW_BAD_PARAMS);
	while (ct_buf_avail(sbuf)) {
		unsigned char type, len, val;

		if (ct_buf_get(sbuf, &type, 1) < 0
		    || ct_buf_get(sbuf, &len, 1) < 0
		    || ct_buf_avail(sbuf) < len)
			goto bad_length;

		if (type == CTBCS_TAG_TPP) {	/* Transmission Protocol Preference */
			if (len != 1)
				goto bad_length;
			ct_buf_get(sbuf, &proto, len);
			switch (proto) {
			case 0x01:
				proto = IFD_PROTOCOL_T0;	/* T=0 */
				break;
			case 0x02:
				proto = IFD_PROTOCOL_T1;	/* T=1 */
				break;
			case 0x80:
			case 0x81:
			case 0x82:
			case 0x83:
				return ctapi_error(rbuf,
						   CTBCS_SW_NOT_SUPPORTED);
			default:
				return ctapi_error(rbuf, CTBCS_SW_BAD_PARAMS);
			}
		} else if (type == CTBCS_TAG_TPC) {	/* Transmission Protocol Conventions */
			if (len != 1)
				goto bad_length;
			ct_buf_get(sbuf, &val, len);
			switch (val) {
			case 0x00:
			case 0x01:
				ctapi_error(rbuf, CTBCS_SW_NOT_SUPPORTED);
			default:
				return ctapi_error(rbuf, CTBCS_SW_BAD_PARAMS);
			}
		} else {
			/* Ignore unknown tag */
			ct_buf_get(sbuf, NULL, len);
			return ctapi_error(rbuf, CTBCS_SW_INVALID_TLV);
		}
	}
	if (proto == 0xff)
		return ctapi_error(rbuf, CTBCS_SW_BAD_PARAMS);
	if (!ct_card_set_protocol(ct->h, slot, proto))
		return ctapi_error(rbuf, CTBCS_SW_SUCCESS);
	else
		return ctapi_error(rbuf, CTBCS_SW_NOT_EXECUTABLE);
      bad_length:
	return ctapi_error(rbuf, CTBCS_SW_BAD_LENGTH);
}

static int ctapi_status(ct_handle * h, ct_buf_t * rbuf)
{
	unsigned int n;

	for (n = 0; n < 2; n++) {
		unsigned char c;
		int status;

		if (ct_card_status(h, n, &status) < 0)
			break;

		c = (status & IFD_CARD_PRESENT)
		    ? CTBCS_DATA_STATUS_CARD_CONNECT : CTBCS_DATA_STATUS_NOCARD;
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
static int ctapi_control(struct CardTerminal *ct, const unsigned char *cmd,
			 size_t cmd_len, void *rsp, size_t rsp_len)
{
	ct_buf_t sbuf, rbuf;
	int rc;
	int le = 0;
	unsigned char id[2];

	if (rsp_len < 2)
		return ERR_INVALID;

	if (cmd_len < 4)
		return ctapi_error(&rbuf, CTBCS_SW_BAD_LENGTH);

	ct_buf_set(&sbuf, (void *)cmd, cmd_len);
	ct_buf_init(&rbuf, rsp, rsp_len);

	if (cmd_len == 4) {
		le = 0;
		ct_buf_get(&sbuf, NULL, 4);
	} else if (cmd_len == 5 + cmd[4]) {
		le = 0;
		ct_buf_get(&sbuf, NULL, 5);
	} else {
		le = cmd[4];
		ct_buf_get(&sbuf, NULL, 5);
	}
	if (le == 0)
		le = 256;

	switch ((cmd[0] << 8) | cmd[1]) {
	case (CTBCS_CLA << 8) | 0x10:	/* B1 compatibility reset command */
		if (cmd_len != 5)
			return ctapi_error(&rbuf, CTBCS_SW_BAD_LENGTH);
		if ((cmd_len == 5) && (cmd[4] != 0x00))
			return ctapi_error(&rbuf, CTBCS_SW_BAD_LE);
		rc = ctapi_reset(ct, cmd[2], cmd[3], &rbuf, 0, NULL);
		break;
	case (CTBCS_CLA << 8) | CTBCS_INS_RESET:	/* RESET_CT command */
		if (cmd_len > 5)
			return ctapi_error(&rbuf, CTBCS_SW_BAD_LENGTH);
		if ((cmd_len == 5) && (cmd[4] != 0x00))
			return ctapi_error(&rbuf, CTBCS_SW_BAD_LE);
		rc = ctapi_reset(ct, cmd[2], cmd[3], &rbuf, 0, NULL);
		break;
	case (CTBCS_CLA << 8) | CTBCS_INS_REQUEST_ICC:
		rc = ctapi_request_icc(ct, cmd[2], cmd[3], &sbuf, &rbuf);
		break;
	case (CTBCS_CLA << 8) | CTBCS_INS_STATUS:
		rc = ctapi_status(ct->h, &rbuf);
		break;
	case (0x00 << 8) | 0xb0:
		rc = CardTerminalFile_read(ct, &rbuf, (cmd[2] << 8) | cmd[3],
					   le);
		break;
	case (0x00 << 8) | 0xa4:
		if (cmd[4] != 2 || ct_buf_get(&sbuf, id, 2) == -1) {
			ct_error("Bad SELECT FILE ID");
			rc = ctapi_error(&rbuf, CTBCS_SW_BAD_CLASS);
		} else
			rc = CardTerminalFile_select(ct, (id[0] << 8) | id[1],
						     &rbuf);
		break;
	case (CTBCS_CLA_2 << 8) | CTBCS_INS_SET_INTERFACE_PARAM:
		rc = ctapi_set_interface_parameter(ct, cmd[2], cmd[3], &sbuf,
						   &rbuf);
		break;
	default:
		if (cmd[0] != CTBCS_CLA && cmd[0] != 0x00) {
			ct_error("Bad CTBCS APDU, cla=0x%02x", cmd[0]);
			rc = ctapi_error(&rbuf, CTBCS_SW_BAD_CLASS);
		} else {
			ct_error("Bad CTBCS APDU, ins=0x%02x", cmd[1]);
			rc = ctapi_error(&rbuf, CTBCS_SW_BAD_INS);
		}
	}

	if (rc < 0)
		return rc;

	if (ct_buf_avail(&rbuf) > le + 2)
		return ctapi_error(&rbuf, CTBCS_SW_BAD_LENGTH);

	return ct_buf_avail(&rbuf);
}

/*
 * Handle card transactions
 */
static int ctapi_transact(struct CardTerminal *ct, int nslot,
			  const unsigned char *cmd, size_t cmd_len, void *rsp,
			  size_t rsp_len)
{
	static const unsigned char select_kvk[11] =
	    { 0x00, 0xa4, 0x04, 0x00, 0x06, 0xd2, 0x80, 0x00, 0x00, 0x01,
		0x01
	};
	static const unsigned char read_binary[2] = { 0x00, 0xb0 };
	ct_buf_t sbuf, rbuf;
	int rc;
	int le = 0;

	ct_buf_set(&sbuf, (void *)cmd, cmd_len);
	ct_buf_init(&rbuf, rsp, rsp_len);

	if (cmd_len == 4) {
		le = 0;
		ct_buf_get(&sbuf, NULL, 4);
	} else if (cmd_len == 5 + (unsigned char)cmd[4]) {
		le = 0;
		ct_buf_get(&sbuf, NULL, 5);
	} else {
		le = (unsigned char)cmd[4];
		ct_buf_get(&sbuf, NULL, 5);
	}
	if (le == 0)
		le = 256;

	if (cmd_len == 11 && memcmp(cmd, select_kvk, 11) == 0) {
		if (ctapi_put_sw(&rbuf, 0x9000) < 0)
			return ctapi_error(&rbuf, CTBCS_SW_BAD_LENGTH);
		return ct_buf_avail(&rbuf);
	} else if ((ct->sync & (1 << nslot)) && cmd_len >= 5
		   && memcmp(cmd, read_binary, 2) == 0) {
		unsigned char buf[256];

		if ((rc =
		     ct_card_read_memory(ct->h, nslot,
					 (((unsigned char)cmd[2]) << 8) |
					 ((unsigned char)cmd[3]), buf,
					 le)) < 0) {
#if 0
			printf("rc is %d\n", rc);
#endif
			return rc;
		}
		if (ct_buf_put(&rbuf, buf, rc) < 0
		    || ctapi_put_sw(&rbuf, 0x9000) < 0)
			return ctapi_error(&rbuf, CTBCS_SW_BAD_LENGTH);
		return ct_buf_avail(&rbuf);
	} else
		return ct_card_transact(ct->h, 0, cmd, cmd_len, rsp, rsp_len);
}

/*
 * Initialize card terminal #N.
 */
char CT_init(unsigned short ctn, unsigned short pn)
{
	struct CardTerminal *ct;
	ct_handle *h;
	ct_info_t info;
	int i;

	ct = (struct CardTerminal *)malloc(sizeof(struct CardTerminal));
	if (ct == NULL)
		return ERR_MEMORY;
	if (!(h = ct_reader_connect(pn))) {
		free(ct);
		return ERR_INVALID;
	}
	memset(ct, 0, sizeof(struct CardTerminal));
	ct->ctn = ctn;
	ct->h = h;
	ct->next = cardTerminals;
	cardTerminals = ct;
	ct->cwd = &ct->mf;
	ct_reader_info(pn, &info);
	ct->mf.id = 0x3f00;
	ct->mf.gen = dir;
	ct->mf.dir[0] = &ct->mf;
	ct->mf.dir[1] = &ct->ctcf;
	ct->mf.dir[2] = &ct->ctdir;
	for (i = 0; i < info.ct_slots; ++i) {
		ct->mf.dir[3 + i] = &ct->iccdir[i];
	}
	ct->ctcf.id = 0x0020;
	ct->ctcf.gen = ctcf;
	ct->ctcf.dir[0] = &ct->mf;
	ct->ctdir.id = 0x7f60;
	ct->ctdir.gen = dir;
	ct->ctdir.dir[0] = &ct->mf;
	for (i = 0; i < info.ct_slots; ++i) {
		ct->iccdir[i].id = 0x7f70 + i;
		ct->iccdir[i].gen = dir;
		ct->iccdir[i].dir[0] = &ct->iccdir[i];
	}
	ct->hostcf.id = 0xff10;
	ct->hostcf.gen = hostcf;
	ct->hostcf.dir[0] = &ct->hostcf;
	ct->hoststatus.id = 0xff11;
	ct->hoststatus.gen = hoststatus;
	ct->hoststatus.dir[0] = &ct->hoststatus;
	if (ct_card_lock(h, 0, IFD_LOCK_EXCLUSIVE, &ct->lock) < 0) {
		CT_close(ctn);
		return ERR_HTSI;
	}
	return OK;
}

char CT_close(unsigned short ctn)
{
	struct CardTerminal **ct, *this;

	for (ct = &cardTerminals; *ct && (*ct)->ctn != ctn; ct = &(*ct)->next) ;
	this = *ct;
	if (!this)
		return ERR_INVALID;
	ct_reader_disconnect(this->h);
	ct = &(this->next);

	this->next = NULL;
	free(this);
	return OK;
}

char CT_data(unsigned short ctn, unsigned char *dad, unsigned char *sad,
	     unsigned short lc, unsigned char *cmd, unsigned short *lr,
	     unsigned char *rsp)
{
	struct CardTerminal **ct;
	int rc;

	for (ct = &cardTerminals; *ct && (*ct)->ctn != ctn; ct = &(*ct)->next) ;
	if ((*ct) == (struct CardTerminal *)0 || !sad || !dad)
		return ERR_INVALID;

#if 0
	ct_debug("CT_data(dad=%d lc=%u lr=%u cmd=%s",
		 *dad, lc, *lr, ct_hexdump(cmd, lc));
#endif

	switch (*dad) {
	case CTAPI_DAD_ICC1:
		rc = ctapi_transact(*ct, 0, cmd, lc, rsp, *lr);
		break;
	case CTAPI_DAD_ICC2:
		rc = ctapi_transact(*ct, 1, cmd, lc, rsp, *lr);
		break;
	case CTAPI_DAD_CT:
		rc = ctapi_control(*ct, cmd, lc, rsp, *lr);
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
