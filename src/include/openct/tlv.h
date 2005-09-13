/*
 * Handle TLV encoded data
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_TLV_H
#define OPENCT_TLV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <openct/protocol.h>
#include <openct/buffer.h>

typedef unsigned char	ifd_tag_t;

typedef struct ct_tlv_parser {
	unsigned char	use_large_tags;
	unsigned char *	val[256];
	unsigned int	len[256];
} ct_tlv_parser_t;

typedef struct ct_tlv_builder {
	int		error;
	unsigned char	use_large_tags;
	ct_buf_t *	buf;
	unsigned int	len;
	unsigned char *	lenp;
} ct_tlv_builder_t;

extern int	ct_tlv_parse(ct_tlv_parser_t *, ct_buf_t *);
/* ct_tlv_get return 0 == not there, 1 == there */
extern int	ct_tlv_get_int(ct_tlv_parser_t *,
				ifd_tag_t, unsigned int *);
extern int	ct_tlv_get_string(ct_tlv_parser_t *,
				ifd_tag_t, char *, size_t);
extern int	ct_tlv_get_opaque(ct_tlv_parser_t *,
				ifd_tag_t, unsigned char **, size_t *);
/* or number of bytes */
extern int	ct_tlv_get_bytes(ct_tlv_parser_t *,
				ifd_tag_t, void *, size_t);

extern void	ct_tlv_builder_init(ct_tlv_builder_t *, ct_buf_t *, int);
extern void	ct_tlv_put_int(ct_tlv_builder_t *,
				ifd_tag_t, unsigned int);
extern void	ct_tlv_put_string(ct_tlv_builder_t *,
				ifd_tag_t, const char *);
extern void	ct_tlv_put_opaque(ct_tlv_builder_t *, ifd_tag_t,
				const unsigned char *, size_t);
extern void	ct_tlv_put_tag(ct_tlv_builder_t *, ifd_tag_t);
extern void	ct_tlv_add_byte(ct_tlv_builder_t *, unsigned char);
extern void	ct_tlv_add_bytes(ct_tlv_builder_t *,
				const unsigned char *, size_t);

#ifdef __cplusplus
}
#endif

#endif /* OPENCT_TLV_H */
