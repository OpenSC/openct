/*
 * openct-util
 *
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openct/conf.h>
#include <openct/openct.h>

static void	usage(int exval);
static void	print_reader(ct_handle *h);
static void	print_atr(ct_handle *);
static void	select_mf(ct_handle *reader);
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

#if 0
	/* Parse IFD config file */
	if (ct_config_parse(opt_config) < 0)
		exit(1);
#endif

	if (opt_debug > ct_config.debug)
		ct_config.debug = opt_debug;

	if (opt_command == CMD_LIST) {
		int	i = 0, num = IFD_MAX_READER;

		printf("Available reader positions: %d\n", num);
		for (i = 0; i < num; i++) {
			if (!(h = ct_reader_connect(i)))
				continue;
			printf(" %2d ", i);
			print_reader(h);
			// ct_reader_disconnect(h);
		}

		printf("Try option \"-h\" for help\n");
		exit(0);
	}

	if (!(h = ct_reader_connect(opt_reader))) {
		fprintf(stderr, "Unknown reader #%u\n", opt_reader);
		return 1;
	}

	print_atr(h);
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
print_reader(ct_handle *h)
{
	ct_info_t	info;
	const char	*sepa;
	int		rc;

	if ((rc = ct_reader_status(h, &info)) < 0) {
		printf("ct_reader_status: err=%d\n", rc);
		return;
	}
	printf("%s", info.ct_name);

	sepa = " (";
	if (info.ct_slots != 1) {
		printf("%s%d slots", sepa, info.ct_slots);
		sepa = ", ";
	}
	if (info.ct_display) {
		printf("%sdisplay", sepa);
		sepa = ", ";
	}
	if (info.ct_keypad) {
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

	rc = ct_card_transact(h, 0, cmd, sizeof(cmd), res, sizeof(res));
	if (rc < 0) {
		fprintf(stderr, "card communication failure, err=%d\n", rc);
		return;
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
