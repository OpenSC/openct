/*
 * APDU handling
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <string.h>
#include "internal.h"

/*
 * Check the APDU type and length
 */
static int
__ifd_apdu_check(const ifd_apdu_t *apdu, ifd_iso_apdu_t *iso)
{
	unsigned char	*data = (unsigned char *) apdu->snd_buf;
	unsigned int	b, len = apdu->snd_len;

	memset(iso, 0, sizeof(*iso));
	if (len < 5) {
		iso->cse = IFD_APDU_CASE_1;
		return 0;
	}

	b = data[4];
	len -= 5;

	/* APDU + Le */
	if (len == 0) {
		iso->cse = IFD_APDU_CASE_2S;
		iso->le = b? b : 256;
		return 0;
	}

	data += 5;
	if (b == 0)
		b = 256;

	iso->lc = b;
	iso->snd_len = len;
	iso->snd_buf = data;

	/* APDU + Lc + data */
	if (len == b) {
		iso->cse = IFD_APDU_CASE_3S;
		return 0;
	}

	/* APDU + Lc + data + Le */
	if (len == b + 1) {
		iso->cse = IFD_APDU_CASE_4S;
		iso->le = data[b]? data[b] : 256;
		iso->snd_len--;
		return 0;
	}

	return -1;
}

int
ifd_apdu_case(const ifd_apdu_t *apdu, unsigned int *lc, unsigned int *le)
{
	ifd_iso_apdu_t iso;

	if (__ifd_apdu_check(apdu, &iso) < 0)
		return -1;
	if (lc)
		*lc = iso.lc;
	if (le)
		*le = iso.le;
	return iso.cse;
}

/*
 * Convert internal APDU type to an ISO-7816-4 APDU
 */
int
ifd_apdu_to_iso(const ifd_apdu_t *apdu, ifd_iso_apdu_t *iso)
{
	unsigned char	*p;

	if (apdu->snd_len < 4)
		return -1;

	if (__ifd_apdu_check(apdu, iso) < 0)
		return -1;

	p = (unsigned char *) apdu->snd_buf;
	iso->cla = *p++;
	iso->ins = *p++;
	iso->p1  = *p++;
	iso->p2  = *p++;

	if (IFD_APDU_CASE_LE(iso->cse)) {
		iso->rcv_buf = apdu->rcv_buf;
		iso->rcv_len = apdu->rcv_len;
	}

	return 0;
}

/*
 * Convert an ISO-7816-4 APDU to our internal APDU type
 */
int
ifd_iso_to_apdu(const ifd_iso_apdu_t *iso, ifd_apdu_t *apdu, void *buf, size_t size)
{
	unsigned int	slen = 4, rlen = 2;
	unsigned char	*p;

	memset(apdu, 0, sizeof(*apdu));

	if (IFD_APDU_CASE_LC(iso->cse)) {
		slen += iso->lc + 1;
	}
	if (IFD_APDU_CASE_LE(iso->cse)) {
		rlen += iso->le;
		slen++;
	}

	if (slen > size || rlen > size)
		return -1;

	apdu->snd_buf = apdu->rcv_buf = buf;
	apdu->snd_len = slen;
	apdu->rcv_len = size;

	p = (unsigned char *) buf;
	*p++ = iso->cla;
	*p++ = iso->ins;
	*p++ = iso->p1;
	*p++ = iso->p2;
	if (IFD_APDU_CASE_LC(iso->cse)) {
		*p++ = iso->lc;
		memcpy(p, iso->snd_buf, iso->lc);
		p += iso->lc;
	}
	if (IFD_APDU_CASE_LE(iso->cse)) {
		*p++ = iso->le;
	}

	return 0;
}

