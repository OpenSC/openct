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
#include <pwd.h>
#include <grp.h>
#include <limits.h>

#include <openct/path.h>
#include <openct/socket.h>
#include <openct/server.h>
#include <openct/logging.h>
#include "internal.h"
#include "ria.h"

static int opt_foreground = 0;
static char *opt_config = NULL;
static const char *opt_device_port = ":6666";
static const char *opt_server_port = "proxy";
static const char *opt_chroot = NULL;
static const char *opt_user = NULL;

static int get_ports(void);
static int run_server(int, char **);
static int run_client(int, char **);
static int list_devices(int, char **);
static void usage(int);
static void version(void);

int main(int argc, char **argv)
{
	char *command;
	int c;

	if (argc < 2)
		usage(1);

	ct_log_destination("@stderr");

	while ((c = getopt(argc, argv, "df:FR:U:v")) != -1) {
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
		case 'R':
			opt_chroot = optarg;
			break;
		case 'U':
			opt_user = optarg;
			break;
		case 'v':
			version();
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
	} else if (!strcmp(command, "export")) {
		return run_client(argc - optind, argv + optind);
	} else if (!strcmp(command, "list")) {
		list_devices(argc - optind, argv + optind);
	} else if (!strcmp(command, "version")) {
		version();
	} else {
		ct_error("Unknown command `%s'\n", command);
		return 1;
	}

	return 0;
}

static void enter_jail(void)
{
	struct passwd *pw = NULL;

	if (opt_chroot && !opt_user)
		opt_user = "nobody";
	if (opt_user) {
		if (!(pw = getpwnam(opt_user))) {
			ct_error("Unknown user %s\n", opt_user);
			exit(1);
		}
		endpwent();
	}

	if (opt_chroot) {
		if (chdir("/") < 0 || chroot(opt_chroot) < 0) {
			ct_error("chroot(%s) failed: %m", opt_chroot);
			exit(1);
		}
	}

	if (pw) {
		if (setgroups(0, NULL) < 0
		    || setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0) {
			ct_error("Failed to drop privileges: %m");
			exit(1);
		}
	}
}

static void background_process(void)
{
	int fd;

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

static int get_ports(void)
{
	char *address;
	int rc;

	if ((rc = ifd_conf_get_string("ifdproxy.device-port", &address)) >= 0)
		opt_device_port = address;
	if ((rc = ifd_conf_get_string("ifdproxy.server-port", &address)) >= 0)
		opt_server_port = address;
	return 0;
}

static int run_server(int argc, char **argv)
{
	int rc;
	char path[PATH_MAX];

	if (!ct_format_path(path, PATH_MAX, opt_server_port)) {
		return -1;
	}

	if (argc != 0)
		usage(1);
	if (ct_config.debug)
		ct_socket_reuseaddr(1);

	if ((rc = ria_svc_listen(path, 1)) < 0) {
		ct_error("Cannot bind to server port \"%s\": %s\n",
			 path, ct_strerror(rc));
		return rc;
	}
	if ((rc = ria_svc_listen(opt_device_port, 0)) < 0) {
		ct_error("Cannot bind to device port \"%s\": %s\n",
			 opt_device_port, ct_strerror(rc));
		return rc;
	}

	enter_jail();
	if (!opt_foreground)
		background_process();

	ct_mainloop();
	return 0;
}

static int run_client(int argc, char **argv)
{
	const char *name, *device, *address;
	ria_client_t *ria;
	int rc;

	/* Initialize IFD library */
	if (ifd_init())
		return 1;

	if (argc != 2 && argc != 3)
		usage(1);
	name = argv[0];
	device = argv[1];
	address = argc == 3 ? argv[2] : opt_device_port;

	ria = ria_export_device(address, device);

	ifd_debug(1, "About to register device as \"%s\"", name);
	if ((rc = ria_register_device(ria, name)) < 0) {
		ct_error("Unable to register device: %s\n", ct_strerror(rc));
		exit(1);
	}

	enter_jail();
	if (!opt_foreground)
		background_process();

	ct_mainloop();
	return 0;
}

static int list_devices(int argc, char **argv)
{
	unsigned char buffer[8192];
	ria_device_t *info;
	ria_client_t *clnt;
	unsigned int n, count;
	int rc;

	if (argc == 1)
		opt_server_port = argv[0];
	else if (argc > 1)
		usage(1);

	if (!(clnt = ria_connect(opt_server_port)))
		exit(1);
	rc = ria_command(clnt, RIA_MGR_LIST, NULL, 0, buffer, sizeof(buffer),
			 -1);
	if (rc < 0) {
		ct_error("Failed to list exported devices: %s",
			 ct_strerror(rc));
		ria_free(clnt);
		return 1;
	}

	count = rc / sizeof(ria_device_t);
	if (count == 0) {
		printf("No exported devices\n");
		ria_free(clnt);
		return 0;
	}

	printf("Exported devices\n");
	for (info = (ria_device_t *) buffer, n = 0; n < count; info++, n++) {
		printf("  %-16s %-30s %s\n",
		       info->handle, info->address, info->name);
	}

	ria_free(clnt);
	return 0;
}

static void version(void)
{
	fprintf(stderr, "OpenCT " VERSION "\n");
	exit(0);
}

static void usage(int exval)
{
	fprintf(exval ? stderr : stdout,
		"Usage:\n"
		"ifdproxy server [-dF]\n"
		"ifdproxy export [-dF] name device address\n"
		"ifdproxy list [-dF] address\n" "ifdproxy version\n");
	exit(exval);
}
