/*
 * Manage a single reader
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
#include <fcntl.h>
#include <time.h>

#include <openct/ifd.h>
#include <openct/conf.h>
#include <openct/logging.h>
#include <openct/socket.h>
#include <openct/device.h>
#include <openct/server.h>

#include "ifdhandler.h"
#include "internal.h"

static int	opt_debug = 0;
static int	opt_hotplug = 0;
static int	opt_foreground = 0;
static int	opt_reader = -1;

static void	usage(int exval);
static void	ifdhandler_run(ifd_reader_t *);
static int	ifdhandler_poll_presence(struct pollfd *, unsigned int, void *);
static int	ifdhandler_accept(ct_socket_t *);
static int	ifdhandler_recv(ct_socket_t *);
static int	ifdhandler_send(ct_socket_t *);
static void	ifdhandler_close(ct_socket_t *);

int
main(int argc, char **argv)
{
	const char	*device, *driver;
	ifd_reader_t	*reader;
	ct_info_t	*status;
	int		c;

	/* Make sure the mask is good */
	umask(033);

	while ((c = getopt(argc, argv, "dFHhr:s")) != -1) {
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
		case 'r':
			opt_reader = atoi(optarg);
			break;
		case 's':
			ct_log_destination("@syslog");
			break;
		case 'h':
			usage(0);
		default:
			usage(1);
		}
	}

	if (optind < argc - 2 || optind > argc - 1)
		usage(1);

	driver = argv[optind++];
	if (optind < argc)
		device = argv[optind++];

	ct_config.debug = opt_debug;

	/* Initialize IFD library */
	ifd_init();

	/* Allocate a socket slot
	 * FIXME: may need to use a lock file here to
	 * prevent race condition
	 */
	status = ct_status_alloc_slot(&opt_reader);
	if (status == NULL) {
		ct_error("too many readers, no reader slot available");
		return 1;
	}

	/* Become a daemon if needed - we do this after allocating the
	 * slot so openct-control can synchronize slot allocation */
	if (!opt_foreground) {
		pid_t	pid;
		int	fd;

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
	if (!(reader = ifd_open(driver, device))) {
		ct_error("unable to open reader %s@%s", driver, device);
		return 1;
	}

	reader->status = status;
	strncpy(status->ct_name, reader->name, sizeof(status->ct_name)-1);
	status->ct_slots = reader->nslots;
	if (reader->flags & IFD_READER_DISPLAY)
		status->ct_display = 1;
	if (reader->flags & IFD_READER_KEYPAD)
		status->ct_keypad = 1;

	ifdhandler_run(reader);
	return 0;
}

/*
 * Spawn a new ifd handler thread
 */
void
ifdhandler_run(ifd_reader_t *reader)
{
	ct_socket_t	*sock;
	char		socket_name[16];
	int		rc;

	/* Activate reader */
	if ((rc = ifd_activate(reader)) < 0) {
		ct_error("Failed to activate reader; err=%d", rc);
		exit(1);
	}

	sock = ct_socket_new(0);
	snprintf(socket_name, sizeof(socket_name), "%u", opt_reader);
	if (ct_socket_listen(sock, socket_name, 0666) < 0) {
		ct_error("Failed to create server socket");
		exit(1);
	}

	sock->user_data = reader;
	sock->recv = ifdhandler_accept;

	/* Call the server loop */
	ct_mainloop(sock, ifdhandler_poll_presence, reader);
	exit(0);
}

/*
 * Poll for presence of hotplug device
 */
int
ifdhandler_poll_presence(struct pollfd *pfd, unsigned int max, void *ptr)
{
	ifd_reader_t	*reader = (ifd_reader_t *) ptr;
	ifd_device_t	*dev = reader->device;
	unsigned int	n;

	/* Check if the card status changed */
	for (n = 0; n < reader->nslots; n++) {
		static unsigned int	card_seq = 1;
		unsigned int		prev_seq, new_seq;
		ct_info_t		*info;
		time_t			now;
		int			status;

		time(&now);
		if (now < reader->slot[n].next_update)
			continue;

		/* Poll card status at most once a second
		 * XXX: make this configurable */
		reader->slot[n].next_update = now + 1;

		if (ifd_card_status(reader, n, &status) < 0)
			continue;

		info = reader->status;
		new_seq = prev_seq = info->ct_card[n];
		if (!(status & IFD_CARD_PRESENT))
			new_seq = 0;
		else
		if (!prev_seq || (status & IFD_CARD_STATUS_CHANGED)) {
			new_seq = card_seq++;
		}

		if (prev_seq != new_seq) {
			ifd_debug(1, "card status change: %u -> %u",
				prev_seq, new_seq);
			info->ct_card[n] = new_seq;
			ct_status_update(info);
		}
	}

	if (!opt_hotplug || !dev->ops->poll_presence)
		return 0;
	if (!dev->ops->poll_presence(dev, pfd)) {
		ifd_debug(1, "Reader %s detached", reader->name);
		memset(reader->status, 0, sizeof(*reader->status));
		exit(0);
	}
	return 1;
}


/*
 * Handle connection request from client
 */
static int
ifdhandler_accept(ct_socket_t *listener)
{
	ct_socket_t	*sock;

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
int
ifdhandler_recv(ct_socket_t *sock)
{
	ifd_reader_t	*reader;
	char		buffer[512];
	header_t	header;
	ct_buf_t	args, resp;
	int		rc;

	/* Error or client closed connection? */
	if ((rc = ct_socket_filbuf(sock)) <= 0)
		return -1;

	/* If request is incomplete, go back
	 * and wait for more
	 * XXX add timeout? */
	if (ct_socket_get_packet(sock, &header, &args) < 0)
		return 0;

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
int
ifdhandler_send(ct_socket_t *sock)
{
	return ct_socket_flsbuf(sock, 0);
}

/*
 * Socket is closed - for whatever reason
 * Release any locks held by this client
 */
void
ifdhandler_close(ct_socket_t *sock)
{
	ifdhandler_unlock_all(sock);
}

/*
 * Usage message
 */
void
usage(int exval)
{
	fprintf(stderr,
"usage: ifdhandler [-Hds] driver device socket\n"
"  -d   enable debugging; repeat to increase verbosity\n"
"  -H   hotplug device, monitor for detach\n"
"  -s   send error and debug messages to syslog\n"
"  -h   display this message\n"
);
	exit(exval);
}

