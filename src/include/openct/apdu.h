/*
 * APDU type definitions
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#ifndef IFD_APDU_H
#define IFD_APDU_H

typedef struct ifd_apdu {
	unsigned char *		snd_buf;
	unsigned int		snd_len;
	unsigned char *		rcv_buf;
	unsigned int		rcv_len;
} ifd_apdu_t;

typedef struct ifd_iso_apdu {
	unsigned char		cse, cla, ins, p1, p2;
	unsigned int		lc, le;
	unsigned int		sw;
	void *			snd_buf;
	void *			rcv_buf;
	size_t			snd_len, rcv_len;
} ifd_iso_apdu_t;

enum {
	IFD_APDU_CASE_1  = 0x00,
	IFD_APDU_CASE_2S = 0x01,
	IFD_APDU_CASE_3S = 0x02,
	IFD_APDU_CASE_4S = 0x03,
	IFD_APDU_CASE_2E = 0x10,
	IFD_APDU_CASE_3E = 0x20,
	IFD_APDU_CASE_4E = 0x30,

	IFD_APDU_BAD = -1,
};
#define IFD_APDU_CASE_LC(c)	((c) & 0x02)
#define IFD_APDU_CASE_LE(c)	((c) & 0x01)

extern int	ifd_apdu_case(const ifd_apdu_t *, unsigned int *, unsigned int *);
extern int	ifd_apdu_to_iso(const ifd_apdu_t *, ifd_iso_apdu_t *);
extern int	ifd_iso_to_apdu(const ifd_iso_apdu_t *, ifd_apdu_t *, void *, size_t);

#endif /* IFD_APDU_H */
