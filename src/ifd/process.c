/*
 * IFD resource manager protocol handling
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openct/ifd.h>
#include <openct/conf.h>
#include <openct/logging.h>
#include <openct/error.h>
#include <openct/tlv.h>

#include "internal.h"
#include "ifdhandler.h"

static int do_status(ifd_reader_t *, int,
			ct_tlv_parser_t *, ct_tlv_builder_t *);
static int do_lock(ct_socket_t *, ifd_reader_t *, int,
			ct_tlv_parser_t *, ct_tlv_builder_t *);
static int do_unlock(ct_socket_t *, ifd_reader_t *, int,
			ct_tlv_parser_t *, ct_tlv_builder_t *);
static int do_reset(ifd_reader_t *, int,
			ct_tlv_parser_t *, ct_tlv_builder_t *);
static int do_transact(ifd_reader_t *, int,
			ct_buf_t *, ct_buf_t *);

int
ifdhandler_process(ct_socket_t *sock, ifd_reader_t *reader,
		ct_buf_t *argbuf, ct_buf_t *resbuf)
{
	unsigned char		cmd, unit;
	ct_tlv_parser_t	args;
	ct_tlv_builder_t	resp;
	int			rc;

	/* Get command and target unit */
	if (ct_buf_get(argbuf, &cmd, 1) < 0
	 || ct_buf_get(argbuf, &unit, 1) < 0)
		return IFD_ERROR_INVALID_MSG;

	/* First, handle commands that don't do TLV encoded
	 * arguments - currently this is only CT_CMD_TRANSACT. */
	if (cmd == CT_CMD_TRANSACT) {
		/* Security - deny any APDUs if there's an
		 * exclusive lock held by some other client. */
		if ((rc = ifdhandler_check_lock(sock, unit, IFD_LOCK_EXCLUSIVE)) < 0)
			return rc;
		return do_transact(reader, unit, argbuf, resbuf);
	}

	memset(&args, 0, sizeof(args));
	ct_tlv_builder_init(&resp, resbuf);

	if (ct_tlv_parse(&args, argbuf) < 0)
		return IFD_ERROR_INVALID_MSG;

	switch (cmd) {
	case CT_CMD_STATUS:
		rc = do_status(reader, unit, &args, &resp);
		break;

	case CT_CMD_RESET:
		rc = do_reset(reader, unit, &args, &resp);
		break;

	case CT_CMD_LOCK:
		rc = do_lock(sock, reader, unit, &args, &resp);
		break;

	case CT_CMD_UNLOCK:
		rc = do_unlock(sock, reader, unit, &args, &resp);
		break;

	default:
		return IFD_ERROR_INVALID_CMD;
	}

	if (rc >= 0)
		rc = resp.error;

	return rc;
}

/*
 * Status query
 */
int
do_status(ifd_reader_t *reader, int unit,
		ct_tlv_parser_t *args, ct_tlv_builder_t *resp)
{
	int	n, rc, status;

	switch (unit) {
	case CT_UNIT_READER:
		ct_tlv_put_string(resp, CT_TAG_READER_NAME, reader->name);

		ct_tlv_put_tag(resp, CT_TAG_READER_UNITS);
		for (n = 0; n < reader->nslots; n++)
			ct_tlv_add_byte(resp, n);

		if (reader->flags & IFD_READER_DISPLAY)
			ct_tlv_add_byte(resp,  CT_UNIT_DISPLAY);
		if (reader->flags & IFD_READER_KEYPAD)
			ct_tlv_add_byte(resp,  CT_UNIT_KEYPAD);
		break;

	default:
		if (unit > reader->nslots)
			return IFD_ERROR_INVALID_SLOT;
		if ((rc = ifd_activate(reader)) < 0
		 || (rc = ifd_card_status(reader, unit, &status)) < 0)
			return rc;
		ct_tlv_put_int(resp, CT_TAG_CARD_STATUS, status);
		break;
	}

	return 0;
}

/*
 * Lock/unlock card
 */
int
do_lock(ct_socket_t *sock, ifd_reader_t *reader, int unit,
		ct_tlv_parser_t *args, ct_tlv_builder_t *resp)
{
	unsigned int	lock_type;
	ct_lock_handle	lock;
	int		rc;

	if (unit > reader->nslots)
		return IFD_ERROR_INVALID_SLOT;

	if (ct_tlv_get_int(args, CT_TAG_LOCKTYPE, &lock_type) < 0)
		return IFD_ERROR_MISSING_ARG;

	if ((rc = ifdhandler_lock(sock, unit, lock_type, &lock)) < 0)
		return rc;

	/* Return the lock handle */
	ct_tlv_put_int(resp, CT_TAG_LOCK, lock);
	return 0;
}

int
do_unlock(ct_socket_t *sock, ifd_reader_t *reader, int unit,
		ct_tlv_parser_t *args, ct_tlv_builder_t *resp)
{
	ct_lock_handle	lock;
	int		rc;

	if (unit > reader->nslots)
		return IFD_ERROR_INVALID_SLOT;

	if (ct_tlv_get_int(args, CT_TAG_LOCK, &lock) < 0)
		return IFD_ERROR_MISSING_ARG;

	if ((rc = ifdhandler_unlock(sock, unit, lock)) < 0)
		return rc;

	return 0;
}

/*
 * Reset card
 */
int
do_reset(ifd_reader_t *reader, int unit,
		ct_tlv_parser_t *args, ct_tlv_builder_t *resp)
{
	unsigned char	atr[64];
	char		msgbuf[128];
	const char	*message = NULL;
	unsigned int	timeout = 0;
	int		rc;

	if (unit > reader->nslots)
		return IFD_ERROR_INVALID_SLOT;

	/* See if we have timeout and/or message parameters */
	ct_tlv_get_int(args, CT_TAG_TIMEOUT, &timeout);
	if (ct_tlv_get_string(args, CT_TAG_MESSAGE, msgbuf, sizeof(msgbuf)) > 0)
		message = msgbuf;

	rc = ifd_card_request(reader, unit, timeout, message, atr, sizeof(atr));
	if (rc < 0)
		return rc;

	/* Add the ATR to the response */
	ct_tlv_put_tag(resp, CT_TAG_ATR);
	ct_tlv_add_bytes(resp, atr, rc);

	return 0;
}

/*
 * Transceive APDU
 */
int
do_transact(ifd_reader_t *reader, int unit, ct_buf_t *args, ct_buf_t *resp)
{
	int	rc;

	rc = ifd_card_command(reader, unit,
			ct_buf_head(args), ct_buf_avail(args),
			ct_buf_tail(resp), ct_buf_tailroom(resp));
	if (rc < 0)
		return rc;

	ct_buf_put(resp, NULL, rc);
	return 0;
}
