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

#include <openct/tlv.h>

/*
 * Parse TLV data
 */
int
ct_tlv_parse(ct_tlv_parser_t *parser, ct_buf_t *bp)
{
	unsigned int	avail;
	unsigned char	*p, tag, len;

	while ((avail = ct_buf_avail(bp)) != 0) {
		if (avail < 2)
			return -1;

		p = ct_buf_head(bp);
		tag = p[0];
		len = p[1];

		if (len == 0 || 2 + len > avail)
			return -1;

		parser->v[tag] = p + 1;

		ct_buf_get(bp, NULL, 2 + len);
	}

	return 0;
}

/*
 * Extract TLV encoded items as strings, integers, etc.
 */
int
ct_tlv_get_string(ct_tlv_parser_t *parser, ifd_tag_t tag,
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
ct_tlv_get_int(ct_tlv_parser_t *parser, ifd_tag_t tag,
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
ct_tlv_get_opaque(ct_tlv_parser_t *parser, ifd_tag_t tag,
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
ct_tlv_get_bytes(ct_tlv_parser_t *parser, ifd_tag_t tag,
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
ct_tlv_builder_init(ct_tlv_builder_t *builder, ct_buf_t *bp)
{
	memset(builder, 0, sizeof(*builder));
	builder->buf = bp;
}

/*
 * TLV encode objects
 */
void
ct_tlv_put_int(ct_tlv_builder_t *builder, ifd_tag_t tag,
		unsigned int value)
{
	int	n;

	if (builder->error)
		return;
	ct_tlv_put_tag(builder, tag);
	for (n = 0; (value >> (n + 8)) != 0; n += 8)
		;
	do {
		ct_tlv_add_byte(builder, value >> n);
		n -= 8;
	} while (n >= 0);

	builder->lenp = NULL;
}

void
ct_tlv_put_string(ct_tlv_builder_t *builder, ifd_tag_t tag,
		const char *string)
{
	if (builder->error)
		return;

	ct_tlv_put_tag(builder, tag);
	ct_tlv_add_bytes(builder, string, strlen(string));

	builder->lenp = NULL;
}

void
ct_tlv_put_tag(ct_tlv_builder_t *builder, ifd_tag_t tag)
{
	ct_buf_t	*bp = builder->buf;

	if (builder->error < 0)
		return;
	if (ct_buf_putc(bp, tag) < 0)
		goto err;
	builder->lenp = ct_buf_tail(bp);
	if (ct_buf_putc(bp, 0) < 0)
		goto err;
	return;

err:	builder->error = -1;
}

void
ct_tlv_add_byte(ct_tlv_builder_t *builder, unsigned char byte)
{
	ct_tlv_add_bytes(builder, &byte, 1);
}

void
ct_tlv_add_bytes(ct_tlv_builder_t *builder,
			const unsigned char *data, size_t num)
{
	ct_buf_t	*bp = builder->buf;

	if (builder->error < 0)
		return;

	if (!builder->lenp
	 || *(builder->lenp) + num > 255
	 || ct_buf_put(bp, data, num) < 0) {
		builder->error = -1;
	} else {
		*(builder->lenp) += num;
	}
}
