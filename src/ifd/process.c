/*
 * IFD resource manager protocol handling
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openct/ifd.h>
#include <openct/conf.h>
#include <openct/logging.h>
#include <openct/error.h>
#include <openct/tlv.h>

#include "ifdhandler.h"

static struct cmd_name {
	unsigned int value;
	const char *str;
} cmd_name[] = {
	{
	CT_CMD_STATUS, "CT_CMD_STATUS"}, {
	CT_CMD_LOCK, "CT_CMD_LOCK"}, {
	CT_CMD_UNLOCK, "CT_CMD_UNLOCK"}, {
	CT_CMD_RESET, "CT_CMD_RESET"}, {
	CT_CMD_REQUEST_ICC, "CT_CMD_REQUEST_ICC"}, {
	CT_CMD_EJECT_ICC, "CT_CMD_EJECT_ICC"}, {
	CT_CMD_OUTPUT, "CT_CMD_OUTPUT"}, {
	CT_CMD_INPUT, "CT_CMD_INPUT"}, {
	CT_CMD_PERFORM_VERIFY, "CT_CMD_PERFORM_VERIFY"}, {
	CT_CMD_CHANGE_PIN, "CT_CMD_CHANGE_PIN"}, {
	CT_CMD_MEMORY_READ, "CT_CMD_MEMORY_READ"}, {
	CT_CMD_MEMORY_WRITE, "CT_CMD_MEMORY_WRITE"}, {
	CT_CMD_TRANSACT_OLD, "CT_CMD_TRANSACT_OLD"}, {
	CT_CMD_TRANSACT, "CT_CMD_TRANSACT"}, {
	CT_CMD_SET_PROTOCOL, "CT_CMD_SET_PROTOCOL"}, {
0, NULL},};

static const char *get_cmd_name(unsigned int cmd)
{
	struct cmd_name *c;

	for (c = cmd_name; c->str; c++) {
		if (c->value == cmd)
			return c->str;
	}
	return "<unknown>";
}

static int do_before_command(ifd_reader_t *);
static int do_after_command(ifd_reader_t *);
static int do_status(ifd_reader_t *, int,
		     ct_tlv_parser_t *, ct_tlv_builder_t *);
static int do_output(ifd_reader_t *, int,
		     ct_tlv_parser_t *, ct_tlv_builder_t *);
static int do_lock(ct_socket_t *, ifd_reader_t *, int,
		   ct_tlv_parser_t *, ct_tlv_builder_t *);
static int do_unlock(ct_socket_t *, ifd_reader_t *, int,
		     ct_tlv_parser_t *, ct_tlv_builder_t *);
static int do_reset(ifd_reader_t *, int, ct_tlv_parser_t *, ct_tlv_builder_t *);
static int do_eject(ifd_reader_t *, int, ct_tlv_parser_t *, ct_tlv_builder_t *);
static int do_verify(ifd_reader_t *, int,
		     ct_tlv_parser_t *, ct_tlv_builder_t *);
static int do_transact(ifd_reader_t *, int,
		       ct_tlv_parser_t *, ct_tlv_builder_t *);
static int do_memory_read(ifd_reader_t *, int,
			  ct_tlv_parser_t *, ct_tlv_builder_t *);
static int do_memory_write(ifd_reader_t *, int,
			   ct_tlv_parser_t *, ct_tlv_builder_t *);
static int do_transact_old(ifd_reader_t *, int, ct_buf_t *, ct_buf_t *);
static int do_set_protocol(ifd_reader_t *, int,
			   ct_tlv_parser_t *, ct_tlv_builder_t *);

int ifdhandler_process(ct_socket_t * sock, ifd_reader_t * reader,
		       ct_buf_t * argbuf, ct_buf_t * resbuf)
{
	unsigned char cmd, unit;
	ct_tlv_parser_t args;
	ct_tlv_builder_t resp;
	int rc;

	/* Get command and target unit */
	if (ct_buf_get(argbuf, &cmd, 1) < 0 || ct_buf_get(argbuf, &unit, 1) < 0)
		return IFD_ERROR_INVALID_MSG;

	ifd_debug(1, "ifdhandler_process(cmd=%s, unit=%u)",
		  get_cmd_name(cmd), unit);

	/* First, handle commands that don't do TLV encoded
	 * arguments - currently this is only CT_CMD_TRANSACT. */
	if (cmd == CT_CMD_TRANSACT_OLD) {
		/* Security - deny any APDUs if there's an
		 * exclusive lock held by some other client. */
		if ((rc =
		     ifdhandler_check_lock(sock, unit, IFD_LOCK_EXCLUSIVE)) < 0)
			return rc;
		return do_transact_old(reader, unit, argbuf, resbuf);
	}

	if ((rc = do_before_command(reader)) < 0) {
		return rc;
	}

	memset(&args, 0, sizeof(args));
	if (ct_tlv_parse(&args, argbuf) < 0)
		return IFD_ERROR_INVALID_MSG;
	if (args.use_large_tags)
		sock->use_large_tags = 1;

	ct_tlv_builder_init(&resp, resbuf, sock->use_large_tags);

	switch (cmd) {
	case CT_CMD_STATUS:
		rc = do_status(reader, unit, &args, &resp);
		break;

	case CT_CMD_OUTPUT:
		rc = do_output(reader, unit, &args, &resp);
		break;

	case CT_CMD_RESET:
	case CT_CMD_REQUEST_ICC:
		rc = do_reset(reader, unit, &args, &resp);
		break;

	case CT_CMD_EJECT_ICC:
		rc = do_eject(reader, unit, &args, &resp);
		break;

	case CT_CMD_PERFORM_VERIFY:
		rc = do_verify(reader, unit, &args, &resp);
		break;

	case CT_CMD_LOCK:
		rc = do_lock(sock, reader, unit, &args, &resp);
		break;

	case CT_CMD_UNLOCK:
		rc = do_unlock(sock, reader, unit, &args, &resp);
		break;

	case CT_CMD_MEMORY_READ:
		rc = do_memory_read(reader, unit, &args, &resp);
		break;

	case CT_CMD_MEMORY_WRITE:
		rc = do_memory_write(reader, unit, &args, &resp);
		break;

	case CT_CMD_TRANSACT:
		rc = do_transact(reader, unit, &args, &resp);
		break;
	case CT_CMD_SET_PROTOCOL:
		rc = do_set_protocol(reader, unit, &args, &resp);
		break;
	default:
		return IFD_ERROR_INVALID_CMD;
	}

	if (rc >= 0)
		rc = resp.error;

	/*
	 * TODO consider checking error
	 */
	do_after_command(reader);

	return rc;
}

