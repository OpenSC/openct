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

#include <openct/ifd.h>
#include <openct/conf.h>
#include <openct/logging.h>
#include <openct/socket.h>
#include <openct/device.h>
#include <openct/server.h>

#include "ifdhandler.h"

static int	opt_debug = 0;
static int	opt_hotplug = 0;
static int	opt_foreground = 0;
static int	opt_info = 0;
static unsigned int	opt_reader = -1;

static void	usage(int exval);
static void	ifdhandler_run(ifd_reader_t *);
static int	ifdhandler_poll_presence(ct_socket_t *, struct pollfd *);
static int	ifdhandler_accept(ct_socket_t *);
static int	ifdhandler_recv(ct_socket_t *);
static int	ifdhandler_send(ct_socket_t *);
static void	ifdhandler_close(ct_socket_t *);
static void	print_info(void);

int
main(int argc, char **argv)
{
	const char	*device = NULL, *driver;
	ifd_reader_t	*reader;
	ct_info_t	*status;
	int		c;

	/* Make sure the mask is good */
	umask(033);

	while ((c = getopt(argc, argv, "dFHhir:s")) != -1) {
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

	if (opt_info) {
		if (optind != argc)
			usage(1);
		ifd_init();
		print_info();
		return 0;
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

	ifd_device_set_hotplug(reader->device, opt_hotplug);

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
	char		socket_name[1024];
	ct_socket_t	*sock;
	int		rc;

	/* Activate reader */
	if ((rc = ifd_activate(reader)) < 0) {
		ct_error("Failed to activate reader; err=%d", rc);
		exit(1);
	}

	sock = ct_socket_new(0);
	snprintf(socket_name, sizeof(socket_name),
			OPENCT_SOCKET_PATH "/%u",
			opt_reader);
	if (ct_socket_listen(sock, socket_name, 0666) < 0) {
		ct_error("Failed to create server socket");
		exit(1);
	}

	sock->user_data = reader;
	sock->recv = ifdhandler_accept;
	ct_mainloop_add_socket(sock);

	/* Encapsulate the reader into a socket struct */
	sock = ct_socket_new(0);
	sock->fd = 0x7FFFFFFF;
	sock->poll = ifdhandler_poll_presence;
	sock->user_data = reader;
	ct_mainloop_add_socket(sock);

	/* Call the server loop */
	ct_mainloop();
	exit(0);
}

/*
 * Poll for presence of hotplug device
 */
int
ifdhandler_poll_presence(ct_socket_t *sock, struct pollfd *pfd)
{
	ifd_reader_t	*reader = (ifd_reader_t *) sock->user_data;
	ifd_device_t	*dev = reader->device;
	unsigned int	n;

	/* Check if the card status changed */
	for (n = 0; n < reader->nslots; n++) {
		static unsigned int	card_seq = 1;
		unsigned int		prev_seq, new_seq;
		ct_info_t		*info;
		time_t			now;
		int			rc, status;

		time(&now);
		if (now < reader->slot[n].next_update)
			continue;

		/* Poll card status at most once a second
		 * XXX: make this configurable */
		reader->slot[n].next_update = now + 1;

		if ((rc = ifd_card_status(reader, n, &status)) < 0) {
			/* Don't return error; let the hotplug test
			 * pick up the detach
			if (rc == IFD_ERROR_DEVICE_DISCONNECTED)
				return rc;
			 */
			continue;
		}

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

	if (dev->hotplug && ifd_device_poll_presence(dev, pfd) == 0) {
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
	char		buffer[CT_SOCKET_BUFSIZ+64];
	header_t	header;
	ct_buf_t	args, resp;
	int		rc;

	/* Error or client closed connection? */
	if ((rc = ct_socket_filbuf(sock, -1)) <= 0)
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
 * Display ifdhandler configuration stuff
 */
static void
print_list(const char **names, unsigned int n)
{
	unsigned int	i, width = 0, len;

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

void
print_info(void)
{
	const char	*names[64];
	unsigned int	n;

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
 * Usage message
 */
void
usage(int exval)
{
	fprintf(stderr,
"usage: ifdhandler [-Hds] [-r reader] driver [device]\n"
"  -r   specify index of reader\n"
"  -F   stay in foreground\n"
"  -H   hotplug device, monitor for detach\n"
"  -s   send error and debug messages to syslog\n"
"  -d   enable debugging; repeat to increase verbosity\n"
"  -h   display this message\n"
);
	exit(exval);
}

