/*
 * IFD resource manager daemon
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ifd/core.h>
#include <ifd/conf.h>
#include <ifd/logging.h>

#include <ifd/socket.h>
#include "internal.h"

static void		usage(int exval);

static const char *	opt_config = NULL;
static int		opt_debug = 0;

static void		mgr_spawn_handler(unsigned int, ifd_reader_t *);
static int		mgr_accept(ifd_socket_t *);
static int		mgr_recv(ifd_socket_t *);
static int		mgr_send(ifd_socket_t *);

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

	/* Initialize IFD library */
	ifd_init();

	/* Parse IFD config file */
	if (ifd_config_parse(opt_config) < 0)
		exit(1);

	if (opt_debug > ifd_config.debug)
		ifd_config.debug = opt_debug;

	ifd_hotplug_init();

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
	ifd_socket_t	*sock;

	/* XXX - fork process here */

	snprintf(socket_name, sizeof(socket_name),
			"%s/%u", ifd_config.socket_dir, idx);

	sock = ifd_socket_new(0);
	if (ifd_socket_listen(sock, socket_name) < 0) {
		ifd_error("Failed to create server socket");
		exit(1);
	}

	sock->user_data = reader;
	sock->recv = mgr_accept;

	/* Call the server loop */
	ifd_socket_server_loop(sock);
}

/*
 * Handle connection request from client
 */
static int
mgr_accept(ifd_socket_t *listener)
{
	ifd_socket_t	*sock;

	if (!(sock = ifd_socket_accept(listener)))
		return 0;

	sock->user_data = listener->user_data;
	sock->recv = mgr_recv;
	sock->send = mgr_send;
	return 0;
}

/*
 * Receive data from client
 */
int
mgr_recv(ifd_socket_t *sock)
{
	ifd_reader_t	*reader;
	char		buffer[512];
	header_t	header;
	ifd_buf_t	args, resp;
	int		rc;

	/* Error or client closed connection? */
	if ((rc = ifd_socket_filbuf(sock)) <= 0)
		return -1;

	/* If request is incomplete, go back
	 * and wait for more
	 * XXX add timeout? */
	if (ifd_socket_get_packet(sock, &header, &args) < 0)
		return 0;

	ifd_buf_init(&resp, buffer, sizeof(buffer));

	reader = (ifd_reader_t *) sock->user_data;
	header.error = mgr_process(reader, &args, &resp);

	if (header.error)
		ifd_buf_clear(&resp);

	/* Put packet into transmit buffer */
	header.count = ifd_buf_avail(&resp);
	if (ifd_socket_put_packet(sock, &header, &resp) < 0)
		return -1;

	/* Leave transmitting to the main server loop */
	return 0;
}

/*
 * Transmit data to client
 */
int
mgr_send(ifd_socket_t *sock)
{
	return ifd_socket_flsbuf(sock, 0);
}

/*
int
mgr_process(ifd_buf_t *argbuf, ifd_buf_t *resbuf)
{
	unsigned char	cmd, unit;
	ifd_tlv_t	args, resp;

	memset(args, 0, sizeof(args));
	memset(resp, 0, sizeof(resp));

	if (ifd_buf_get(argbuf, &cmd, 1) < 0
	 || ifd_buf_get(argbuf, &unit, 1) < 0
	 || mgr_tlv_parse(argbuf, &args) < 0)
		return IFD_ERROR_INVALID_MSG;

	switch (cmd) {
	case IFD_CMD_STATUS:
		return mgr_status(unit, &args, &resp);
	}

	return IFD_ERROR_INVALID_CMD;
}
 */

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
