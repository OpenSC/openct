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
#include <ifd/core.h>
#include <ifd/conf.h>
#include <ifd/logging.h>
#include <ifd/error.h>
#include <ifd/tlv.h>

#include "internal.h"

static int do_status(ifd_reader_t *, int,
			ifd_tlv_parser_t *, ifd_tlv_builder_t *);
static int do_lock(ifd_socket_t *, ifd_reader_t *, int,
			ifd_tlv_parser_t *, ifd_tlv_builder_t *);
static int do_unlock(ifd_socket_t *, ifd_reader_t *, int,
			ifd_tlv_parser_t *, ifd_tlv_builder_t *);
static int do_reset(ifd_reader_t *, int,
			ifd_tlv_parser_t *, ifd_tlv_builder_t *);
static int do_transact(ifd_reader_t *, int,
			ifd_buf_t *, ifd_buf_t *);

int
mgr_process(ifd_socket_t *sock, ifd_reader_t *reader,
		ifd_buf_t *argbuf, ifd_buf_t *resbuf)
{
	unsigned char		cmd, unit;
	ifd_tlv_parser_t	args;
	ifd_tlv_builder_t	resp;
	int			rc;

	/* Get command and target unit */
	if (ifd_buf_get(argbuf, &cmd, 1) < 0
	 || ifd_buf_get(argbuf, &unit, 1) < 0)
		return IFD_ERROR_INVALID_MSG;

	/* First, handle commands that don't do TLV encoded
	 * arguments - currently this is only IFD_CMD_TRANSACT. */
	if (cmd == IFD_CMD_TRANSACT) {
		/* Security - deny any APDUs if there's an
		 * exclusive lock held by some other client. */
		if ((rc = mgr_check_lock(sock, unit, IFD_LOCK_EXCLUSIVE)) < 0)
			return rc;
		return do_transact(reader, unit, argbuf, resbuf);
	}

	memset(&args, 0, sizeof(args));
	ifd_tlv_builder_init(&resp, resbuf);

	if (ifd_tlv_parse(&args, argbuf) < 0)
		return IFD_ERROR_INVALID_MSG;

	switch (cmd) {
	case IFD_CMD_STATUS:
		rc = do_status(reader, unit, &args, &resp);
		break;

	case IFD_CMD_RESET:
		rc = do_reset(reader, unit, &args, &resp);
		break;

	case IFD_CMD_LOCK:
		rc = do_lock(sock, reader, unit, &args, &resp);
		break;

	case IFD_CMD_UNLOCK:
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
		ifd_tlv_parser_t *args, ifd_tlv_builder_t *resp)
{
	int	n, rc, status;

	switch (unit) {
	case IFD_UNIT_CT:
		ifd_tlv_put_string(resp, IFD_TAG_READER_NAME, reader->name);

		ifd_tlv_put_tag(resp, IFD_TAG_READER_UNITS);
		for (n = 0; n < reader->nslots; n++)
			ifd_tlv_add_byte(resp, n);

		if (reader->flags & IFD_READER_DISPLAY)
			ifd_tlv_add_byte(resp,  IFD_UNIT_DISPLAY);
		if (reader->flags & IFD_READER_KEYPAD)
			ifd_tlv_add_byte(resp,  IFD_UNIT_KEYPAD);
		break;

	default:
		if (unit > reader->nslots)
			return IFD_ERROR_INVALID_SLOT;
		if ((rc = ifd_activate(reader)) < 0
		 || (rc = ifd_card_status(reader, unit, &status)) < 0)
			return rc;
		ifd_tlv_put_int(resp, IFD_TAG_CARD_STATUS, status);
		break;
	}

	return 0;
}

/*
 * Lock/unlock card
 */
int
do_lock(ifd_socket_t *sock, ifd_reader_t *reader, int unit,
		ifd_tlv_parser_t *args, ifd_tlv_builder_t *resp)
{
	unsigned int	lock_type;
	ct_lock_handle	lock;
	int		rc;

	if (unit > reader->nslots)
		return IFD_ERROR_INVALID_SLOT;

	if (ifd_tlv_get_int(args, IFD_TAG_LOCKTYPE, &lock_type) < 0)
		return IFD_ERROR_MISSING_ARG;

	if ((rc = mgr_lock(sock, unit, lock_type, &lock)) < 0)
		return rc;

	/* Return the lock handle */
	ifd_tlv_put_int(resp, IFD_TAG_LOCK, lock);
	return 0;
}

int
do_unlock(ifd_socket_t *sock, ifd_reader_t *reader, int unit,
		ifd_tlv_parser_t *args, ifd_tlv_builder_t *resp)
{
	ct_lock_handle	lock;
	int		rc;

	if (unit > reader->nslots)
		return IFD_ERROR_INVALID_SLOT;

	if (ifd_tlv_get_int(args, IFD_TAG_LOCK, &lock) < 0)
		return IFD_ERROR_MISSING_ARG;

	if ((rc = mgr_unlock(sock, unit, lock)) < 0)
		return rc;

	return 0;
}

/*
 * Reset card
 */
int
do_reset(ifd_reader_t *reader, int unit, ifd_tlv_parser_t *args, ifd_tlv_builder_t *resp)
{
	unsigned char	atr[64];
	char		msgbuf[128];
	const char	*message = NULL;
	unsigned int	timeout = 0;
	int		rc;

	if (unit > reader->nslots)
		return IFD_ERROR_INVALID_SLOT;

	/* See if we have timeout and/or message parameters */
	ifd_tlv_get_int(args, IFD_TAG_TIMEOUT, &timeout);
	if (ifd_tlv_get_string(args, IFD_TAG_MESSAGE, msgbuf, sizeof(msgbuf)) > 0)
		message = msgbuf;

	if ((rc = ifd_card_request(reader, unit, timeout, message, atr, sizeof(atr))) < 0)
		return rc;

	/* Add the ATR to the response */
	ifd_tlv_put_tag(resp, IFD_TAG_ATR);
	ifd_tlv_add_bytes(resp, atr, rc);

	return 0;
}

/*
 * Transceive APDU
 */
int
do_transact(ifd_reader_t *reader, int unit, ifd_buf_t *args, ifd_buf_t *resp)
{
	ifd_apdu_t	apdu;
	int		rc;

	apdu.snd_buf = ifd_buf_head(args);
	apdu.snd_len = ifd_buf_avail(args);
	apdu.rcv_buf = ifd_buf_tail(resp);
	apdu.rcv_len = ifd_buf_tailroom(resp);

	if ((rc = ifd_card_command(reader, unit, &apdu)) < 0)
		return rc;

	ifd_buf_put(resp, NULL, rc);
	return 0;
}
