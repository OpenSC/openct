/*
 * IFD resource manager daemon
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <getopt.h>
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

#include "internal.h"

#define IFD_MAX_SOCKETS	256

static void		usage(int exval);

static const char *	opt_config = NULL;
static int		opt_debug = 0;
static int		opt_foreground = 0;

static pid_t		mgr_spawn_handler(unsigned int, ifd_reader_t *);
static int		mgr_accept(ct_socket_t *);
static int		mgr_recv(ct_socket_t *);
static int		mgr_send(ct_socket_t *);
static void		mgr_close(ct_socket_t *);
static void		mgr_mainloop(ct_socket_t *);

int
main(int argc, char **argv)
{
	unsigned int	n, count;
	int	c;

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
		/* XXX change logging destination */
		/* ct_log_destination("@syslog"); */
	}

	/* Initialize IFD library */
	ifd_init();

	/* Create sub-processes for every reader present */
	count = ifd_reader_count();
	for (n = 0; n < count; n++) {
		ifd_reader_t	*reader;

		if ((reader = ifd_reader_by_index(n)) != NULL)
			mgr_spawn_handler(n, reader);
	}

	return 0;
}

/*
 * Master thread; mostly doing the hotplug stuff and
 * managing the coming and going of its children
 */
void
mgr_master(void)
{
	ct_socket_t	*sock;
	char		socket_name[128];

	snprintf(socket_name, sizeof(socket_name),
			"%s/master", ct_config.socket_dir);

	sock = ct_socket_new(0);
	if (ct_socket_listen(sock, socket_name) < 0) {
		ct_error("Failed to create server socket");
		exit(1);
	}

	//sock->recv = mgr_master_accept;

	/* Call the server loop */
	mgr_mainloop(sock);
}

/*
 * Spawn a new ifd handler thread
 */
pid_t
mgr_spawn_handler(unsigned int idx, ifd_reader_t *reader)
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
	mgr_mainloop(sock);
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

/*
 * Main loop
 */
void
mgr_mainloop(ct_socket_t *listener)
{
	unsigned int	nsockets = 1;
	ct_socket_t	head;
	struct pollfd	pfd[IFD_MAX_SOCKETS];

	head.next = head.prev = NULL;
	ct_socket_link(&head, listener);

	while (1) {
		ct_socket_t	*sock, *next;
		unsigned int	n = 0;
		int		rc;

		/* Count active sockets */
		for (nsockets = 0, sock = head.next; sock; sock = next) {
			next = sock->next;
			if (sock->fd < 0)
				ct_socket_free(sock);
			else
				nsockets++;
		}

		if (nsockets == 0)
			break;

		/* Stop accepting new connections if there are
		 * too many already */
		listener->events = (nsockets < IFD_MAX_SOCKETS)?  POLLIN : 0;

		/* Make sure we don't exceed the max socket limit */
		assert(nsockets < IFD_MAX_SOCKETS);

		/* Set up the poll structure */
		for (n = 0, sock = head.next; sock; sock = sock->next, n++) {
			pfd[n].fd = sock->fd;
			pfd[n].events = sock->events;
		}

		rc = poll(pfd, n, 1000);
		if (rc < 0) {
			if (rc == EINTR)
				continue;
			ct_error("poll: %m");
			exit(1);
		}

		for (n = 0, sock = head.next; sock; sock = next, n++) {
			next = sock->next;
			if (pfd[n].revents & POLLOUT) {
				if (sock->send(sock) < 0) {
					ct_socket_free(sock);
					continue;
				}
			}
			if (pfd[n].revents & POLLIN) {
				if ((rc = sock->recv(sock)) < 0) {
					ct_socket_free(sock);
					continue;
				}
			}
		}
	}
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
