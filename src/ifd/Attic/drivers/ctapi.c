/*
 * Generic include file to provide a CTAPI shim for
 * a terminal driver
 *
 * Copyright (C) 2993 Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <ifd/ctapi.h>
#include "../internal.h"

static ifd_reader_t	*my_reader;

char
CT_init(unsigned short ctn, unsigned short pn)
{
	const char	*name;

	if (ctn != 0)
		return ERR_INVALID;
	if (my_reader) {
		ifd_close(my_reader);
		my_reader = NULL;
	}

	if (!(name = ifd_device_cannel_to_name(pn)))
		return ERR_INVALID;
	
	if (!(my_reader = ifd_open(DRIVER_NAME, name)))
		return ERR_INVALID;

	return OK;
}

char
CT_close(unsigned short ctn)
{
	if (ctn != 0)
		return ERR_INVALID;
	if (my_reader) {
		ifd_close(my_reader);
		my_reader = NULL;
	}
	return OK;
}

char
CT_data(unsigned short ctn,
	unsigned char  *dad,
	unsigned char  *sad,
	unsigned short lc,
	unsigned char  *cmd,
	unsigned short *lr,
	unsigned char  *rsp)
{
	ifd_apdu_t	apdu;
	int		rc;

	if (ctn != 0 || !my_reader || !sad || !dad)
		return ERR_INVALID;

	apdu.snd_len = lc;
	apdu.snd_buf = cmd;
	apdu.rcv_len = *lr;
	apdu.rcv_buf = rsp;

	switch (*dad) {
	case 0:
		rc = ifd_card_command(my_reader, 0, &apdu);
		break;
	case 1:
		rc = ifd_reader_ctbcs(my_reader, &apdu);
		break;
	case 2:
		ifd_error("CT-API: host talking to itself - "
			  "needs professional help?");
		return ERR_INVALID;
	case 3:
		rc = ifd_card_command(my_reader, 1, &apdu);
		break;
	default:
		ifd_error("CT-API: unknown DAD %u", *dad);
		return ERR_INVALID;
	}

	/* Somewhat simplistic error translation */
	if (rc < 0)
		return ERR_INVALID;

	*lr = rc;
	return OK;
}
