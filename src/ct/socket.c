/*
 * Socket handling routines
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
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
#include <arpa/inet.h>
#include <netdb.h>
#include <limits.h>

#include <openct/logging.h>
#include <openct/socket.h>
#include <openct/path.h>
#include <openct/error.h>

static unsigned int ifd_xid = 1;
static int ifd_reuse_addr = 0;

static inline unsigned int min(unsigned int a, unsigned int b)
{
	return (a < b) ? a : b;
}

static int ct_socket_default_recv_cb(ct_socket_t *);
static int ct_socket_default_send_cb(ct_socket_t *);
static int ct_socket_getcreds(ct_socket_t *);

/*
 * Create a socket object
 */
ct_socket_t *ct_socket_new(unsigned int bufsize)
{
	ct_socket_t *sock;
	unsigned char *p;

	sock = (ct_socket_t *) calloc(1, sizeof(*sock) + 2 * bufsize);
	if (sock == NULL)
		return NULL;

	/* Initialize socket buffer */
	p = (unsigned char *)(sock + 1);
	ct_buf_init(&sock->rbuf, p, bufsize);
	ct_buf_init(&sock->sbuf, p + bufsize, bufsize);
	sock->recv = ct_socket_default_recv_cb;
	sock->send = ct_socket_default_send_cb;
	sock->fd = -1;

	return sock;
}

/*
 * Free a socket object
 */
void ct_socket_free(ct_socket_t * sock)
{
	ct_socket_unlink(sock);
	if (sock->close)
		sock->close(sock);
	ct_socket_close(sock);
	free(sock);
}

void ct_socket_reuseaddr(int n)
{
	ifd_reuse_addr = n;
}

/*
 * Make the socket.
 * This code tries to deal with IPv4/IPv6 and AF_UNIX sockets
 */
enum { CT_MAKESOCK_BIND, CT_MAKESOCK_CONNECT };

static int __ct_socket_make(ct_socket_t * sock, int op,
			    const struct sockaddr *sa, socklen_t salen)
{
	int fd, oerrno;

	if ((fd = socket(sa->sa_family, SOCK_STREAM, 0)) < 0)
		return -1;

	/* For non-local sockets, use network byte order */
	if (sa->sa_family != AF_UNIX)
		sock->use_network_byte_order = 1;

	/* set close on exec */
	if (fcntl(fd, F_SETFD, 1) < 0)
		goto failed;

	switch (op) {
	case CT_MAKESOCK_BIND:
#ifdef IPV6_V6ONLY
# ifndef SOL_IPV6
#  define SOL_IPV6 IPPROTO_IPV6
# endif
		if (sa->sa_family == AF_INET6) {
			int val = 1;

			setsockopt(fd, SOL_IPV6, IPV6_V6ONLY,
				   &val, sizeof(val));
		}
#endif
		if (ifd_reuse_addr) {
			int val = 1;

			setsockopt(fd, SOL_SOCKET,
				   SO_REUSEADDR, &val, sizeof(val));
		}
		if (bind(fd, sa, salen) >= 0) {
			sock->fd = fd;
			return fd;
		}
		ct_debug("bind() failed: %m");
		break;
	case CT_MAKESOCK_CONNECT:
		if (connect(fd, sa, salen) >= 0) {
			sock->fd = fd;
			return fd;
		}
		/* no error message - reader does not exist. */
		break;
	default:
		errno = EINVAL;
	}

      failed:
	oerrno = errno;
	close(fd);

	/* XXX translate error */
	return -1;
}

