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

#include "internal.h"

static int	mgr_accept(ct_socket_t *);
static int	mgr_recv(ct_socket_t *);
static int	mgr_send(ct_socket_t *);
static void	mgr_close(ct_socket_t *);

/*
 * Spawn a new ifd handler thread
 */
pid_t
mgr_spawn_reader(unsigned int idx, ifd_reader_t *reader)
{
	char		socket_name[128];
	ct_socket_t	*sock;
	pid_t		pid;
	int		rc;

	/* fork process here */
	if ((pid = fork()) < 0) {
		ct_error("failed to fork reader handler: %m");
		return -1;
	}

	/* parent returns */
	if (pid != 0)
		return pid;

	setsid();

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
	if (ct_socket_listen(sock, socket_name, 0666) < 0) {
		ct_error("Failed to create server socket");
		exit(1);
	}

	sock->user_data = reader;
	sock->recv = mgr_accept;

	/* Call the server loop */
	mgr_mainloop(sock, 0);
	exit(0);
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
