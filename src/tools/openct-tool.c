/*
 * openct-tool
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openct/openct.h>
#include <openct/logging.h>

static void	usage(int exval);
static void	print_reader(ct_handle *h);
static void	print_reader_info(ct_info_t *info);
static void	print_atr(ct_handle *);
static void	select_mf(ct_handle *reader);
static void	dump(unsigned char *data, size_t len);


static unsigned int	opt_reader = 0;
static unsigned int	opt_slot = 0;
static const char *	opt_config = NULL;
static int		opt_debug = 0;
static int		opt_command = -1;

enum {
	CMD_LIST = 0,
	CMD_WAIT,
	CMD_ATR,
	CMD_MF,
};

int
main(int argc, char **argv)
{
	const char	*cmd;
	ct_handle	*h;
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

	if (optind == argc)
		usage(1);

	cmd = argv[optind];

	if (!strcmp(cmd, "list"))
		opt_command = CMD_LIST;
	else if (!strcmp(cmd, "atr"))
		opt_command = CMD_ATR;
	else if (!strcmp(cmd, "wait"))
		opt_command = CMD_WAIT;
	else if (!strcmp(cmd, "mf"))
		opt_command = CMD_MF;
	else {
		fprintf(stderr,
			"Unknown command \"%s\"\n", cmd);
		usage(1);
	}

	if (opt_command == CMD_LIST) {
		int	i;

		for (i = 0; i < OPENCT_MAX_READERS; i++) {
			ct_info_t	info;

			if (ct_reader_info(i, &info) < 0)
				continue;
			printf(" %2d ", i);
			print_reader_info(&info);
		}

		exit(0);
	}

	if (!(h = ct_reader_connect(opt_reader))) {
		fprintf(stderr, "Unknown reader #%u\n", opt_reader);
		return 1;
	}

	if (opt_command == CMD_WAIT) {
		int	status, rc;

		while (1) {
			if ((rc = ct_card_status(h, opt_slot, &status)) < 0) {
				fprintf(stderr,
					"failed to get card status: %s\n",
					ct_strerror(rc));
				return 1;
			}
			if (status)
				break;
			sleep(1);
		}
		printf("Card detected\n");
	} else {
		print_atr(h);
	}

	return 0;
}

void
usage(int exval)
{
	fprintf(stderr,
"usage: openct-tool [-d] [-f configfile] [-r reader] command ...\n"
"  -d   enable debugging; repeat to increase verbosity\n"
"  -f   specify config file (default /etc/ifd.conf\n"
"  -r   specify index of reader to use\n"
"  -h   display this message\n"
"\n"
"command: can be one of the following\n"
" list  list all readers found\n"
" atr   print ATR of card in selected reader\n"
" wait  wait for card to be inserted\n"
" mf    try to select main folder of card\n"
);
	exit(exval);
}

void
print_reader(ct_handle *h)
{
	ct_info_t	info;
	int		rc;

	if ((rc = ct_reader_status(h, &info)) < 0) {
		printf("ct_reader_status: err=%d\n", rc);
	} else {
		print_reader_info(&info);
	}
}

void
print_reader_info(ct_info_t *info)
{
	const char	*sepa;

	printf("%s", info->ct_name);

	sepa = " (";
	if (info->ct_slots != 1) {
		printf("%s%d slots", sepa, info->ct_slots);
		sepa = ", ";
	}
	if (info->ct_display) {
		printf("%sdisplay", sepa);
		sepa = ", ";
	}
	if (info->ct_keypad) {
		printf("%skeypad", sepa);
		sepa = ", ";
	}
	if (sepa[0] != ' ')
		printf(")");
	printf("\n");
}

void
print_atr(ct_handle *h)
{
	unsigned char	atr[64];
	int		rc, m, n, status;
	ct_lock_handle	lock;

	printf("Detected ");
	print_reader(h);

	if ((rc = ct_card_lock(h, 0, IFD_LOCK_SHARED, &lock)) < 0) {
		fprintf(stderr, "ct_card_lock: err=%d\n", rc);
		exit(1);
	}

	if ((rc = ct_card_status(h, 0, &status)) < 0) {
		fprintf(stderr, "ct_card_status: err=%d\n", rc);
		exit(1);
	}

	printf("Card %spresent%s\n",
			(status & IFD_CARD_PRESENT)? "" : "not ",
			(status & IFD_CARD_STATUS_CHANGED)? ", status changed" : "");

	if (status & IFD_CARD_PRESENT) {
		n = ct_card_reset(h, 0, atr, sizeof(atr));
	} else {
		n = ct_card_request(h, 0, 5, "Please insert card",
				atr, sizeof(atr));
	}

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
		select_mf(h);
	}

	ct_card_unlock(h, 0, lock);
	sleep(1);
}

void
select_mf(ct_handle *h)
{
	unsigned char	cmd[] = { 0x00, 0xA4, 0x00, 0x00, 0x02, 0x3f, 0x00, 0x00 };
	unsigned char	res[256];
	ct_lock_handle	lock;
	int		rc;

	if ((rc = ct_card_lock(h, 0, IFD_LOCK_EXCLUSIVE, &lock)) < 0) {
		fprintf(stderr, "ct_card_lock: err=%d\n", rc);
		exit(1);
	}

again:
	rc = ct_card_transact(h, 0, cmd, sizeof(cmd), res, sizeof(res));
	if (rc < 0) {
		fprintf(stderr, "card communication failure, err=%d\n", rc);
		return;
	}

	if (rc == 2 && res[0] == 0x6A && res[1] == 0x86) {
		/* FIXME - Cryptoflex needs class byte 0xC0 :-( */
		if (cmd[0] == 0x00) {
			cmd[0] = 0xC0;
			goto again;
		}
	}

	printf("Selected MF, response:\n");
	dump(res, rc);

	ct_card_unlock(h, 0, lock);
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
