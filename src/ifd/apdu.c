/*
 * APDU handling
 *
 */

#include "internal.h"

/*
 * Check the APDU type and length
 */
static int
__ifd_apdu_check(const ifd_apdu_t *apdu, ifd_iso_apdu_t *iso)
{
	unsigned char	*data = (unsigned char *) apdu->snd_buf;
	unsigned int	b, len = apdu->snd_len;

	if (len < 5) {
		iso->cse = IFD_APDU_CASE_1;
		return 0;
	}

	b = data[4];
	len -= 5;

	/* APDU + Le */
	if (len == 0) {
		iso->cse = IFD_APDU_CASE_2S;
		iso->le = b;
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
		iso->le = data[b];
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

int
ifd_apdu_to_iso(const ifd_apdu_t *apdu, ifd_iso_apdu_t *iso)
{
	unsigned char	*p;

	memset(iso, 0, sizeof(*iso));
	if (apdu->snd_len < 4)
		return -1;

	if (__ifd_apdu_case(apdu, &iso) < 0)
		return -1;

	p = (unsigned char *) apdu->snd_buf;
	iso->cla = *p++;
	iso->ins = *p++;
	iso->p1  = *p++;
	iso->p2  = *p++;

	return 0;
}

int
ifd_iso_to_apdu(const ifd_iso_apdu_t *iso, ifd_apdu_t *apdu, void *buf, size_t size)
{
}

