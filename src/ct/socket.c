/*
 * Socket handling routines
 *
 * Copyright (C) 2003, Olaf Kirch <okir@caldera.de>
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

#include <openct/logging.h>
#include <openct/socket.h>
#include <openct/pathnames.h>
#include <openct/error.h>


static unsigned int	ifd_xid = 0;

static inline unsigned int
min(unsigned int a, unsigned int b)
{
	return (a < b)? a : b;
}

static int	ct_socket_getcreds(ct_socket_t *);

/*
 * Create a socket object
 */
ct_socket_t *
ct_socket_new(unsigned int bufsize)
{
	ct_socket_t	*sock;

	sock = (ct_socket_t *) calloc(1, sizeof(*sock) + bufsize);
	if (sock == NULL)
		return NULL;

	/* Initialize socket buffer */
	ct_buf_init(&sock->buf, (sock + 1), bufsize);
	sock->fd = -1;

	return sock;
}

/*
 * Free a socket object
 */
void
ct_socket_free(ct_socket_t *sock)
{
	ct_socket_unlink(sock);
	if (sock->close)
		sock->close(sock);
	ct_socket_close(sock);
	free(sock);
}

/*
 * Make the socket path
 */
static char *
ct_socket_makepath(char *path, size_t size, const char *name)
{
	if (name[0] == '/') {
		snprintf(path, size, "%s", name);
	} else {
		snprintf(path, size, OPENCT_SOCKET_PATH "/%s", name);
	}
	return path;
}

/*
 * Create a client socket
 */
