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

	if (!(reader = ifd_new_serial(device, opt_driver)))
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
	}

	sleep(1);
}
