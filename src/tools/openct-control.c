/*
 * IFD resource manager daemon
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

#include <openct/ifd.h>
#include <openct/conf.h>
#include <openct/logging.h>
#include <openct/socket.h>
#include <openct/device.h>

#include "internal.h"

#define IFD_MAX_SOCKETS	256

static void		usage(int exval);

static const char *	opt_config = NULL;
static int		opt_debug = 0;
static int		opt_foreground = 0;

static void		configure_reader(ifd_conf_node_t *);

int
main(int argc, char **argv)
{
	int	n, c;

	/* Make sure the mask is good */
	umask(033);

	while ((c = getopt(argc, argv, "dFf:h")) != -1) {
		switch (c) {
		case 'd':
			opt_debug++;
			break;
		case 'F':
			opt_foreground = 1;
			break;
		case 'f':
			opt_config = optarg;
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

	/* Parse IFD config file */
	if (ifd_config_parse(opt_config) < 0)
		exit(1);

	if (!opt_foreground) {
		if (daemon(0, 0) < 0) {
			perror("failed to background process");
			exit(1);
		}

		setsid();

		/* change logging destination */
		ct_log_destination("@syslog");
	}

	/* Initialize IFD library */
	ifd_init();

	/* Create sub-processes for every reader defined
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

	mgr_scan_usb();

	mgr_master();
	return 0;
}

/*
 * Configure a reader using info from the config file
 */
void
configure_reader(ifd_conf_node_t *cf)
{
	char		*device, *driver;

	if (ifd_conf_node_get_string(cf, "device", &device) < 0) {
		ct_error("no device specified in reader configuration");
		return;
	}

	if (ifd_conf_node_get_string(cf, "driver", &driver) < 0)
		driver = "auto";

	mgr_spawn_ifdhandler(driver, device, 0);
}

/*
 * Usage message
 */
void
usage(int exval)
{
	fprintf(stderr,
"usage: ifdd [-Fd] [-f configfile]\n"
"  -F   master process remains in the foreground\n"
"  -d   enable debugging; repeat to increase verbosity\n"
"  -f   specify config file (default /etc/ifd.conf\n"
"  -h   display this message\n"
);
	exit(exval);
}