int
ct_socket_connect(ct_socket_t *sock, const char *path)
{
	struct sockaddr_un un;
	int		fd = -1;

	ct_socket_close(sock);

	memset(&un, 0, sizeof(un));
	un.sun_family = AF_UNIX;
	ct_socket_makepath(un.sun_path, sizeof(un.sun_path), path);

	if ((fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0
	 || fcntl(fd, F_SETFD, 1) < 0 /* close on exec */
	 || connect(fd, (struct sockaddr *) &un, sizeof(un)) < 0)
		return -1;

	sock->fd   = fd;
	return 0;
}

/*
 * Listen on a socket
 */
int
ct_socket_listen(ct_socket_t *sock, const char *pathname, int mode)
{
	struct sockaddr_un un;
	int		fd;

	ct_socket_close(sock);

	memset(&un, 0, sizeof(un));
	un.sun_family = AF_UNIX;
	ct_socket_makepath(un.sun_path, sizeof(un.sun_path), pathname);
	unlink(un.sun_path);

	if ((fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0
	 || bind(fd, (struct sockaddr *) &un, sizeof(un)) < 0
	 || listen(fd, 5) < 0)
		return -1;

	chmod(un.sun_path, mode);

	sock->events = POLLIN;
	sock->fd = fd;
	return 0;
}

/*
 * Accept incoming connection
 */
ct_socket_t *
ct_socket_accept(ct_socket_t *sock)
{
	ct_socket_t	*svc;
	int		fd;

	if (!(svc = ct_socket_new(CT_SOCKET_BUFSIZ)))
		return NULL;

	if ((fd = accept(sock->fd, NULL, NULL)) < 0) {
		ct_socket_free(svc);
		return NULL;;
	}

	svc->events = POLLIN;
	svc->fd = fd;

	/* obtain client credentials */
	svc->client_uid = -2;
	ct_socket_getcreds(svc);

	/* Add socket to list */
	ct_socket_link(sock, svc);

	return svc;
}

/*
 * Get client credentials
 * Should move this to a platform specific file
 */
int
ct_socket_getcreds(ct_socket_t *sock)
{
#ifdef SO_PEERCRED
	struct ucred	creds;
	socklen_t	len;

	len = sizeof(creds);
	if (getsockopt(sock->fd, SOL_SOCKET, SO_PEERCRED, &creds, &len) < 0)
		return -1;
	sock->client_uid = creds.uid;
#endif
	return 0;
}

/*
 * Close socket
 */
void
ct_socket_close(ct_socket_t *sock)
{
	ct_buf_clear(&sock->buf);
	if (sock->fd >= 0)
		close(sock->fd);
	sock->fd = -1;
}

/*
 * Transmit a call and receive the response
 */
int
ct_socket_call(ct_socket_t *sock, ct_buf_t *args, ct_buf_t *resp)
{
	ct_buf_t	*bp = &sock->buf, data;
	unsigned int	xid = ifd_xid++, avail;
	header_t	header;
	int		rc;

	/* Compact send buffer */
	ct_buf_compact(bp);

	/* Build header - note there's no need to convert
	 * integers to network byte order: everything happens
	 * on the same host, so there's no byte order issue */
	header.xid   = xid;
	header.count = ct_buf_avail(args);
	header.dest  = 0;

	/* Put everything into send buffer and transmit */
	if ((rc = ct_socket_put_packet(sock, &header, args)) < 0
	 || (rc = ct_socket_flsbuf(sock, 1)) < 0)
		return rc;

	/* Loop until we receive a complete packet with the
	 * right xid in it */
	do {
		if ((rc = ct_socket_filbuf(sock)) < 0)
			return -1;

		ct_buf_clear(resp);
		if ((rc = ct_socket_get_packet(sock, &header, &data)) < 0)
			return rc;
	}  while (rc == 0 || header.xid != xid);

	if (header.error)
		return header.error;

	avail = ct_buf_avail(&data);
	if (avail > ct_buf_tailroom(resp)) {
		ct_error("received truncated reply (%u out of %u bytes)",
				rc, header.count);
		return IFD_ERROR_BUFFER_TOO_SMALL;
	}

	ct_buf_put(resp, ct_buf_head(&data), avail);
	return header.count;
}

/*
 * Put packet into send buffer
 */
int
ct_socket_put_packet(ct_socket_t *sock, header_t *hdr, ct_buf_t *data)
{
	ct_buf_t	*bp = &sock->buf;

	ct_buf_clear(bp);
	if (ct_buf_put(bp, hdr, sizeof(*hdr)) < 0
	 || (data &&
	     ct_buf_put(bp, ct_buf_head(data), ct_buf_avail(data)) < 0)) {
		ct_error("packet too large for buffer");
		return -1;
	}

	sock->events = POLLOUT;
	return 0;
}

int
ct_socket_puts(ct_socket_t *sock, const char *string)
{
	ct_buf_t	*bp = &sock->buf;

	ct_buf_clear(bp);
	if (ct_buf_puts(bp, string) < 0) {
		ct_error("string too large for buffer");
		return -1;
	}

	sock->events = POLLOUT;
	return 0;
}

/*
 * Get packet from buffer
 */
int
ct_socket_get_packet(ct_socket_t *sock, header_t *hdr, ct_buf_t *data)
{
	ct_buf_t	*bp = &sock->buf;
	unsigned int	avail;
	header_t	th;

	avail = ct_buf_avail(bp);
	if (avail < sizeof(header_t))
		return 0;

	memcpy(&th, ct_buf_head(bp), sizeof(th));
	if (avail >= sizeof(header_t) + th.count) {
		/* There's enough data in the buffer
		 * Extract header... */
		ct_buf_get(bp, hdr, sizeof(*hdr));

		/* ... set data buffer (don't copy, just set pointers) ... */
		ct_buf_set(data, ct_buf_head(bp), hdr->count);

		/* ... and advance head pointer */
		ct_buf_get(bp, NULL, hdr->count);
		return 1;
	}

	/* Check if this packet will ever fit into this buffer */
	if (ct_buf_size(bp) < sizeof(header_t) + th.count) {
		ct_error("packet too large for buffer");
		return -1;
	}

	return 0;
}

int
ct_socket_gets(ct_socket_t *sock, char *buffer, size_t size)
{
	return ct_buf_gets(&sock->buf, buffer, size);
}

/*
 * Read some data from socket and put it into buffer
 */
int
ct_socket_filbuf(ct_socket_t *sock)
{
	ct_buf_t	*bp = &sock->buf;
	unsigned int	count;
	int		n;

	if (!(count = ct_buf_tailroom(bp))) {
		ct_buf_compact(bp);
		if (!(count = ct_buf_tailroom(bp))) {
			ct_error("packet too large");
			return -1;
		}
	}

	n = read(sock->fd, ct_buf_tail(bp), count);
	if (n < 0) {
		ct_error("socket recv error: %m");
		return -1;
	}

	/* When EOF occurs, the server's recv() routine
	 * should deal with it instantly. If we come here
	 * a second time we may be looping on a closed
	 * socket, so error out
	 */
	if (n == 0) {
		if (sock->eof) {
			ct_error("Peer closed connection");
			return -1;
		}
		sock->eof = 1;
		return 0;
	}

	/* Advance buffer tail pointer */
	ct_buf_put(bp, NULL, n);
	return n;
}

/*
 * Flush data from buffer to socket
 */
int
ct_socket_flsbuf(ct_socket_t *sock, int all)
{
	struct sigaction act;
	ct_buf_t	*bp = &sock->buf;
	int		n, rc = 0;

	/* Ignore SIGPIPE while writing to socket */
	memset(&act, 0, sizeof(act));
	act.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &act, &act);

	do {
		if (!(n = ct_buf_avail(bp))) {
			sock->events = POLLIN;
			break;
		}
		n = write(sock->fd, ct_buf_head(bp), n);
		if (n < 0) {
			if (errno != EPIPE)
				ct_error("socket send error: %m");
			rc = IFD_ERROR_NOT_CONNECTED;
			break;
		}
		/* Advance head pointer */
		ct_buf_get(bp, NULL, n);
	} while (all);

	/* Restore old signal handler */
	sigaction(SIGPIPE, &act, &act);

	if (rc >= 0 && all == 2) {
		/* Shutdown socket for write */
		if (shutdown(sock->fd, 1) < 0) {
			ct_error("socket shutdown error: %m");
			return -1;
		}
	}

	return rc;
}

/*
 * Send/receive request
 */
int
ct_socket_send(ct_socket_t *sock, header_t *hdr, ct_buf_t *data)
{
	if (ct_socket_write(sock, hdr, sizeof(*hdr)) < 0
	 || ct_socket_write(sock, ct_buf_head(data), hdr->count) < 0)
		return -1;
	return 0;
}

int
ct_socket_recv(ct_socket_t *sock, header_t *hdr, ct_buf_t *resp)
{
	unsigned int	left, count, n;
	unsigned char	c;
	int		rc;

	if (ct_socket_write(sock, hdr, sizeof(*hdr)) < 0)
		return -1;

	if (hdr->count > 1024) {
		ct_error("oversize packet, discarding");
		ct_socket_close(sock);
		return -1;
	}

	/* Read the data following the packet header. If
	 * there's more data than the receive buffer can hold,
	 * truncate the packet.
	 * We return the number of bytes stored - the true packet
	 * length is available from the header
	 */
	left = hdr->count;
	count = 0;
	while (left) {
		n = min(left, ct_buf_tailroom(resp));
		if (n == 0) {
			rc = ct_socket_read(sock, &c, 1);
		} else {
			rc = ct_socket_read(sock, ct_buf_tail(resp), n);
		}
		if (rc < 0)
			return -1;
		count += n;
		left -= rc;
	}

	return count;
}

/*
 * Socket read/write routines
 */
int
ct_socket_write(ct_socket_t *sock, void *ptr, size_t len)
{
	struct sigaction act;
	unsigned int	count = 0;
	int		rc;

	if (sock->fd < 0)
		return -1;

	/* Ignore SIGPIPE while writing to socket */
	memset(&act, 0, sizeof(act));
	act.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &act, &act);

	while (count < len) {
		rc = write(sock->fd, ptr, len);
		if (rc < 0) {
			ct_error("send error: %m");
			goto done;
		}
		(caddr_t) ptr += rc;
		count += rc;
	}
	rc = count;

done:	/* Restore old signal handler */
	sigaction(SIGPIPE, &act, &act);
	return rc;
}

int
ct_socket_read(ct_socket_t *sock, void *ptr, size_t size)
{
	unsigned int	count = 0;
	int		rc;

	if (sock->fd < 0)
		return -1;

	while (count < size) {
		rc = read(sock->fd, ptr, size - count);
		if (rc < 0) {
			ct_error("recv error: %m");
			goto done;
		}
		if (rc == 0) {
			ct_error("peer closed connection");
			rc = -1;
			goto done;
		}
		(caddr_t) ptr += rc;
		count += rc;
	}
	rc = count;

done:	return rc;
}

/*
 * Link/unlink socket
 */
void
ct_socket_link(ct_socket_t *prev, ct_socket_t *sock)
{
	ct_socket_t	*next = prev->next;

	if (next)
		next->prev = sock;
	if (prev)
		prev->next = sock;
	sock->prev = prev;
	sock->next = next;
}

void
ct_socket_unlink(ct_socket_t *sock)
{
	ct_socket_t	*next = sock->next,
			*prev = sock->prev;

	if (next)
		next->prev = prev;
	if (prev)
		prev->next = next;
	sock->prev = sock->next = NULL;
}
