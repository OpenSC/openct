/*
 * Socket type definitions
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_SOCKET_H
#define OPENCT_SOCKET_H

#include <sys/types.h>
#include <openct/buffer.h>

typedef struct ct_socket {
	struct ct_socket *next, *prev;

	int		fd;
	int		eof;
	ct_buf_t	buf;

	/* events to poll for */
	int		events;

	void *		user_data;
	int		(*recv)(struct ct_socket *);
	int		(*send)(struct ct_socket *);
	void		(*close)(struct ct_socket *);

	pid_t		client_id;
	uid_t		client_uid;
} ct_socket_t;

typedef struct header {
	u_int32_t       xid;
	u_int32_t	dest;
	int16_t		error;
	u_int16_t       count;
} header_t;

extern ct_socket_t *	ct_socket_new(unsigned int);
extern void		ct_socket_free(ct_socket_t *);
extern int		ct_socket_connect(ct_socket_t *, const char *);
extern int		ct_socket_listen(ct_socket_t *, const char *, int);
extern ct_socket_t *	ct_socket_accept(ct_socket_t *);
extern void		ct_socket_close(ct_socket_t *);
extern int		ct_socket_call(ct_socket_t *, ct_buf_t *, ct_buf_t *);
extern int		ct_socket_flsbuf(ct_socket_t *, int);
extern int		ct_socket_filbuf(ct_socket_t *);
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

#endif /* OPENCT_SOCKET_H */
