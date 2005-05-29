/*
 * TLV handling routines
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <openct/tlv.h>

/*
 * Parse TLV data
 */
int ct_tlv_parse(ct_tlv_parser_t * parser, ct_buf_t * bp)
{
	unsigned int avail, len;
	unsigned char *p, tag;

	/* Code below relies on it */
	assert(((ifd_tag_t) - 1) == 255);

	while ((avail = ct_buf_avail(bp)) != 0) {
		unsigned int header = 2;

		if (avail < 2)
			return -1;

		p = (unsigned char *)ct_buf_head(bp);
		tag = p[0];
		len = p[1];

		if (tag & __CT_TAG_LARGE) {
			parser->use_large_tags = 1;
			tag &= ~__CT_TAG_LARGE;
			if (avail < 3)
				return -1;
			len = (len << 8) | p[header++];
		}

		if (len == 0 || header + len > avail)
			return -1;

		parser->val[tag] = p + header;
		parser->len[tag] = len;

		ct_buf_get(bp, NULL, header + len);
	}

	return 0;
}

/*
 * Extract TLV encoded items as strings, integers, etc.
 */
int ct_tlv_get_string(ct_tlv_parser_t * parser, ifd_tag_t tag, char *buf,
		      size_t size)
{
	unsigned char *p;
	unsigned int len;

	if (!(p = parser->val[tag]))
		return 0;

	len = parser->len[tag];
	if (len > size - 1)
		len = size - 1;
	strncpy(buf, (const char *)p, len);
	buf[len] = '\0';
	return 1;
}

int ct_tlv_get_int(ct_tlv_parser_t * parser, ifd_tag_t tag, unsigned int *value)
{
	unsigned char *p;
	unsigned int len;

	*value = 0;
	if (!(p = parser->val[tag]))
		return 0;

	len = parser->len[tag];
	while (len--) {
		*value <<= 8;
		*value |= *p++;
	}

	return 1;
}

int ct_tlv_get_opaque(ct_tlv_parser_t * parser, ifd_tag_t tag,
		      unsigned char **data, size_t * lenp)
{
	unsigned char *p;

	*data = NULL;
	*lenp = 0;

	if (!(p = parser->val[tag]))
		return 0;
	*lenp = parser->len[tag];
	*data = p;
	return 1;
}

int ct_tlv_get_bytes(ct_tlv_parser_t * parser, ifd_tag_t tag, void *buf,
		     size_t size)
{
	unsigned char *p;
	unsigned int len;

	if (!(p = parser->val[tag]))
		return 0;
	len = parser->len[tag];
	if (len > size)
		len = size;
	memcpy(buf, p, len);
	return len;
}

/*
 * Initialize a TLV data builder
 */
void ct_tlv_builder_init(ct_tlv_builder_t * builder, ct_buf_t * bp,
			 int large_tags)
{
	memset(builder, 0, sizeof(*builder));
	builder->use_large_tags = large_tags;
	builder->buf = bp;
}

/*
 * TLV encode objects
 */
void ct_tlv_put_int(ct_tlv_builder_t * builder, ifd_tag_t tag,
		    unsigned int value)
{
	int n;

	if (builder->error)
		return;
	ct_tlv_put_tag(builder, tag);
	for (n = 0; (value >> (n + 8)) != 0; n += 8) ;
	do {
		ct_tlv_add_byte(builder, value >> n);
		n -= 8;
	} while (n >= 0);

	builder->lenp = NULL;
}

void ct_tlv_put_string(ct_tlv_builder_t * builder, ifd_tag_t tag,
		       const char *string)
{
	if (builder->error)
		return;

	ct_tlv_put_tag(builder, tag);
	ct_tlv_add_bytes(builder, (const unsigned char *)string,
			 strlen(string));

	builder->lenp = NULL;
}

void ct_tlv_put_opaque(ct_tlv_builder_t * builder, ifd_tag_t tag,
		       const unsigned char *data, size_t len)
{
	if (builder->error)
		return;

	ct_tlv_put_tag(builder, tag);
	ct_tlv_add_bytes(builder, data, len);

	builder->lenp = NULL;
}

void ct_tlv_put_tag(ct_tlv_builder_t * builder, ifd_tag_t tag)
{
	ct_buf_t *bp = builder->buf;

	if (builder->error < 0)
		return;
	if (builder->use_large_tags)
		tag |= __CT_TAG_LARGE;
	if (ct_buf_putc(bp, tag) < 0)
		goto err;
	builder->len = 0;
	builder->lenp = (unsigned char *)ct_buf_tail(bp);
	if (ct_buf_putc(bp, 0) < 0
	    || (builder->use_large_tags && ct_buf_putc(bp, 0) < 0))
		goto err;
	return;

      err:builder->error = -1;
}

void ct_tlv_add_byte(ct_tlv_builder_t * builder, unsigned char byte)
{
	ct_tlv_add_bytes(builder, &byte, 1);
}

void ct_tlv_add_bytes(ct_tlv_builder_t * builder, const unsigned char *data,
		      size_t num)
{
	ct_buf_t *bp = builder->buf;

	if (builder->error < 0)
		return;

	if (!builder->lenp)
		goto error;

	builder->len += num;
	if (ct_buf_put(bp, data, num) < 0)
		goto error;

	if (builder->use_large_tags) {
		if (builder->len > 65535)
			goto error;
		builder->lenp[0] = builder->len >> 8;
		builder->lenp[1] = builder->len;
	} else {
		if (builder->len > 266)
			goto error;
		builder->lenp[0] = builder->len;
	}
	return;

      error:
	builder->error = -1;
	return;
}
