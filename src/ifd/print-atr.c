/*
 * Test application - given a device, print the ATR
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ifd/core.h>
#include <ifd/config.h>

static void	usage(int exval);
static void	print_atr(ifd_reader_t *);
static void	select_mf(ifd_reader_t *reader);
static void	dump(unsigned char *data, size_t len);

static unsigned int	opt_reader = 0;
static const char *	opt_config = NULL;

int
main(int argc, char **argv)
{
	ifd_reader_t	*reader;
	int		c;

	while ((c = getopt(argc, argv, "df:r:h")) != -1) {
		switch (c) {
		case 'd':
			ifd_config.debug++;
			break;
		case 'f':
			opt_config = optarg;
			break;
		case 'h':
			usage(0);
		case 'r':
			opt_reader = atoi(optarg);
			break;
		default:
			usage(1);
		}
	}
	if (optind != argc)
		usage(1);

	if (ifd_config_parse(opt_config) < 0)
		exit(1);

	if (!(reader = ifd_reader_by_index(opt_reader))) {
		fprintf(stderr, "Unknown reader #%u\n", opt_reader);
		return 1;
	}

	print_atr(reader);
	return 0;
}

void
usage(int exval)
{
	fprintf(stderr, "usage: print-atr [-d] [-f configfile] [-r reader]\n");
	exit(exval);
}

void
print_atr(ifd_reader_t *reader)
{
	unsigned char	atr[64];
	int		m, n, status;

	printf("Detected %s (%d slot%s%s%s)\n",
		reader->name,
		reader->nslots,
		(reader->nslots == 1)? "" : "s",
		(reader->flags & IFD_READER_KEYPAD)? ", keypad" : "",
		(reader->flags & IFD_READER_DISPLAY)? ", display" : "");

	if (ifd_activate(reader) < 0)
		exit(1);
	if (ifd_card_status(reader, 0, &status) < 0)
		exit(1);
	printf("Card %spresent%s\n",
			(status & IFD_CARD_PRESENT)? "" : "not ",
			(status & IFD_CARD_STATUS_CHANGED)? ", status changed" : "");

	if (status & IFD_CARD_PRESENT) {
		n = ifd_card_reset(reader, 0, atr, sizeof(atr));
		if (n < 0) {
			fprintf(stderr, "failed to get ATR\n");
			exit(1);
		}
		printf("ATR:");
		for (m = 0; m < n; m++)
			printf(" %02x", atr[m]);
		printf("\n");

		select_mf(reader);
	}

	sleep(1);
}

void
select_mf(ifd_reader_t *reader)
{
	unsigned char	cmd[] = { 0x00, 0xA4, 0x00, 0x00, 0x02, 0x3f, 0x00, 0x00 };
	unsigned char	res[256];
	ifd_apdu_t	apdu;

	apdu.snd_buf = cmd;
	apdu.snd_len = sizeof(cmd);
	apdu.rcv_buf = res;
	apdu.rcv_len = sizeof(res);

	if (ifd_card_command(reader, 0, &apdu) < 0) {
		fprintf(stderr, "card communication failure\n");
		return;
	}

	printf("Selected MF, response:\n");
	dump(res, apdu.rcv_len);
}

void
dump(unsigned char *data, size_t len)
{
	unsigned int offset = 0;

	do {
		unsigned int i;

		printf("%04x:", offset);
		for (i = 0; i < 16 && len; i++, len--)
			printf(" %02x", *data++);
		printf("\n");
	} while (len);
}
