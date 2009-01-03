/*
 * Resource manager daemon - main loop
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <openct/socket.h>
#include <openct/server.h>
#include <openct/logging.h>

#define IFD_MAX_SOCKETS	256

static ct_socket_t sock_head;
static int leave_mainloop;

void ct_mainloop_add_socket(ct_socket_t * sock)
{
	if (sock)
		ct_socket_link(&sock_head, sock);
}

/*
 * Main loop
 */
void ct_mainloop(void)
{
	leave_mainloop = 0;
	while (!leave_mainloop) {
		struct pollfd pfd[IFD_MAX_SOCKETS + 1];
		ct_socket_t *poll_socket[IFD_MAX_SOCKETS];
		ct_socket_t *sock, *next;
		unsigned int nsockets = 0, npoll = 0;
		unsigned int n = 0, listening;
		int have_driver_with_poll = 0;
		int rc;

		/* Zap poll structure */
		memset(pfd, 0, sizeof(pfd));

		/* Count active sockets, and decide whether to
		 * accept additional connections or not. */
		for (sock = sock_head.next; sock; sock = next) {
			next = sock->next;
			/* Kill any dead or excess sockets */
			if (sock->fd < 0 || nsockets == IFD_MAX_SOCKETS) {
				ct_socket_free(sock);
			} else {
				nsockets++;
			}
		}
		listening = (nsockets < IFD_MAX_SOCKETS) ? POLLIN : 0;

		/* Now loop over all sockets and set up the poll structs */
		for (sock = sock_head.next; sock; sock = sock->next) {
			poll_socket[npoll] = sock;
			if (sock->poll) {
				have_driver_with_poll = 1;
				if (sock->poll(sock, &pfd[npoll]) == 1)
					npoll++;
			} else {
				if (sock->listener)
					sock->events = listening;

				pfd[npoll].fd = sock->fd;
				pfd[npoll].events = sock->events;
				npoll++;
			}
		}

		if (npoll == 0)
			break;

		rc = poll(pfd, npoll, have_driver_with_poll ? 1000 : -1);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			ct_error("poll: %m");
			break;
		}

		for (n = 0; n < npoll; n++) {
			sock = poll_socket[n];
			if (sock->poll) {
				if (sock->poll(sock, &pfd[n]) < 0) {
					ct_socket_free(sock);
					continue;
				}
				continue;
			}

			if (pfd[n].revents & POLLERR) {
				if (sock->error(sock) < 0) {
					ct_socket_free(sock);
					continue;
				}
			}
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

void ct_mainloop_leave(void)
{
	leave_mainloop = 1;
}
