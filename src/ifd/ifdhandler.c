/*
 * Manage a single reader
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>

#include <openct/path.h>
#include <openct/ifd.h>
#include <openct/conf.h>
#include <openct/logging.h>
#include <openct/socket.h>
#include <openct/device.h>
#include <openct/server.h>

#include "ifdhandler.h"

static int opt_debug = 0;
static int opt_hotplug = 0;
static int opt_foreground = 0;
static int opt_info = 0;
static int opt_poll = 0;
static const char *opt_reader = NULL;

static void usage(int exval);
static void version(void);
static void ifdhandler_run(ifd_reader_t *);
static int ifdhandler_poll_presence(ct_socket_t *, struct pollfd *);
static int ifdhandler_event(ct_socket_t * sock);
static int ifdhandler_accept(ct_socket_t *);
static int ifdhandler_error(ct_socket_t *);
static int ifdhandler_recv(ct_socket_t *);
static int ifdhandler_send(ct_socket_t *);
static void ifdhandler_close(ct_socket_t *);
static void print_info(void);

int main(int argc, char **argv)
{
	const char *driver = NULL, *type = NULL, *device = NULL;
	ifd_reader_t *reader;
	ct_info_t *status;
	int c;

	/* Make sure the mask is good */
	umask(033);

	while ((c = getopt(argc, argv, "dFHhvipr:s")) != -1) {
		switch (c) {
		case 'd':
			opt_debug++;
			break;
		case 'F':
			opt_foreground = 1;
			break;
		case 'H':
			opt_hotplug = 1;
			break;
		case 'i':
			opt_info = 1;
			break;
		case 'p':
			opt_poll = 1;
			break;
		case 'r':
			opt_reader = optarg;
			break;
		case 's':
			ct_log_destination("@syslog");
			break;
		case 'v':
			version();
		case 'h':
			usage(0);
		default:
			usage(1);
		}
	}

	if (opt_info) {
		if (optind != argc)
			usage(1);
		if (ifd_init())
			return 1;
		print_info();
		return 0;
	}

	if (optind != argc - 3)
		usage(1);

	driver = argv[optind++];
	type = argv[optind++];
	device = argv[optind++];

	ct_config.debug = opt_debug;

	/* Initialize IFD library */
	if (ifd_init())
		return 1;

	/* Allocate a socket slot
	 * FIXME: may need to use a lock file here to
	 * prevent race condition
	 */
	{
		int r = -1;
		char path[PATH_MAX];

		status = ct_status_alloc_slot(&r);
		if (status == NULL) {
			ct_error("too many readers, no reader slot available");
			return 1;
		}
		snprintf(path, PATH_MAX, "%d", r);
		opt_reader = strdup(path);
	}

	/* Become a daemon if needed - we do this after allocating the
	 * slot so openct-control can synchronize slot allocation */
	if (!opt_foreground) {
		pid_t pid;
		int fd;

		if ((pid = fork()) < 0) {
			ct_error("fork: %m");
			return 1;
		}

		if (pid) {
			status->ct_pid = pid;
			return 0;
		}

		if ((fd = open("/dev/null", O_RDWR)) >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			close(fd);
		}

		ct_log_destination("@syslog");
		setsid();
	}

	/* Create reader */
	{
		char *typedev = malloc(strlen(type) + strlen(device) + 2);
		if (!typedev) {
			ct_error("out of memory");
			return 1;
		}
		sprintf(typedev, "%s:%s", type, device);
		if (!(reader = ifd_open(driver, typedev))) {
			ct_error("unable to open reader %s %s %s", driver, type,
				 device);
			return 1;
		}
		free(typedev);
	}

	ifd_device_set_hotplug(reader->device, opt_hotplug);

	reader->status = status;
	strncpy(status->ct_name, reader->name, sizeof(status->ct_name) - 1);
	status->ct_slots = reader->nslots;
	if (reader->flags & IFD_READER_DISPLAY)
		status->ct_display = 1;
	if (reader->flags & IFD_READER_KEYPAD)
		status->ct_keypad = 1;

	ifdhandler_run(reader);
	return 0;
}

static void TERMhandler(int signo)
{
	ct_mainloop_leave();
}

/*
 * Spawn a new ifd handler thread
 */
static void ifdhandler_run(ifd_reader_t * reader)
{
	ct_socket_t *sock;
	int rc;
	struct sigaction act;
	char path[PATH_MAX];

	if (!ct_format_path(path, PATH_MAX, opt_reader)) {
		ct_error("ct_format_path failed!");
		exit(1);
	}

	/* Activate reader */
	if ((rc = ifd_activate(reader)) < 0) {
		ct_error("Failed to activate reader; err=%d", rc);
		exit(1);
	}

	sock = ct_socket_new(0);
	if (ct_socket_listen(sock, path, 0666) < 0) {
		ct_error("Failed to create server socket");
		exit(1);
	}

	sock->user_data = reader;
	sock->recv = ifdhandler_accept;
	ct_mainloop_add_socket(sock);

	/* Set an TERM signal handler for clean exit */
	act.sa_handler = TERMhandler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGTERM, &act, NULL);

	/* Encapsulate the reader into a socket struct */
	sock = ct_socket_new(0);
	if (opt_poll) {
		sock->fd = -1;
	}
	else {
		sock->fd = ifd_get_eventfd(reader, &sock->events);
	}
	if (sock->fd == -1) {
		ifd_debug(1, "events inactive for reader %s", reader->name);
		sock->fd = 0x7FFFFFFF;
		sock->poll = ifdhandler_poll_presence;
	}
	else {
		ifd_debug(1, "events active for reader %s", reader->name);
		sock->error = ifdhandler_error;
		sock->send = ifdhandler_event;
		ifd_before_command(reader);
		ifd_poll(reader);
		ifd_after_command(reader);
	}
	sock->user_data = reader;
	ct_mainloop_add_socket(sock);

	/* Call the server loop */
	ct_mainloop();
	ct_socket_unlink(sock);
	ct_socket_free(sock);
	memset(reader->status, 0, sizeof(*reader->status));
	ct_status_update(reader->status);
	ifd_debug(1, "ifdhandler for reader %s shut down", reader->name);

	exit(0);
}

