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

static int mgr_status(ifd_reader_t *, int, ifd_tlv_parser_t *, ifd_tlv_builder_t *);
static int mgr_reset(ifd_reader_t *, int, ifd_tlv_parser_t *, ifd_tlv_builder_t *);

int
mgr_process(ifd_reader_t *reader, ifd_buf_t *argbuf, ifd_buf_t *resbuf)
{
	unsigned char		cmd, unit;
	ifd_tlv_parser_t	args;
	ifd_tlv_builder_t	resp;
	int			rc;

	memset(&args, 0, sizeof(args));
	ifd_tlv_builder_init(&resp, resbuf);

	if (ifd_buf_get(argbuf, &cmd, 1) < 0
	 || ifd_buf_get(argbuf, &unit, 1) < 0
	 || ifd_tlv_parse(&args, argbuf) < 0)
		return IFD_ERROR_INVALID_MSG;

	switch (cmd) {
	case IFD_CMD_STATUS:
		rc = mgr_status(reader, unit, &args, &resp);
		break;

	case IFD_CMD_RESET:
		rc = mgr_reset(reader, unit, &args, &resp);
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
mgr_status(ifd_reader_t *reader, int unit,
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
		if ((rc = ifd_card_status(reader, unit, &status)) < 0)
			return rc;
		ifd_tlv_put_int(resp, IFD_TAG_CARD_STATUS, status);
		break;
	}

	return 0;
}

/*
 * Reset card
 */
int
mgr_reset(ifd_reader_t *reader, int unit, ifd_tlv_parser_t *args, ifd_tlv_builder_t *resp)
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
