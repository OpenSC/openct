/*
 * Test application - given a device, print the ATR
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <getopt.h>
#include <stdio.h>
#include <unistd.h>
#include <ifd/core.h>
#include <ifd/config.h>

static void	usage(int exval);
static void	print_atr(const char *);
static void	select_mf(ifd_reader_t *reader);
static void	dump(unsigned char *data, size_t len);

static const char *	opt_driver = "auto";

int
main(int argc, char **argv)
{
	int	c;

	while ((c = getopt(argc, argv, "dD:h")) != -1) {
		switch (c) {
		case 'd':
			ifd_config.debug++;
			break;
		case 'D':
			opt_driver = optarg;
			break;
		case 'h':
			usage(0);
		default:
			usage(1);
		}
	}
	if (optind != argc - 1)
		usage(1);

	print_atr(argv[optind]);
	return 0;
}

void
usage(int exval)
{
	fprintf(stderr, "usage: print-atr [-h] device\n");
	exit(exval);
}

void
print_atr(const char *device)
{
	ifd_reader_t	*reader;
	unsigned char	atr[64];
	int		m, n, status;

	if (!(reader = ifd_open(opt_driver, device)))
		exit(1);

	printf("Detected %s\n", reader->name);

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

	if (ifd_icc_command(reader, 0, &apdu) < 0) {
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