static int ct_socket_make(ct_socket_t * sock, int op, const char *addr)
{
	union {
		struct sockaddr a;
		struct sockaddr_in in;
		struct sockaddr_in6 ix;
		struct sockaddr_un un;
	} s;
	struct addrinfo *res, *ai;
	char addrbuf[1024], *port;
	unsigned int portnum = 6666;
	int fd;

	memset(&s, 0, sizeof(s));

	/* Simple stuff first - unix domain sockets */
	if (addr[0] == '/') {
		s.un.sun_family = AF_UNIX;
		strncpy(s.un.sun_path, addr, sizeof(s.un.sun_path));
		if (op == CT_MAKESOCK_BIND) {
			if (unlink(addr) < 0 && errno != ENOENT)
				return -1;
		}
		return __ct_socket_make(sock, op, &s.a, sizeof(s.un));
	}

	memset(addrbuf, 0, sizeof(addrbuf));
	strncpy(addrbuf, addr, sizeof(addrbuf) - 1);

	addr = addrbuf;
	if ((port = strchr(addrbuf, ';')) != NULL) {
		*port++ = '\0';
	} else
	    if ((port = strchr(addrbuf, ':')) != NULL
		&& (strchr(port + 1, ':')) == NULL) {
		*port++ = '\0';
	}

	if (*addr == '\0')
		addr = "0.0.0.0";

	if (port) {
		portnum = strtoul(port, &port, 10);
		if (*port)
			return -1;
		portnum = htons(portnum);
	}

	if (inet_pton(AF_INET, addr, &s.in.sin_addr) > 0) {
		s.in.sin_family = AF_INET;
		s.in.sin_port = portnum;
		return __ct_socket_make(sock, op, &s.a, sizeof(s.in));
	}
	if (inet_pton(AF_INET6, addr, &s.ix.sin6_addr) > 0) {
		s.ix.sin6_family = AF_INET6;
		s.ix.sin6_port = portnum;
		return __ct_socket_make(sock, op, &s.a, sizeof(s.ix));
	}

	if (getaddrinfo(addr, NULL, NULL, &res) < 0)
		return -1;

	fd = -1;
	for (ai = res; ai; ai = ai->ai_next) {
		if (ai->ai_family == AF_INET)
			((struct sockaddr_in *)ai->ai_addr)->sin_port = portnum;
		else if (ai->ai_family == AF_INET6)
			((struct sockaddr_in6 *)ai->ai_addr)->sin6_port =
			    portnum;
		fd = __ct_socket_make(sock, op, ai->ai_addr, ai->ai_addrlen);
		if (fd >= 0)
			break;
	}

	freeaddrinfo(res);
	return fd;
}

/*
 * Create a client socket
 */
int ct_socket_connect(ct_socket_t * sock, const char *addr)
{
	ct_socket_close(sock);
	if (ct_socket_make(sock, CT_MAKESOCK_CONNECT, addr) < 0)
		return -1;

	return 0;
}

/*
 * Listen on a socket
 */
int ct_socket_listen(ct_socket_t * sock, const char *path, int mode)
{
	ct_socket_close(sock);
	if (ct_socket_make(sock, CT_MAKESOCK_BIND, path) < 0)
		return -1;

	if (listen(sock->fd, 5) < 0) {
		ct_socket_close(sock);
		return -1;
	}
	if (path[0] == '/')
		chmod(path, mode);

	sock->listener = 1;
	sock->events = POLLIN;
	return 0;
}

/*
 * Accept incoming connection
 */
