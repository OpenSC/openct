/*
 * Socket type definitions
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_SOCKET_H
#define OPENCT_SOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <openct/types.h>
#include <openct/buffer.h>

/* forward decl */
struct pollfd;

typedef struct header {
	uint32_t	xid;
	uint32_t	dest;
	int16_t		error;
	uint16_t	count;
} header_t;

typedef struct ct_socket {
	struct ct_socket *next, *prev;

	int		fd;
	int		eof;
	ct_buf_t	rbuf, sbuf;

	unsigned int	use_large_tags : 1,
			use_network_byte_order : 1,
			listener : 1;

	/* events to poll for */
	int		events;

	void *		user_data;
	int		(*poll)(struct ct_socket *, struct pollfd *);
	int		(*error)(struct ct_socket *);
	int		(*recv)(struct ct_socket *);
	int		(*send)(struct ct_socket *);
	int		(*process)(struct ct_socket *, header_t *,
					ct_buf_t *, ct_buf_t *);
	void		(*close)(struct ct_socket *);

	pid_t		client_id;
	uid_t		client_uid;
} ct_socket_t;

#define CT_SOCKET_BUFSIZ 4096

extern ct_socket_t *	ct_socket_new(unsigned int);
extern void		ct_socket_free(ct_socket_t *);
extern void		ct_socket_reuseaddr(int);
extern int		ct_socket_connect(ct_socket_t *, const char *);
extern int		ct_socket_listen(ct_socket_t *, const char *, int);
extern ct_socket_t *	ct_socket_accept(ct_socket_t *);
extern void		ct_socket_close(ct_socket_t *);
extern int		ct_socket_call(ct_socket_t *, ct_buf_t *, ct_buf_t *);
extern int		ct_socket_flsbuf(ct_socket_t *, int);
extern int		ct_socket_filbuf(ct_socket_t *, long);
extern int		ct_socket_put_packet(ct_socket_t *,
				header_t *, ct_buf_t *);
extern int		ct_socket_puts(ct_socket_t *, const char *);
extern int		ct_socket_get_packet(ct_socket_t *,
				header_t *, ct_buf_t *);
extern int		ct_socket_gets(ct_socket_t *, char *, size_t);
extern int		ct_socket_send(ct_socket_t *, header_t *,
				ct_buf_t *);
extern int		ct_socket_recv(ct_socket_t *, header_t *,
				ct_buf_t *);
extern int		ct_socket_write(ct_socket_t *, void *, size_t);
extern int		ct_socket_read(ct_socket_t *, void *, size_t);
extern void		ct_socket_link(ct_socket_t *, ct_socket_t *);
extern void		ct_socket_unlink(ct_socket_t *);
extern int		ct_socket_getpeername(ct_socket_t *, char *, size_t);

#ifdef __cplusplus
}
#endif

#endif /* OPENCT_SOCKET_H */