/*
 * Before command
 */
static int do_before_command(ifd_reader_t * reader)
{
	return ifd_before_command(reader);
}

/*
 * After command
 */
static int do_after_command(ifd_reader_t * reader)
{
	return ifd_after_command(reader);
}

/*
 * Status query
 */
static int do_status(ifd_reader_t * reader, int unit, ct_tlv_parser_t * args,
		     ct_tlv_builder_t * resp)
{
	int n, rc, status;

	switch (unit) {
	case CT_UNIT_READER:
		ct_tlv_put_string(resp, CT_TAG_READER_NAME, reader->name);

		ct_tlv_put_tag(resp, CT_TAG_READER_UNITS);
		for (n = 0; n < reader->nslots; n++)
			ct_tlv_add_byte(resp, n);

		if (reader->flags & IFD_READER_DISPLAY)
			ct_tlv_add_byte(resp, CT_UNIT_DISPLAY);
		if (reader->flags & IFD_READER_KEYPAD)
			ct_tlv_add_byte(resp, CT_UNIT_KEYPAD);
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
 * Output string to reader's display
 */
static int do_output(ifd_reader_t * reader, int unit, ct_tlv_parser_t * args,
		     ct_tlv_builder_t * resp)
{
	char msgbuf[128];
	const char *message = NULL;

	if (unit > CT_UNIT_READER)
		return IFD_ERROR_INVALID_ARG;

	/* See if we have message parameter */
	if (ct_tlv_get_string(args, CT_TAG_MESSAGE, msgbuf, sizeof(msgbuf)) > 0)
		message = msgbuf;

	return ifd_output(reader, message);
}

/*
 * Lock/unlock card
 */
static int do_lock(ct_socket_t * sock, ifd_reader_t * reader, int unit,
		   ct_tlv_parser_t * args, ct_tlv_builder_t * resp)
{
	unsigned int lock_type;
	ct_lock_handle lock;
	int rc;

	if (unit > reader->nslots)
		return IFD_ERROR_INVALID_SLOT;

	if (ct_tlv_get_int(args, CT_TAG_LOCKTYPE, &lock_type) == 0)
		return IFD_ERROR_MISSING_ARG;

	if ((rc = ifdhandler_lock(sock, unit, lock_type, &lock)) < 0)
		return rc;

	/* Return the lock handle */
	ct_tlv_put_int(resp, CT_TAG_LOCK, lock);
	return 0;
}

static int do_unlock(ct_socket_t * sock, ifd_reader_t * reader, int unit,
		     ct_tlv_parser_t * args, ct_tlv_builder_t * resp)
{
	ct_lock_handle lock;
	int rc;

	if (unit > reader->nslots)
		return IFD_ERROR_INVALID_SLOT;

	if (ct_tlv_get_int(args, CT_TAG_LOCK, &lock) == 0)
		return IFD_ERROR_MISSING_ARG;

	if ((rc = ifdhandler_unlock(sock, unit, lock)) < 0)
		return rc;

	return 0;
}

/*
 * Reset card
 */
static int do_reset(ifd_reader_t * reader, int unit, ct_tlv_parser_t * args,
		    ct_tlv_builder_t * resp)
{
	unsigned char atr[64];
	char msgbuf[128];
	const char *message = NULL;
	unsigned int timeout = 0;
	int rc;

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
	if (rc != 0) {
		ct_tlv_put_tag(resp, CT_TAG_ATR);
		ct_tlv_add_bytes(resp, atr, rc);
	}

	return 0;
}

/*
 * Eject card
 */
static int do_eject(ifd_reader_t * reader, int unit, ct_tlv_parser_t * args,
		    ct_tlv_builder_t * resp)
{
	char msgbuf[128];
	const char *message = NULL;
	unsigned int timeout = 0;
	int rc;

	if (unit > reader->nslots)
		return IFD_ERROR_INVALID_SLOT;

	/* See if we have timeout and/or message parameters */
	ct_tlv_get_int(args, CT_TAG_TIMEOUT, &timeout);
	if (ct_tlv_get_string(args, CT_TAG_MESSAGE, msgbuf, sizeof(msgbuf)) > 0)
		message = msgbuf;

	rc = ifd_card_eject(reader, unit, timeout, message);
	if (rc < 0)
		return rc;

	return 0;
}

/*
 * Request PIN through key pad and have card verify it
 */
static int do_verify(ifd_reader_t * reader, int unit, ct_tlv_parser_t * args,
		     ct_tlv_builder_t * resp)
{
	char msgbuf[128];
	unsigned char *data;
	size_t data_len;
	const char *message = NULL;
	unsigned int timeout = 0;
	int rc;

	if (unit > reader->nslots)
		return IFD_ERROR_INVALID_SLOT;

	/* See if we have timeout and/or message parameters */
	ct_tlv_get_int(args, CT_TAG_TIMEOUT, &timeout);
	if (ct_tlv_get_string(args, CT_TAG_MESSAGE, msgbuf, sizeof(msgbuf)) > 0)
		message = msgbuf;
	if (!ct_tlv_get_opaque(args, CT_TAG_PIN_DATA, &data, &data_len))
		return IFD_ERROR_MISSING_ARG;

	rc = ifd_card_perform_verify(reader, unit, timeout, message,
				     data, data_len,
				     (unsigned char *)msgbuf, sizeof(msgbuf));
	if (rc < 0)
		return rc;

	ct_tlv_put_tag(resp, CT_TAG_CARD_RESPONSE);
	ct_tlv_add_bytes(resp, (const unsigned char *)msgbuf, rc);
	return 0;
}

/*
 * Transceive APDU
 */
static int do_transact(ifd_reader_t * reader, int unit, ct_tlv_parser_t * args,
		       ct_tlv_builder_t * resp)
{
	unsigned char replybuf[258];
	unsigned char *data;
	size_t data_len;
	unsigned int timeout = 0;
	int rc;

	if (unit > reader->nslots)
		return IFD_ERROR_INVALID_SLOT;

	ct_tlv_get_int(args, CT_TAG_TIMEOUT, &timeout);
	if (!ct_tlv_get_opaque(args, CT_TAG_CARD_REQUEST, &data, &data_len))
		return IFD_ERROR_MISSING_ARG;

	rc = ifd_card_command(reader, unit, data, data_len,
			      replybuf, sizeof(replybuf));
	if (rc < 0)
		return rc;

	ct_tlv_put_tag(resp, CT_TAG_CARD_RESPONSE);
	ct_tlv_add_bytes(resp, replybuf, rc);
	return 0;
}

static int do_transact_old(ifd_reader_t * reader, int unit, ct_buf_t * args,
			   ct_buf_t * resp)
{
	int rc;

	rc = ifd_card_command(reader, unit,
			      ct_buf_head(args), ct_buf_avail(args),
			      ct_buf_tail(resp), ct_buf_tailroom(resp));
	if (rc < 0)
		return rc;

	ct_buf_put(resp, NULL, rc);
	return 0;
}

static int do_set_protocol(ifd_reader_t * reader, int unit,
			   ct_tlv_parser_t * args, ct_tlv_builder_t * resp)
{
	unsigned int protocol = 0xFF;
	int rc;

	if (unit > reader->nslots)
		return IFD_ERROR_INVALID_SLOT;

	if (ct_tlv_get_int(args, CT_TAG_PROTOCOL, &protocol) == 0)
		return IFD_ERROR_MISSING_ARG;

	rc = ifd_set_protocol(reader, unit, protocol);
	if (rc < 0)
		return rc;

	return 0;
}

/*
 * Synchronous ICC read/write
 */
static int do_memory_write(ifd_reader_t * reader, int unit,
			   ct_tlv_parser_t * args, ct_tlv_builder_t * resp)
{
	unsigned char *data;
	unsigned int data_len;
	unsigned int address;
	int rc;

	if (unit > reader->nslots)
		return IFD_ERROR_INVALID_SLOT;

	if (ct_tlv_get_int(args, CT_TAG_ADDRESS, &address) == 0
	    || !ct_tlv_get_opaque(args, CT_TAG_DATA, &data, &data_len))
		return IFD_ERROR_MISSING_ARG;

	rc = ifd_card_write_memory(reader, unit, address, data, data_len);
	if (rc < 0)
		return rc;

	return 0;
}

static int do_memory_read(ifd_reader_t * reader, int unit,
			  ct_tlv_parser_t * args, ct_tlv_builder_t * resp)
{
	unsigned char data[CT_SOCKET_BUFSIZ];
	unsigned int data_len;
	unsigned int address;
	int rc;

	if (unit > reader->nslots)
		return IFD_ERROR_INVALID_SLOT;

	if (ct_tlv_get_int(args, CT_TAG_ADDRESS, &address) == 0
	    || !ct_tlv_get_int(args, CT_TAG_COUNT, &data_len))
		return IFD_ERROR_MISSING_ARG;

	if (data_len > sizeof(data))
		data_len = sizeof(data);

	rc = ifd_card_read_memory(reader, unit, address, data, data_len);
	if (rc < 0)
		return rc;

	ct_tlv_put_opaque(resp, CT_TAG_DATA, data, rc);
	return 0;
}
