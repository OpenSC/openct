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

#include "internal.h"

static int mgr_status(ifd_reader_t *, int, ifd_tlv_parser_t *, ifd_tlv_builder_t *);

int
mgr_process(ifd_reader_t *reader, ifd_buf_t *argbuf, ifd_buf_t *resbuf)
{
	unsigned char		cmd, unit;
	ifd_tlv_parser_t	args;
	ifd_tlv_builder_t	resp;
	int			rc;

	memset(&args, 0, sizeof(args));
	mgr_tlv_builder_init(&resp, resbuf);

	if (ifd_buf_get(argbuf, &cmd, 1) < 0
	 || ifd_buf_get(argbuf, &unit, 1) < 0
	 || mgr_tlv_parse(&args, argbuf) < 0)
		return IFD_ERROR_INVALID_MSG;

	switch (cmd) {
	case IFD_CMD_STATUS:
		rc = mgr_status(reader, unit, &args, &resp);
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
		mgr_tlv_put_string(resp, IFD_TAG_READER_NAME, reader->name);

		mgr_tlv_put_tag(resp, IFD_TAG_READER_UNITS);
		for (n = 0; n < reader->nslots; n++)
			mgr_tlv_add_byte(resp, n);

		if (reader->flags & IFD_READER_DISPLAY)
			mgr_tlv_add_byte(resp,  IFD_UNIT_DISPLAY);
		if (reader->flags & IFD_READER_KEYPAD)
			mgr_tlv_add_byte(resp,  IFD_UNIT_KEYPAD);
		break;

	default:
		if (unit > reader->nslots)
			return IFD_ERROR_INVALID_SLOT;
		if ((rc = ifd_card_status(reader, unit, &status)) < 0)
			return rc;
		mgr_tlv_put_int(resp, IFD_TAG_CARD_STATUS, status);
		break;
	}

	return 0;
}

/*
 * Handle TLV encoding
 */
int
mgr_tlv_parse(ifd_tlv_parser_t *parser, ifd_buf_t *bp)
{
	unsigned int	avail;
	unsigned char	*p, tag, len;

	while ((avail = ifd_buf_avail(bp)) != 0) {
		if (avail < 2)
			return -1;

		p = ifd_buf_head(bp);
		tag = p[0];
		len = p[1];

		if (len == 0 || 2 + len > avail)
			return -1;

		parser->v[tag] = p + 1;

		ifd_buf_get(bp, NULL, 2 + len);
	}

	return 0;
}

int
mgr_tlv_get_string(ifd_tlv_parser_t *parser, ifd_tag_t tag,
			char *buf, size_t size)
{
	unsigned char	*p;
	unsigned int	len;

	if (tag > 255 || !(p = parser->v[tag]))
		return 0;

	len = *p++;
	if (len > size - 1)
		len = size - 1;
	strncpy(buf, p, len);
	buf[len] = '\0';
	return 1;
}

int
mgr_tlv_get_int(ifd_tlv_parser_t *parser, ifd_tag_t tag,
			unsigned int *value)
{
	unsigned char	*p;
	unsigned int	len;

	*value = 0;
	if (tag > 255 || !(p = parser->v[tag]))
		return 0;

	len = *p++;
	while (len--) {
		*value <<= 8;
		*value |= *p++;
	}

	return 1;
}

int
mgr_tlv_get_opaque(ifd_tlv_parser_t *parser, ifd_tag_t tag,
			unsigned char **data, size_t *lenp)
{
	unsigned char	*p;

	*data = NULL;
	*lenp = 0;

	if (tag > 255 || !(p = parser->v[tag]))
		return 0;
	*lenp = *p++;
	*data = p;
	return 1;
}

void
mgr_tlv_builder_init(ifd_tlv_builder_t *builder, ifd_buf_t *bp)
{
	memset(builder, 0, sizeof(*builder));
	builder->buf = bp;
}

void
mgr_tlv_put_int(ifd_tlv_builder_t *builder, ifd_tag_t tag,
		unsigned int value)
{
	unsigned int	n;

	if (builder->error)
		return;
	mgr_tlv_put_tag(builder, tag);
	for (n = 0; (value >> (n + 8)) != 0; n += 8)
		;
	do {
		mgr_tlv_add_byte(builder, value >> n);
		n -= 8;
	} while (n);

	builder->lenp = NULL;
}

void
mgr_tlv_put_string(ifd_tlv_builder_t *builder, ifd_tag_t tag,
		const char *string)
{
	if (builder->error)
		return;

	mgr_tlv_put_tag(builder, tag);
	mgr_tlv_add_bytes(builder, string, strlen(string));

	builder->lenp = NULL;
}

void
mgr_tlv_put_tag(ifd_tlv_builder_t *builder, ifd_tag_t tag)
{
	ifd_buf_t	*bp = builder->buf;

	if (builder->error < 0)
		return;
	if (ifd_buf_putc(bp, tag) < 0)
		goto err;
	builder->lenp = ifd_buf_head(bp);
	if (ifd_buf_putc(bp, 0) < 0)
		goto err;
	return;

err:	builder->error = -1;
}

void
mgr_tlv_add_byte(ifd_tlv_builder_t *builder, unsigned char byte)
{
	mgr_tlv_add_bytes(builder, &byte, 1);
}

void
mgr_tlv_add_bytes(ifd_tlv_builder_t *builder,
		const unsigned char *data, size_t num)
{
	ifd_buf_t	*bp = builder->buf;

	if (builder->error < 0)
		return;

	if (!builder->lenp
	 || *(builder->lenp) >= 256 - num
	 || ifd_buf_put(bp, data, num)) {
		builder->error = -1;
	} else {
		*(builder->lenp) += num;
	}
}
