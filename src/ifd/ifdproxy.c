/*
 * Remote device access - debugging utility that allows to
 * test smart card readers on remote hosts.
 * 
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <openct/socket.h>
#include <openct/server.h>
#include <openct/logging.h>
#include "internal.h"
#include "ria.h"

#define DEFAULT_SERVER_PATH	OPENCT_SOCKET_PATH "/proxy"

static int		opt_foreground = 0;
static char *		opt_config = NULL;
static const char *	opt_device_port = ":6666";
static const char *	opt_server_port = OPENCT_SOCKET_PATH "/proxy";

static int		get_ports(void);
static int		run_server(int, char **);
static int		run_client(int, char **);
static int		list_devices(int, char **);
static void		usage(int);

int
main(int argc, char **argv)
{
	char	*command;
	int	c;

	if (argc < 2)
		usage(1);

	while ((c = getopt(argc, argv, "df:F")) != -1) {
		switch (c) {
		case 'd':
			ct_config.debug++;
			break;
		case 'f':
			opt_config = optarg;
			break;
		case 'F':
			opt_foreground++;
			break;
		default:
			usage(1);
		}
	}

	if (ifd_config_parse(opt_config) < 0)
		return 1;

	if (optind >= argc)
		usage(1);
	command = argv[optind++];

	if (get_ports() < 0)
		return 1;

	if (!strcmp(command, "server")) {
		run_server(argc - optind, argv + optind);
	} else
	if (!strcmp(command, "export")) {
		run_client(argc - optind, argv + optind);
	} else
	if (!strcmp(command, "list")) {
		list_devices(argc - optind, argv + optind);
	} else {
		ct_error("Unknown command `%s'\n", command);
		return 1;
	}

	return 0;
}

#ifndef HAVE_DAEMON

static int
daemon(int nochdir, int noclose)
{
	pid_t pid;

	pid = fork();

	/* In case of fork is error. */
	if (pid < 0) {
		perror("fork");
		return -1;
	}

	/* In case of this is parent process. */
	if (pid != 0)
		exit(0);

	/* Become session leader and get pid. */
	pid = setsid();

	if (pid < -1) {
		perror("setsid");
		return -1;
	}

	/* Change directory to root. */
	if (!nochdir)
		chdir("/");

	/* File descriptor close. */
	if (!noclose) {
		int fd;

		fd = open("/dev/null", O_RDWR, 0);
		if (fd != -1) {
			dup2(fd, STDIN_FILENO);
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
			if (fd > 2)
				close(fd);
		}
	}
	umask(0027);
	return 0;
}

#endif

static void
background_process(void)
{
	int	fd;

	if (daemon(0, 0) < 0) {
		ct_error("failed to background process: %m");
		exit(1);
	}

	if ((fd = open("/dev/null", O_RDWR)) >= 0) {
		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		if (fd > 2)
			close(fd);
	}

	ct_log_destination("@syslog");
	setsid();
}

static int
get_ports(void)
{
	char	*address;
	int	rc;

	if ((rc = ifd_conf_get_string("ifdproxy.device-port", &address)) >= 0)
		opt_device_port = address;
	if ((rc = ifd_conf_get_string("ifdproxy.server-port", &address)) >= 0)
		opt_server_port = address;
	return 0;
}

int
run_server(int argc, char **argv)
{
	int	rc;

	if (argc != 0)
		usage(1);
	if (ct_config.debug)
		ct_socket_reuseaddr(1);

	if ((rc = ria_svc_listen(opt_server_port, 1)) < 0) {
		ct_error("Cannot bind to server port \"%s\": %s\n",
				opt_server_port, ct_strerror(rc));
		return rc;
	}
	if ((rc = ria_svc_listen(opt_device_port, 0)) < 0) {
		ct_error("Cannot bind to device port \"%s\": %s\n",
				opt_device_port, ct_strerror(rc));
		return rc;
	}

	if (!opt_foreground)
		background_process();

	ct_mainloop();
	return 0;
}

int
run_client(int argc, char **argv)
{
	const char	*name, *device, *address;
	ria_client_t	*ria;
	int		rc;

	/* Initialize IFD library */
	ifd_init();

	if (argc != 2 && argc != 3)
		usage(1);
	name    = argv[0];
	device  = argv[1];
	address = argc == 3? argv[2] : opt_device_port;

	ria = ria_export_device(address, device);

	ifd_debug(1, "About to register device as \"%s\"", name);
	if ((rc = ria_register_device(ria, name)) < 0) {
		ct_error("Unable to register device: %s\n",
				ct_strerror(rc));
		exit(1);
	}

	if (!opt_foreground)
		background_process();

	ct_mainloop();
	return 0;
}

int
list_devices(int argc, char **argv)
{
	unsigned char	buffer[8192];
	ria_device_t	*info;
	ria_client_t	*clnt;
	unsigned int	n, count;
	int		rc;

	if (argc == 1)
		opt_server_port = argv[0];
	else if (argc > 1)
		usage(1);

	if (!(clnt = ria_connect(opt_server_port)))
		exit(1);
	rc = ria_command(clnt, RIA_MGR_LIST, NULL, 0, buffer, sizeof(buffer));
	if (rc < 0) {
		ct_error("Failed to list exported devices: %s",
				ct_strerror(rc));
		return 1;
	}

	count = rc / sizeof(ria_device_t);
	if (count == 0) {
		printf("No exported devices\n");
		return 0;
	}

	printf("Exported devices\n");
	for (info = (ria_device_t *) buffer, n = 0; n < count; n++) {
		printf("  %-16s %-30s %s\n",
				info->handle,
				info->address,
				info->name);
	}

	return 0;
}

void
usage(int exval)
{
	fprintf(stderr,
	"Usage:\n"
	"ifdproxy server [-dF]\n"
	"ifdproxy export [-dF] name device address\n"
	"ifdproxy list [-dF] address\n"
	       );
	exit(exval);
}
