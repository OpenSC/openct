/*
 * Resource manager daemon - main loop
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

#include <openct/socket.h>
#include <openct/server.h>
#include <openct/logging.h>

#define IFD_MAX_SOCKETS	256

static int	leave_mainloop;

/*
 * Main loop
 */
void
ct_mainloop(ct_socket_t *listener, ct_poll_fn_t *poll_more, void *user_data)
{
	ct_socket_t	head;
	struct pollfd	pfd[IFD_MAX_SOCKETS + 1];

	head.next = head.prev = NULL;
	ct_socket_link(&head, listener);

	leave_mainloop = 0;
	while (!leave_mainloop) {
		ct_socket_t	*poll_socket[IFD_MAX_SOCKETS];
		ct_socket_t	*sock, *next;
		unsigned int	nsockets = 0, npoll = 0;
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

		/* poll for unplug events of hotplug devices */
		if (poll_more)
			npoll += poll_more(&pfd[npoll], 1, user_data);

		if (npoll == 0)
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

		if (nsockets < npoll)
			poll_more(&pfd[nsockets], npoll - nsockets, user_data);
	}
}

void
ct_mainloop_leave(void)
{
	leave_mainloop = 1;
}