ct_socket_t *ct_socket_accept(ct_socket_t * sock)
{
	ct_socket_t *svc;
	int fd;

	if (!(svc = ct_socket_new(CT_SOCKET_BUFSIZ)))
		return NULL;

	if ((fd = accept(sock->fd, NULL, NULL)) < 0) {
		ct_socket_free(svc);
		return NULL;;
	}

	svc->use_network_byte_order = sock->use_network_byte_order;
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
static int ct_socket_getcreds(ct_socket_t * sock)
{
#ifdef SO_PEERCRED
	struct ucred creds;
	socklen_t len;

	len = sizeof(creds);
	if (getsockopt(sock->fd, SOL_SOCKET, SO_PEERCRED, &creds, &len) < 0)
		return -1;
	sock->client_uid = creds.uid;
#endif
	return 0;
}

/*
 * Get the peer name
 */
int ct_socket_getpeername(ct_socket_t * sock, char *buf, size_t len)
{
	struct sockaddr_storage ss;
	socklen_t slen = sizeof(struct sockaddr_storage);

	getpeername(sock->fd, (struct sockaddr *)&ss, &slen);
	switch (ss.ss_family) {
	case AF_INET:
		inet_ntop(AF_INET,
			  &((struct sockaddr_in *)&ss)->sin_addr, buf, len);
		break;
	case AF_INET6:
		inet_ntop(AF_INET6,
			  &((struct sockaddr_in6 *)&ss)->sin6_addr, buf, len);
		break;
	case AF_UNIX:
		snprintf(buf, len, "<local process>");
		break;
	default:
		ct_error("Unsupported address family\n");
		return -1;
	}

	return 0;
}

/*
 * Close socket
 */
void ct_socket_close(ct_socket_t * sock)
{
	ct_buf_clear(&sock->rbuf);
	ct_buf_clear(&sock->sbuf);
	if (sock->fd >= 0)
		close(sock->fd);
	sock->fd = -1;
}

/*
 * Transmit a call and receive the response
 */
int ct_socket_call(ct_socket_t * sock, ct_buf_t * args, ct_buf_t * resp)
{
	ct_buf_t data;
	unsigned int xid, avail;
	header_t header;
	int rc;

	/* Compact send buffer */
	ct_buf_compact(&sock->sbuf);

	if ((xid = ifd_xid++) == 0)
		xid = ifd_xid++;

	/* Build header - note there's no need to convert
	 * integers to network byte order: everything happens
	 * on the same host, so there's no byte order issue */
	header.xid = xid;
	header.count = ct_buf_avail(args);
	header.dest = 0;
	header.error = 0;

	/* Put everything into send buffer and transmit */
	if ((rc = ct_socket_put_packet(sock, &header, args)) < 0
	    || (rc = ct_socket_flsbuf(sock, 1)) < 0)
		return rc;

	/* Return right now if we don't expect a response */
	if (resp == NULL)
		return 0;

	/* Loop until we receive a complete packet with the
	 * right xid in it */
	rc = 0;
	do {
		if ((rc == 0) && (rc = ct_socket_filbuf(sock, -1)) < 0)
			return -1;

		ct_buf_clear(resp);
		if ((rc = ct_socket_get_packet(sock, &header, &data)) < 0)
			return rc;
	} while (rc == 0 || header.xid != xid);

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
int ct_socket_put_packet(ct_socket_t * sock, header_t * hdr, ct_buf_t * data)
{
	header_t hcopy;
	ct_buf_t *bp = &sock->sbuf;
	size_t count;
	int rc;

	count = sizeof(*hdr) + (data ? ct_buf_avail(data) : 0);
	if (ct_buf_tailroom(bp) < count) {
		if ((rc = ct_socket_flsbuf(sock, 1)) < 0)
			return rc;
		ct_buf_compact(bp);
		if (ct_buf_tailroom(bp) < count) {
			ct_error("packet too large for buffer");
			return IFD_ERROR_BUFFER_TOO_SMALL;
		}
	}

	hdr->count = data ? ct_buf_avail(data) : 0;

	hcopy = *hdr;
	if (sock->use_network_byte_order) {
		hcopy.error = ntohs(hcopy.error);
		hcopy.count = ntohs(hcopy.count);
	}
	ct_buf_put(bp, &hcopy, sizeof(hcopy));

	if (hdr->count)
		ct_buf_put(bp, ct_buf_head(data), hdr->count);

	sock->events = POLLOUT;
	return 0;
}

int ct_socket_puts(ct_socket_t * sock, const char *string)
{
	ct_buf_t *bp = &sock->sbuf;

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
int ct_socket_get_packet(ct_socket_t * sock, header_t * hdr, ct_buf_t * data)
{
	ct_buf_t *bp = &sock->rbuf;
	unsigned int avail;
	header_t th;

	avail = ct_buf_avail(bp);
	if (avail < sizeof(header_t))
		return 0;

	memcpy(&th, ct_buf_head(bp), sizeof(th));
	if (sock->use_network_byte_order) {
		th.count = ntohs(th.count);
		th.error = ntohs(th.error);
	}

	if (avail >= sizeof(header_t) + th.count) {
		/* There's enough data in the buffer
		 * Extract header... */
		ct_buf_get(bp, NULL, sizeof(*hdr));
		*hdr = th;

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

int ct_socket_gets(ct_socket_t * sock, char *buffer, size_t size)
{
	return ct_buf_gets(&sock->rbuf, buffer, size);
}

/*
 * Read some data from socket and put it into buffer
 */
int ct_socket_filbuf(ct_socket_t * sock, long timeout)
{
	ct_buf_t *bp = &sock->rbuf;
	unsigned int count;
	int n;

	if (!(count = ct_buf_tailroom(bp))) {
		ct_buf_compact(bp);
		if (!(count = ct_buf_tailroom(bp))) {
			ct_error("packet too large");
			return -1;
		}
	}

	if (timeout >= 0) {
		struct pollfd pfd;

		pfd.fd = sock->fd;
		pfd.events = POLLIN;
		do {
			n = poll(&pfd, 1, timeout);
		} while (n < 0 && errno == EINTR);
		if (n == 0)
			return IFD_ERROR_TIMEOUT;
	}

      retry:
	n = read(sock->fd, ct_buf_tail(bp), count);
	if (n < 0 && errno == EINTR)
		goto retry;

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
int ct_socket_flsbuf(ct_socket_t * sock, int all)
{
	struct sigaction act;
	ct_buf_t *bp = &sock->sbuf;
	int n, rc = 0;

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
 * Default send/receive handlers
 */
static int ct_socket_default_recv_cb(ct_socket_t * sock)
{
	char buffer[CT_SOCKET_BUFSIZ + 64];
	header_t header;
	ct_buf_t args, resp;
	int rc;

	/* Error or client closed connection? */
	if ((rc = ct_socket_filbuf(sock, -1)) <= 0)
		return -1;

	while (ct_buf_avail(&sock->rbuf)) {
		/* If request is incomplete, go back
		 * and wait for more
		 * XXX add timeout? */
		rc = ct_socket_get_packet(sock, &header, &args);
		if (rc <= 0)
			return 0;

		ct_buf_init(&resp, buffer, sizeof(buffer));

		if (sock->process == 0)
			continue;

		/* Process the request */
		rc = sock->process(sock, &header, &args, &resp);

		/* Do not reply if the request was dropped */
		if (header.xid == 0)
			continue;

		if (rc >= 0) {
			header.error = 0;
		} else {
			/* Do not return an error to a reply */
			if (header.dest)
				continue;
			ct_buf_clear(&resp);
		}

		/* Now mark as reply */
		header.dest = 1;

		/* Put packet into transmit buffer */
		if ((rc = ct_socket_put_packet(sock, &header, &resp)) < 0)
			return rc;
	}

	return 0;
}

static int ct_socket_default_send_cb(ct_socket_t * sock)
{
	return ct_socket_flsbuf(sock, 0);
}

/*
 * Send/receive request
 */
int ct_socket_send(ct_socket_t * sock, header_t * hdr, ct_buf_t * data)
{
	header_t hcopy = *hdr;

	if (sock->use_network_byte_order) {
		hcopy.error = htons(hcopy.error);
		hcopy.count = htons(hcopy.count);
	}
	if (ct_socket_write(sock, &hcopy, sizeof(hcopy)) < 0
	    || ct_socket_write(sock, ct_buf_head(data), hdr->count) < 0)
		return -1;
	return 0;
}

int ct_socket_recv(ct_socket_t * sock, header_t * hdr, ct_buf_t * resp)
{
	header_t hcopy = *hdr;
	unsigned int left, count, n;
	unsigned char c;
	int rc;

	if (sock->use_network_byte_order) {
		hcopy.error = htons(hcopy.error);
		hcopy.count = htons(hcopy.count);
	}
	if (ct_socket_write(sock, &hcopy, sizeof(hcopy)) < 0)
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
int ct_socket_write(ct_socket_t * sock, void *ptr, size_t len)
{
	struct sigaction act;
	unsigned int count = 0;
	int rc;
	caddr_t p = (caddr_t) ptr;

	if (sock->fd < 0)
		return -1;

	/* Ignore SIGPIPE while writing to socket */
	memset(&act, 0, sizeof(act));
	act.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &act, &act);

	while (count < len) {
		rc = write(sock->fd, (const void *)p, len);
		if (rc < 0) {
			ct_error("send error: %m");
			goto done;
		}
		p += rc;
		count += rc;
	}
	rc = count;

      done:			/* Restore old signal handler */
	sigaction(SIGPIPE, &act, &act);
	return rc;
}

int ct_socket_read(ct_socket_t * sock, void *ptr, size_t size)
{
	unsigned int count = 0;
	int rc;
	caddr_t p = (caddr_t) ptr;

	if (sock->fd < 0)
		return -1;

	while (count < size) {
		rc = read(sock->fd, (void *)p, size - count);
		if (rc < 0) {
			ct_error("recv error: %m");
			goto done;
		}
		if (rc == 0) {
			ct_error("peer closed connection");
			rc = -1;
			goto done;
		}
		p += rc;
		count += rc;
	}
	rc = count;

      done:return rc;
}

/*
 * Link/unlink socket
 */
void ct_socket_link(ct_socket_t * prev, ct_socket_t * sock)
{
	ct_socket_t *next;

	if (!prev)
		return;

	next = prev->next;

	if (next)
		next->prev = sock;

	prev->next = sock;
	sock->prev = prev;
	sock->next = next;
}

void ct_socket_unlink(ct_socket_t * sock)
{
	ct_socket_t *next = sock->next, *prev = sock->prev;

	if (next)
		next->prev = prev;
	if (prev)
		prev->next = next;
	sock->prev = sock->next = NULL;
}
