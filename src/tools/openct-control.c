/*
 * OpenCT ifdhandler control
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <openct/logging.h>
#include <openct/conf.h>

#include "internal.h"

static int		mgr_killall(void);
static void		usage(int exval);

static const char *	opt_config = NULL;
static int		opt_debug = 0;
static int		opt_killall = 0;

static void		configure_reader(ifd_conf_node_t *);

int
main(int argc, char **argv)
{
	int		n, c;

	/* Make sure the mask is good */
	umask(033);

	while ((c = getopt(argc, argv, "df:hk")) != -1) {
		switch (c) {
		case 'd':
			opt_debug++;
			break;
		case 'f':
			opt_config = optarg;
			break;
		case 'k':
			opt_killall++;
			break;
		case 'h':
			usage(0);
		default:
			usage(1);
		}
	}

	if (optind != argc)
		usage(1);

	ct_config.debug = opt_debug;

	if (opt_killall) {
		mgr_killall();
		return 0;
	}

	/* Parse IFD config file */
	if (ifd_config_parse(opt_config) < 0)
		exit(1);

	/* Zap the status file */
	ct_status_clear(OPENCT_MAX_READERS);

	/* Initialize IFD library */
	ifd_init();

	/* Create an ifdhandler process for every reader defined
	 * in the config file */
	n = ifd_conf_get_nodes("reader", NULL, 0);
	if (n >= 0) {
		ifd_conf_node_t	**nodes;
		int		i;

		nodes = (ifd_conf_node_t **) calloc(n, sizeof(*nodes));
		n = ifd_conf_get_nodes("reader", nodes, n);
		for (i = 0; i < n; i++)
			configure_reader(nodes[i]);
		free(nodes);
	}

	/* Create an ifdhandler process for every hotplug reader found */
	mgr_scan_usb();

	return 0;
}

/*
 * Configure a reader using info from the config file
 */
void
configure_reader(ifd_conf_node_t *cf)
{
	static unsigned int nreaders = 0;
	char		*device, *driver;

	if (ifd_conf_node_get_string(cf, "device", &device) < 0) {
		ct_error("no device specified in reader configuration");
		return;
	}

	if (ifd_conf_node_get_string(cf, "driver", &driver) < 0)
		driver = "auto";

	if (device == NULL && driver == NULL) {
		ct_error("neither device nor driver specified "
			  "in reader configuration");
		return;
	}

	mgr_spawn_ifdhandler(driver, device, nreaders++);
}

/*
 * Spawn an ifdhandler
 */
int
mgr_spawn_ifdhandler(const char *driver, const char *device, int idx)
{
	const char	*argv[16];
	char		reader[16], debug[10];
	int		argc, n;
	pid_t		pid;

	ifd_debug(1, "driver=%s, device=%s, index=%d",
			driver, device, idx);

	if ((pid = fork()) < 0) {
		ct_error("fork failed: %m");
		return 0;
	}

	if (pid != 0)
		return pid;

	argc = 0;
	argv[argc++] = ct_config.ifdhandler;
	argv[argc++] = "-s";

	if (idx >= 0) {
		snprintf(reader, sizeof(reader), "-r%u", idx);
		argv[argc++] = reader;
	} else {
		argv[argc++] = "-H";
	}

	if (ct_config.debug) {
		if ((n = ct_config.debug) > 6)
			n = 6;
		debug[n+1] = '\0';
		while (n--)
			debug[n+1] = 'd';
		debug[0] = '-';
		argv[argc++] = debug;
	}

	argv[argc++] = driver;
	if (device)
		argv[argc++] = device;
	argv[argc] = NULL;

	n = getdtablesize();
	while (--n > 2)
		close(n);

	execv(ct_config.ifdhandler, (char **) argv);
	ct_error("failed to execute %s: %m", ct_config.ifdhandler);
	exit(1);
}

/*
 * Kill all ifdhandler processes
 */
int
mgr_killall(void)
{
	const ct_info_t	*status;
	int		num, killed = 0;

	if ((num = ct_status(&status)) < 0) {
		ct_error("cannot access status file; no readers killed");
		return -1;
	}

	while (num--) {
		if (status[num].ct_pid
		 && kill(status[num].ct_pid, SIGTERM) >= 0)
			killed++;
	}

	printf("%d process%s killed.\n", killed, (killed == 1)? "" : "es");
	return 0;
}

/*
 * Usage message
 */
void
usage(int exval)
{
	fprintf(stderr,
"usage: ifdd [-dk] [-f configfile]\n"
"  -d   enable debugging; repeat to increase verbosity\n"
"  -f   specify config file (default /etc/openct.conf\n"
"  -k   kill all reader processes\n"
"  -h   display this message\n"
);
	exit(exval);
}
