/*
 * Handle TLV encoded data
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#ifndef IFD_TLV_H
#define IFD_TLV_H

#include <ifd/protocol.h>
#include <ifd/buffer.h>

typedef unsigned char	ifd_tag_t;

typedef struct ifd_tlv_parser {
	unsigned char *	v[256];
} ifd_tlv_parser_t;

typedef struct ifd_tlv_builder {
	int		error;
	ifd_buf_t *	buf;
	unsigned char *	lenp;
} ifd_tlv_builder_t;

extern int	ifd_tlv_parse(ifd_tlv_parser_t *, ifd_buf_t *);
extern int	ifd_tlv_get_int(ifd_tlv_parser_t *,
				ifd_tag_t, unsigned int *);
extern int	ifd_tlv_get_string(ifd_tlv_parser_t *,
				ifd_tag_t, char *, size_t);
extern int	ifd_tlv_get_opaque(ifd_tlv_parser_t *,
				ifd_tag_t, unsigned char **, size_t *);

extern void	ifd_tlv_builder_init(ifd_tlv_builder_t *, ifd_buf_t *);
extern void	ifd_tlv_put_int(ifd_tlv_builder_t *,
				ifd_tag_t, unsigned int);
extern void	ifd_tlv_put_string(ifd_tlv_builder_t *,
				ifd_tag_t, const char *);
extern void	ifd_tlv_put_tag(ifd_tlv_builder_t *, ifd_tag_t);
extern void	ifd_tlv_add_byte(ifd_tlv_builder_t *, unsigned char);
extern void	ifd_tlv_add_bytes(ifd_tlv_builder_t *,
				const unsigned char *, size_t);

#endif /* IFD_TLV_H */
