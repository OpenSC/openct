/*
 * Resource manager daemon - main loop
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

#define IFD_MAX_SOCKETS	256

static int	leave_mainloop;
static void	mgr_reader_die(int);
static void	mgr_master_die(int);

/*
 * Main loop
 */
void
mgr_mainloop(ct_socket_t *listener, int master)
{
	struct sigaction act;
	ct_socket_t	head;
	struct pollfd	pfd[IFD_MAX_READERS + IFD_MAX_SOCKETS];

	/* Initialize signals */
	act.sa_handler = master? mgr_master_die : mgr_reader_die;
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);

	head.next = head.prev = NULL;
	ct_socket_link(&head, listener);

	leave_mainloop = 0;
	while (!leave_mainloop) {
		ct_socket_t	*poll_socket[IFD_MAX_SOCKETS];
		ifd_reader_t	*poll_reader[IFD_MAX_READERS];
		ct_socket_t	*sock, *next;
		unsigned int	nsockets = 0, nreaders = 0, npoll = 0;
		unsigned int	n = 0;
		int		rc;

		/* Zap poll structure */
		memset(pfd, 0, sizeof(pfd));

		/* Count active sockets */
		for (nsockets = 0, sock = head.next; sock; sock = next) {
			next = sock->next;
			if (sock->fd < 0) {
				ct_socket_free(sock);
			} else if (nsockets < IFD_MAX_SOCKETS) {
				poll_socket[nsockets++] = sock;

				pfd[npoll].fd = sock->fd;
				pfd[npoll].events = sock->events;
				npoll++;
			} else {
				/* should not happen */
				ct_error("too many open sockets?!");
				ct_socket_free(sock);
			}
		}

		/* poll for unplug events of hotplug readers */
		if (master && ct_config.hotplug) {
			ifd_reader_t	*reader;
			ifd_device_t	*dev;

			for (n = 0; n < IFD_MAX_READERS; n++) {
				if (!(reader = ifd_reader_by_index(n))
				 || !(dev = reader->device))
					continue;
				poll_reader[nreaders++] = reader;
				ifd_device_poll_presence(dev, &pfd[npoll]);
				npoll++;
			}
		}

		if (nsockets == 0 && nreaders)
			break;

		/* Stop accepting new connections if there are
		 * too many already */
		listener->events = (nsockets < IFD_MAX_SOCKETS)?  POLLIN : 0;

		rc = poll(pfd, npoll, 1000);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			ct_error("poll: %m");
			break;
		}

		for (n = 0; n < nsockets; n++) {
			sock = poll_socket[n];
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

		for (n = 0; n < nreaders; n++) {
			ifd_reader_t	*reader;
			ifd_device_t	*dev;

			reader = poll_reader[n];
			dev = reader->device;
			if (!ifd_device_poll_presence(dev,  &pfd[nsockets+n])) {
				ifd_debug(1, "Reader \"%s\" detached",
						reader->name);
				ifd_close(reader);
			}
		}
	}
}

void
mgr_reader_die(int sig)
{
	ifd_debug(1, "reader process terminated by signal %d", sig);
	exit(0);
}

void
mgr_master_die(int sig)
{
	ifd_debug(1, "master process received signal %d", sig);
	leave_mainloop = 1;
}
