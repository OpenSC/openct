/*
 * APDU handling
 *
 */

#include "internal.h"

static int
__ifd_apdu_case(const ifd_apdu_t *apdu, unsigned int *lc, unsigned int *le)
{
	unsigned char	*data = (unsigned char *) apdu->snd_buf;
	unsigned int	b, len = apdu->snd_len;

	*lc = *le = 0;

	if (len < 5)
		return IFD_APDU_CASE_1;

	b = data[4];
	len -= 5;

	/* APDU + Le */
	if (len == 0) {
		*le = b;
		return IFD_APDU_CASE_2S;
	}

	if (b == 0)
		b = 256;

	*lc = b;

	/* APDU + Lc + data */
	if (len == b)
		return IFD_APDU_CASE_3S;

	/* APDU + Lc + data + Le */
	if (len == b + 1) {
		*le = data[1 + b];
		return IFD_APDU_CASE_4S;
	}

	return IFD_APDU_BAD;
}

int
ifd_apdu_case(const ifd_apdu_t *apdu, unsigned int *lc, unsigned int *le)
{
	unsigned int dummy;

	return __ifd_apdu_case(apdu, lc? lc : &dummy, le? le : &dummy);
}
