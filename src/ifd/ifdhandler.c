/*
 * Resource manager - handle reader processes
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
#include <openct/server.h>

#include "ifdhandler.h"
#include "internal.h"

static int	opt_debug = 0;
static int	opt_hotplug = 0;

static void	usage(int exval);
static void	ifdhandler_run(const char *, ifd_reader_t *);
static int	ifdhandler_poll_presence(struct pollfd *, unsigned int, void *);
static int	ifdhandler_accept(ct_socket_t *);
static int	ifdhandler_recv(ct_socket_t *);
static int	ifdhandler_send(ct_socket_t *);
static void	ifdhandler_close(ct_socket_t *);

int
main(int argc, char **argv)
{
	const char	*socket, *device, *driver;
	ifd_reader_t	*reader;
	int		c;

	/* Make sure the mask is good */
	umask(033);

	while ((c = getopt(argc, argv, "dHhs")) != -1) {
		switch (c) {
		case 'd':
			opt_debug++;
			break;
		case 'H':
			opt_hotplug = 1;
			break;
		case 'h':
			usage(0);
		case 's':
			ct_log_destination("@syslog");
			break;
		default:
			usage(1);
		}
	}

	if (optind != argc - 3)
		usage(1);
	driver = argv[optind++];
	device = argv[optind++];
	socket = argv[optind++];

	ct_config.debug = opt_debug;

	/* Initialize IFD library */
	ifd_init();

	/* Create reader */
	if (!(reader = ifd_open(driver, device))) {
		ct_error("unable to open reader %s@%s", driver, device);
		return 1;
	}

	ifdhandler_run(socket, reader);
	return 0;
}

/*
 * Spawn a new ifd handler thread
 */
void
ifdhandler_run(const char *socket_name, ifd_reader_t *reader)
{
	ct_socket_t	*sock;
	int		rc;

	/* Activate reader */
	if ((rc = ifd_activate(reader)) < 0) {
		ct_error("Failed to activate reader; err=%d", rc);
		exit(1);
	}

	sock = ct_socket_new(0);
	if (ct_socket_listen(sock, socket_name, 0666) < 0) {
		ct_error("Failed to create server socket");
		exit(1);
	}

	sock->user_data = reader;
	sock->recv = ifdhandler_accept;

	/* Call the server loop */
	if (opt_hotplug)
		ct_mainloop(sock, ifdhandler_poll_presence, reader);
	else
		ct_mainloop(sock, NULL, NULL);
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

	if (!dev->ops->poll_presence)
		return 0;
	if (!dev->ops->poll_presence(dev, pfd)) {
		ifd_debug(1, "Reader %s detached", reader->name);
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

