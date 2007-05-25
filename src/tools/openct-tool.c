/*
 * openct-tool
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <openct/openct.h>
#include <openct/logging.h>
#include <openct/error.h>

static void usage(int exval);
static void version(void);
static int do_reset(ct_handle *, unsigned char *, size_t);
static void do_select_mf(ct_handle * reader);
static void do_read_memory(ct_handle *, unsigned int, unsigned int);
static void print_reader(ct_handle * h);
static void print_reader_info(ct_info_t * info);
static void print_atr(ct_handle *, unsigned char *, size_t);
static void dump(unsigned char *data, size_t len);

static unsigned int opt_reader = 0;
static unsigned int opt_slot = 0;
static const char *opt_config = NULL;
static int opt_debug = 0;
static int opt_command = -1;

enum {
	CMD_LIST = 0,
	CMD_WAIT,
	CMD_RWAIT,
	CMD_ATR,
	CMD_MF,
	CMD_READ,
	CMD_VERSION
};

int main(int argc, char **argv)
{
	unsigned char atr[64];
	const char *cmd;
	ct_handle *h;
	ct_lock_handle lock;
	int c, rc;

	while ((c = getopt(argc, argv, "df:r:s:hv")) != -1) {
		switch (c) {
		case 'd':
			opt_debug++;
			break;
		case 'f':
			opt_config = optarg;
			break;
		case 'v':
			version();
		case 'h':
			usage(0);
		case 'r':
			opt_reader = atoi(optarg);
			break;
		case 's':
			opt_slot = atoi(optarg);
			break;
		default:
			usage(1);
		}
	}

	if (optind == argc)
		usage(1);

	cmd = argv[optind++];

	if (!strcmp(cmd, "list"))
		opt_command = CMD_LIST;
	else if (!strcmp(cmd, "atr"))
		opt_command = CMD_ATR;
	else if (!strcmp(cmd, "rwait"))
		opt_command = CMD_RWAIT;
	else if (!strcmp(cmd, "wait"))
		opt_command = CMD_WAIT;
	else if (!strcmp(cmd, "mf"))
		opt_command = CMD_MF;
	else if (!strcmp(cmd, "read"))
		opt_command = CMD_READ;
	else {
		fprintf(stderr, "Unknown command \"%s\"\n", cmd);
		usage(1);
	}

	if (opt_command == CMD_LIST) {
		int i;

		for (i = 0; i < OPENCT_MAX_READERS; i++) {
			ct_info_t info;

			if (ct_reader_info(i, &info) < 0)
				continue;
			printf(" %2d ", i);
			print_reader_info(&info);
		}

		exit(0);
	}

	if (opt_command == CMD_RWAIT) {
		while (1) {
			h = ct_reader_connect(opt_reader);
			if (h) {
				free(h);
				break;
			}
			sleep(1);
		}
		exit(0);
	}

	if (!(h = ct_reader_connect(opt_reader))) {
		fprintf(stderr, "Unknown reader #%u\n", opt_reader);
		return 1;
	}

	if (opt_command == CMD_WAIT) {
		int status;

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
		return 0;
	}

	printf("Detected ");
	print_reader(h);

	if ((rc = ct_card_lock(h, opt_slot, IFD_LOCK_SHARED, &lock)) < 0) {
		fprintf(stderr, "ct_card_lock: err=%d\n", rc);
		exit(1);
	}

	rc = do_reset(h, atr, sizeof(atr));

	switch (opt_command) {
	case CMD_ATR:
		print_atr(h, atr, rc);
		break;

	case CMD_MF:
		do_select_mf(h);
		break;

	case CMD_READ:{
			unsigned int address = 0, count = 1024;

			if (optind < argc)
				address = strtoul(argv[optind++], NULL, 0);
			if (optind < argc)
				count = strtoul(argv[optind++], NULL, 0);
			do_read_memory(h, address, count);
		}
		break;
	}

	ct_card_unlock(h, 0, lock);
	sleep(1);
	return 0;
}

static void version(void)
{
	fprintf(stdout, "OpenCT " VERSION "\n");
	exit(0);
}

static void usage(int exval)
{
	fprintf(exval ? stderr : stdout,
		"usage: openct-tool [-d] [-f configfile] [-r reader] command ...\n"
		"  -d   enable debugging; repeat to increase verbosity\n"
		"  -f   specify config file (default %s)\n"
		"  -r   specify index of reader to use\n"
		"  -s   specify slot of reader to use\n"
		"  -h   display this message\n"
		"  -v   display version and exit\n"
		"\n"
		"command: can be one of the following\n"
		" list  list all readers found\n"
		" atr   print ATR of card in selected reader\n"
		" wait  wait for card to be inserted\n"
		" rwait wait for reader to be attached\n"
		" mf    try to select main folder of card\n"
		" read  dump memory of synchronous card\n", OPENCT_CONF_PATH);
	exit(exval);
}

static int do_reset(ct_handle * h, unsigned char *atr, size_t atr_len)
{
	int rc, n, status;

	if ((rc = ct_card_status(h, opt_slot, &status)) < 0) {
		fprintf(stderr, "ct_card_status: err=%d\n", rc);
		exit(1);
	}

	printf("Card %spresent%s\n",
	       (status & IFD_CARD_PRESENT) ? "" : "not ",
	       (status & IFD_CARD_STATUS_CHANGED) ? ", status changed" : "");

	if (status & IFD_CARD_PRESENT) {
		n = ct_card_reset(h, opt_slot, atr, atr_len);
	} else {
		n = ct_card_request(h, opt_slot, 5, "Please insert card",
				    atr, atr_len);
	}

	if (n < 0) {
		fprintf(stderr, "failed to reset card\n");
		exit(1);
	}

	return n;
}

static void do_select_mf(ct_handle * h)
{
	unsigned char cmd[] =
	    { 0x00, 0xA4, 0x00, 0x00, 0x02, 0x3f, 0x00, 0x00 };
	unsigned char res[256];
	ct_lock_handle lock;
	int rc;

	if ((rc = ct_card_lock(h, opt_slot, IFD_LOCK_EXCLUSIVE, &lock)) < 0) {
		fprintf(stderr, "ct_card_lock: err=%d\n", rc);
		exit(1);
	}

      again:
	rc = ct_card_transact(h, opt_slot, cmd, sizeof(cmd), res, sizeof(res));
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
}

static void do_read_memory(ct_handle * h, unsigned int address,
			   unsigned int count)
{
	unsigned char buffer[8192];
	int rc;

	if (count > sizeof(buffer))
		count = sizeof(buffer);

	rc = ct_card_read_memory(h, opt_slot, address, buffer, count);
	if (rc < 0) {
		fprintf(stderr,
			"failed to read memory card: %s\n", ct_strerror(rc));
		exit(1);
	}

	printf("Read %u bytes at address 0x%04x\n", rc, address);
	dump(buffer, rc);
}

static void print_reader(ct_handle * h)
{
	ct_info_t info;
	int rc;

	if ((rc = ct_reader_status(h, &info)) < 0) {
		printf("ct_reader_status: err=%d\n", rc);
	} else {
		print_reader_info(&info);
	}
}

static void print_reader_info(ct_info_t * info)
{
	const char *sepa;

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

static void print_atr(ct_handle * h, unsigned char *atr, size_t len)
{
	unsigned int m;

	printf("ATR:");
	if (len == 0) {
		printf("<empty>");
	} else {
		for (m = 0; m < len; m++)
			printf(" %02x", atr[m]);
	}
	printf("\n");
}

static void dump(unsigned char *data, size_t len)
{
	unsigned int offset = 0;

	do {
		unsigned int i;

		printf("%04x:", offset);
		for (i = 0; i < 16; i++) {
			if (offset + i < len)
				printf(" %02x", data[offset + i]);
			else
				printf("   ");
		}
		printf("   ");
		for (i = 0; i < 16 && offset + i < len; i++) {
			int c = data[offset + i];

			if (!isprint(c) || (isspace(c) && c != ' '))
				c = '.';
			printf("%c", c);
		}
		offset += 16;
		printf("\n");
	} while (offset < len);
}
