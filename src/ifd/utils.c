/*
 * Utility functions
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <stdio.h>


unsigned int
ifd_count_bits(unsigned int word)
{
	static unsigned int bcount[16] = {
		0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4
	};
	unsigned int num;

	for (num = 0; word; word >>= 4)
		num += bcount[word & 0xF];
	return num;
}

void
ifd_revert_bits(unsigned char *data, size_t len)
{
	unsigned char j, k, c, d;

	while (len--) {
		c = *data;
		for (j = 1, k = 0x80, d = 0; k != 0; j <<= 1, k >>= 1) {
			if (c & j)
				d |= k;
		}
		*data++ = d ^ 0xFF;
	}
}

const char *
ct_hexdump(const unsigned char *data, size_t len)
{
	static char	string[1024];
	unsigned int	i, left;

	string[0] = '\0';
	left = sizeof(string);
	for (i = 0; len--; i += 3) {
		if (i >= sizeof(string) - 4)
			break;
		snprintf(string + i, 4, " %02x", *data++);
	}
	return string;
}

long
ifd_time_elapsed(struct timeval *then)
{
	struct timeval	now, delta;

	gettimeofday(&now, NULL);
	timersub(&now, then, &delta);
	return delta.tv_sec * 1000 + (delta.tv_usec % 1000);
}