static void exit_on_device_disconnect(ifd_reader_t *reader)
{
	ifd_debug(1, "Reader %s detached", reader->name);
	memset(reader->status, 0, sizeof(*reader->status));
	exit(0);
}

/*
 * Poll for presence of hotplug device
 */
static int ifdhandler_poll_presence(ct_socket_t * sock, struct pollfd *pfd)
{
	ifd_reader_t *reader = (ifd_reader_t *) sock->user_data;
	ifd_device_t *dev = reader->device;

	ifd_poll(reader);

	if (dev->hotplug && ifd_device_poll_presence(dev, pfd) == 0) {
		exit_on_device_disconnect(reader);
	}

	return 1;
}

/*
 * Error from socket
 */
static int ifdhandler_error(ct_socket_t * sock)
{
	ifd_reader_t *reader = (ifd_reader_t *) sock->user_data;

	if (ifd_error(reader) < 0) {
		exit_on_device_disconnect(reader);
	}

	return 0;
}

/*
 * Receive data from client
 */
static int ifdhandler_event(ct_socket_t * sock)
{
	ifd_reader_t *reader = (ifd_reader_t *) sock->user_data;

	if (ifd_event(reader) < 0) {
		exit_on_device_disconnect(reader);
	}

	return 0;
}

/*
 * Handle connection request from client
 */
static int ifdhandler_accept(ct_socket_t * listener)
{
	ct_socket_t *sock;

	if (!(sock = ct_socket_accept(listener)))
		return 0;

	sock->user_data = listener->user_data;
	sock->recv = ifdhandler_recv;
	sock->send = ifdhandler_send;
	sock->close = ifdhandler_close;
	return 0;
}

/*
 * Receive data from client
 */
static int ifdhandler_recv(ct_socket_t * sock)
{
	ifd_reader_t *reader;
	char buffer[CT_SOCKET_BUFSIZ + 64];
	header_t header;
	ct_buf_t args, resp;
	int rc;

	/* Error or client closed connection? */
	if ((rc = ct_socket_filbuf(sock, -1)) <= 0)
		return -1;

	/* If request is incomplete, go back
	 * and wait for more
	 * XXX add timeout? */
	if ((rc = ct_socket_get_packet(sock, &header, &args)) < 1)
		return rc;

	ct_buf_init(&resp, buffer, sizeof(buffer));

	reader = (ifd_reader_t *) sock->user_data;
	header.error = ifdhandler_process(sock, reader, &args, &resp);

	if (header.error)
		ct_buf_clear(&resp);

	/* Put packet into transmit buffer */
	header.count = ct_buf_avail(&resp);
	if (ct_socket_put_packet(sock, &header, &resp) < 0)
		return -1;

	/* Leave transmitting to the main server loop */
	return 0;
}

/*
 * Transmit data to client
 */
static int ifdhandler_send(ct_socket_t * sock)
{
	return ct_socket_flsbuf(sock, 0);
}

/*
 * Socket is closed - for whatever reason
 * Release any locks held by this client
 */
static void ifdhandler_close(ct_socket_t * sock)
{
	ifdhandler_unlock_all(sock);
}

/*
 * Display ifdhandler configuration stuff
 */
static void print_list(const char **names, unsigned int n)
{
	unsigned int i, width = 0, len;

	for (i = 0; i < n; i++) {
		len = 1 + strlen(names[i]);
		if (width && width + len > 64) {
			printf("\n");
			width = 0;
		}
		if (width == 0) {
			printf("   ");
			width = 3;
		}
		printf(" %s", names[i]);
		if (i < n - 1) {
			printf(",");
			len++;
		}
		width += len;
	}
	if (width)
		printf("\n");
}

static void print_info(void)
{
	const char *names[64];
	unsigned int n;

	n = ifd_drivers_list(names, 64);
	if (n == 0) {
		printf("No reader drivers configured\n");
	} else {
		printf("Reader drivers:\n");
		print_list(names, n);
	}

	n = ifd_protocols_list(names, 64);
	if (n == 0) {
		printf("No protocols configured\n");
	} else {
		printf("Protocols:\n");
		print_list(names, n);
	}
}

/*
 * Display version
 */
static void version(void)
{
	fprintf(stdout, "OpenCT " VERSION "\n");
	exit(0);
}

/*
 * Usage message
 */
static void usage(int exval)
{
	fprintf(exval ? stderr : stdout,
		"usage: ifdhandler [-Hds] [-r reader] driver type device\n"
		"  -r   specify index of reader\n"
		"  -F   stay in foreground\n"
		"  -H   hotplug device, monitor for detach\n"
		"  -p   force polling device even if events supported\n"
		"  -s   send error and debug messages to syslog\n"
		"  -d   enable debugging; repeat to increase verbosity\n"
		"  -i   display list of available drivers and protocols\n"
		"  -h   display this message\n"
		"  -v   display version and exit\n");
	exit(exval);
}
