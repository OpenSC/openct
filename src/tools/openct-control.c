/*
 * IFD resource manager daemon
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <openct/ifd.h>
#include <openct/conf.h>
#include <openct/logging.h>

#include <openct/socket.h>
#include "internal.h"

static void		usage(int exval);

static const char *	opt_config = NULL;
static int		opt_debug = 0;

static void		mgr_spawn_handler(unsigned int, ifd_reader_t *);
static int		mgr_accept(ct_socket_t *);
static int		mgr_recv(ct_socket_t *);
static int		mgr_send(ct_socket_t *);
static void		mgr_close(ct_socket_t *);

int
main(int argc, char **argv)
{
	int	c;

	while ((c = getopt(argc, argv, "df:h")) != -1) {
		switch (c) {
		case 'd':
			opt_debug++;
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

	/* Initialize IFD library */
	ifd_init();

	/* Test code: just listen on the socket for reader #0 */
	mgr_spawn_handler(0, ifd_reader_by_index(0));

	return 0;
}

/*
 * Spawn a new ifd handler thread
 */
void
mgr_spawn_handler(unsigned int idx, ifd_reader_t *reader)
{
	char		socket_name[128];
	ct_socket_t	*sock;
	int		rc;

	/* XXX - fork process here */

	/* Activate reader */
	if ((rc = ifd_activate(reader)) < 0) {
		ct_error("Failed to activate reader; err=%d", rc);
		exit(1);
	}

	/* Make sure directory exists */
	if (mkdir(ct_config.socket_dir, 0755) < 0
	 && errno != EEXIST) {
		ct_error("Unable to create %s: %m",
				ct_config.socket_dir);
		exit(1);
	}
	chmod(ct_config.socket_dir, 0755);

	snprintf(socket_name, sizeof(socket_name),
			"%s/%u", ct_config.socket_dir, idx);

	sock = ct_socket_new(0);
	if (ct_socket_listen(sock, socket_name) < 0) {
		ct_error("Failed to create server socket");
		exit(1);
	}

	sock->user_data = reader;
	sock->recv = mgr_accept;

	/* Call the server loop */
	ct_socket_server_loop(sock);
}

/*
 * Handle connection request from client
 */
static int
mgr_accept(ct_socket_t *listener)
{
	ct_socket_t	*sock;

	if (!(sock = ct_socket_accept(listener)))
		return 0;

	sock->user_data = listener->user_data;
	sock->recv = mgr_recv;
	sock->send = mgr_send;
	sock->close = mgr_close;
	return 0;
}

/*
 * Receive data from client
 */
int
mgr_recv(ct_socket_t *sock)
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
	header.error = mgr_process(sock, reader, &args, &resp);

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
mgr_send(ct_socket_t *sock)
{
	return ct_socket_flsbuf(sock, 0);
}

/*
 * Socket is closed - for whatever reason
 * Release any locks held by this client
 */
void
mgr_close(ct_socket_t *sock)
{
	mgr_unlock_all(sock);
}


/*
 * Usage message
 */
void
usage(int exval)
{
	fprintf(stderr,
"usage: print-atr [-d] [-f configfile]\n"
"  -d   enable debugging; repeat to increase verbosity\n"
"  -f   specify config file (default /etc/ifd.conf\n"
"  -h   display this message\n"
);
	exit(exval);
}
