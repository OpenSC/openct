/*
 * ATR parsing functions
 *
 * Copyright (C) 2004, Olaf Kirch <okir@suse.de>
 */

#include <string.h>
#include "internal.h"
#include "atr.h"

int
ifd_atr_parse(ifd_atr_info_t *info, const unsigned char *atr, size_t len)
{
	unsigned int	m, n, k;

	ifd_debug(1, "atr=%s", ct_hexdump(atr, len));

	/* Initialize the atr_info struct */
	memset(info, 0, sizeof(*info));
	info->default_protocol = -1;
	for (n = 0; n < 2; n++) {
		info->TA[n] = -1;
		info->TB[n] = -1;
		info->TC[n] = -1;
	}

	if (len <= 2 + (atr[1] & 0x0f))
		return IFD_ERROR_INVALID_ATR;

	/* Ignore hysterical bytes */
	len -= atr[1] & 0x0f;

	for (m = 0, n = 2; n < len; m++) {
		unsigned int	TDi;

		/* There is only TA1, TA2, TA3, no TA4 */
		if (m > 2)
			return IFD_ERROR_INVALID_ATR;

		TDi = atr[n-1];
		if (n != 2) {
			int	prot;
			
			prot = TDi & 0x0f;
			if (info->default_protocol < 0)
				info->default_protocol = prot;
			info->supported_protocols |= (1 << prot);
		}

		k = ifd_count_bits(TDi & 0xF0);
		if (k == 0 || n + k > len)
			return IFD_ERROR_INVALID_ATR;
		if (TDi & 0x10)
			info->TA[m] = atr[n++];
		if (TDi & 0x20)
			info->TB[m] = atr[n++];
		if (TDi & 0x40)
			info->TC[m] = atr[n++];
		if (!(TDi & 0x80)) {
			if (n < len)
				return IFD_ERROR_INVALID_ATR;
			break;
		}
		n++;
	}

	/* ATR didn't list any supported protocols, so
	 * we default to T=0 */
	if (info->supported_protocols == 0) {
		info->supported_protocols = 0x01;
		info->default_protocol = IFD_PROTOCOL_T0;
	}

	ifd_debug(1, "supported protocols=0x%x, default protocol=%d",
			info->supported_protocols,
			info->default_protocol);
	return 0;
}

/*
 * Given the ATR info and a selected protocol, build the PTS
 * string.
 */
int
ifd_build_pts(const ifd_atr_info_t *info,
		int protocol, unsigned char *buf, size_t len)
{
	unsigned char	ptsbuf[7], pck;
	size_t		n, ptslen = 0;

	/* IFD_PROTOCOL_Tn is just n, so we take it easy here */
	if (!(info->supported_protocols & (1 << protocol))) {
		ct_error("Protocol not supported by card (according to ATR)");
		return IFD_ERROR_NOT_SUPPORTED;
	}

	ptsbuf[ptslen++] = 0xFF;
	ptsbuf[ptslen++] = protocol;
	if (info->TA[0] != -1) {
		ptsbuf[ptslen++] = info->TA[0];
		ptsbuf[1] |= 0x10;
	}
	if (info->TC[0] != -1) {
		ptsbuf[ptslen++] = info->TC[0];
		ptsbuf[1] |= 0x20;
	}

	for (n = 0, pck = 0; n < ptslen; n++)
		pck ^= ptsbuf[n];
	ptsbuf[ptslen++] = pck;

	if (ptslen > len)
		return IFD_ERROR_BUFFER_TOO_SMALL;

	memcpy(buf, ptsbuf, ptslen);
	return ptslen;
}
