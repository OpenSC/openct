/*
 * TLV handling routines
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ifd/tlv.h>

/*
 * Parse TLV data
 */
int
ifd_tlv_parse(ifd_tlv_parser_t *parser, ifd_buf_t *bp)
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

/*
 * Extract TLV encoded items as strings, integers, etc.
 */
int
ifd_tlv_get_string(ifd_tlv_parser_t *parser, ifd_tag_t tag,
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
ifd_tlv_get_int(ifd_tlv_parser_t *parser, ifd_tag_t tag,
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
ifd_tlv_get_opaque(ifd_tlv_parser_t *parser, ifd_tag_t tag,
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

int
ifd_tlv_get_bytes(ifd_tlv_parser_t *parser, ifd_tag_t tag,
			void *buf, size_t size)
{
	unsigned char	*p;
	unsigned int	len;

	if (tag > 255 || !(p = parser->v[tag]))
		return 0;
	if ((len = *p++) > size)
		len = size;
	memcpy(buf, p, len);
	return len;
}

/*
 * Initialize a TLV data builder
 */
void
ifd_tlv_builder_init(ifd_tlv_builder_t *builder, ifd_buf_t *bp)
{
	memset(builder, 0, sizeof(*builder));
	builder->buf = bp;
}

/*
 * TLV encode objects
 */
void
ifd_tlv_put_int(ifd_tlv_builder_t *builder, ifd_tag_t tag,
		unsigned int value)
{
	int	n;

	if (builder->error)
		return;
	ifd_tlv_put_tag(builder, tag);
	for (n = 0; (value >> (n + 8)) != 0; n += 8)
		;
	do {
		ifd_tlv_add_byte(builder, value >> n);
		n -= 8;
	} while (n >= 0);

	builder->lenp = NULL;
}

void
ifd_tlv_put_string(ifd_tlv_builder_t *builder, ifd_tag_t tag,
		const char *string)
{
	if (builder->error)
		return;

	ifd_tlv_put_tag(builder, tag);
	ifd_tlv_add_bytes(builder, string, strlen(string));

	builder->lenp = NULL;
}

void
ifd_tlv_put_tag(ifd_tlv_builder_t *builder, ifd_tag_t tag)
{
	ifd_buf_t	*bp = builder->buf;

	if (builder->error < 0)
		return;
	if (ifd_buf_putc(bp, tag) < 0)
		goto err;
	builder->lenp = ifd_buf_tail(bp);
	if (ifd_buf_putc(bp, 0) < 0)
		goto err;
	return;

err:	builder->error = -1;
}

void
ifd_tlv_add_byte(ifd_tlv_builder_t *builder, unsigned char byte)
{
	ifd_tlv_add_bytes(builder, &byte, 1);
}

void
ifd_tlv_add_bytes(ifd_tlv_builder_t *builder,
			const unsigned char *data, size_t num)
{
	ifd_buf_t	*bp = builder->buf;

	if (builder->error < 0)
		return;

	if (!builder->lenp
	 || *(builder->lenp) + num > 255
	 || ifd_buf_put(bp, data, num) < 0) {
		builder->error = -1;
	} else {
		*(builder->lenp) += num;
	}
}
