/*
 * Test application - given a device, print the ATR
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openct/core.h>
#include <openct/conf.h>

static void	usage(int exval);
static void	print_atr(ifd_reader_t *);
static void	select_mf(ifd_reader_t *reader);
static void	dump(unsigned char *data, size_t len);

static unsigned int	opt_reader = 0;
static const char *	opt_config = NULL;
static int		opt_debug = 0;
static int		opt_command = -1;

enum {
	CMD_ATR = 0,
	CMD_MF,
	CMD_LIST,
};

int
main(int argc, char **argv)
{
	ifd_reader_t	*reader;
	int		c;

	while ((c = getopt(argc, argv, "df:r:h")) != -1) {
		switch (c) {
		case 'd':
			opt_debug++;
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

	if (optind + 1 == argc) {
		const char	*cmd = argv[optind];

		if (!strcmp(cmd, "list"))
			opt_command = CMD_LIST;
		else
		if (!strcmp(cmd, "atr"))
			opt_command = CMD_ATR;
		else
		if (!strcmp(cmd, "mf"))
			opt_command = CMD_MF;
		else {
			fprintf(stderr,
				"Unknown command \"%s\"\n", cmd);
			usage(1);
		}
	} else {
		opt_command = CMD_LIST;
		if (optind != argc)
			usage(1);
	}

	/* Initialize IFD library */
	ifd_init();

	/* Parse IFD config file */
	if (ifd_config_parse(opt_config) < 0)
		exit(1);

	if (opt_debug > ifd_config.debug)
		ifd_config.debug = opt_debug;

	ifd_hotplug_init();

	if (opt_command == CMD_LIST) {
		int	i = 0, num = ifd_reader_count();

		printf("Available reader positions: %d\n", num);
		for (i = 0; i < num; i++) {
			ifd_reader_t	*reader;

			if (!(reader = ifd_reader_by_index(i)))
				continue;
			printf(" %2d %s\n", i, reader->name);
		}

		printf("Try option \"-h\" for help\n");
		exit(0);
	}

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
	fprintf(stderr,
"usage: print-atr [-d] [-f configfile] [-r reader] [command]\n"
"  -d   enable debugging; repeat to increase verbosity\n"
"  -f   specify config file (default /etc/ifd.conf\n"
"  -r   specify index of reader to use\n"
"  -h   display this message\n"
"\n"
"command: can be one of the following\n"
" list  list all readers found\n"
" atr   print ATR of card in selected reader\n"
" mf    try to select ATR of card\n"
);
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
		switch (opt_command) {
		case CMD_ATR:
			printf("ATR:");
			for (m = 0; m < n; m++)
				printf(" %02x", atr[m]);
			printf("\n");
			break;
		case CMD_MF:
			select_mf(reader);
		}
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
