/*
 * APDU type definitions
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_APDU_H
#define OPENCT_APDU_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ifd_iso_apdu {
	unsigned char		cse, cla, ins, p1, p2;
	unsigned int		lc, le;
	unsigned int		sw;
	void *			data;
	size_t			len;

	/* xxx go away */
	unsigned char *		rcv_buf;
	unsigned int		rcv_len;
} ifd_iso_apdu_t;

enum {
	IFD_APDU_CASE_1  = 0x00,
	IFD_APDU_CASE_2S = 0x01,
	IFD_APDU_CASE_3S = 0x02,
	IFD_APDU_CASE_4S = 0x03,
	IFD_APDU_CASE_2E = 0x10,
	IFD_APDU_CASE_3E = 0x20,
	IFD_APDU_CASE_4E = 0x30,

	IFD_APDU_BAD = -1
};

#define IFD_APDU_CASE_LC(c)	((c) & 0x02)
#define IFD_APDU_CASE_LE(c)	((c) & 0x01)

extern int	ifd_iso_apdu_parse(const void *, size_t, ifd_iso_apdu_t *);
extern int	ifd_apdu_case(const void *, size_t);

#ifdef __cplusplus
}
#endif

#endif /* OPENCT_APDU_H */
