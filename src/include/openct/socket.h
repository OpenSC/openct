/*
 * Socket type definitions
 *
 */

#ifndef IFDD_SOCKET_H
#define IFDD_SOCKET_H

#include <sys/types.h>
#include <ifd/buffer.h>

typedef struct ifd_socket {
	struct ifd_socket *next, *prev;

	int		fd;
	ifd_buf_t	buf;

	/* events to poll for */
	int		events;

	void *		user_data;
	int		(*recv)(struct ifd_socket *);
	int		(*send)(struct ifd_socket *);

	pid_t		client_id;
	uid_t		client_uid;
} ifd_socket_t;

typedef struct header {
	u_int32_t       xid;
	u_int32_t	dest;
	u_int16_t	error;
	u_int16_t       count;
} header_t;

extern ifd_socket_t *	ifd_socket_new(unsigned int);
extern void		ifd_socket_free(ifd_socket_t *);
extern int		ifd_socket_connect(ifd_socket_t *, const char *);
extern int		ifd_socket_listen(ifd_socket_t *, const char *);
extern ifd_socket_t *	ifd_socket_accept(ifd_socket_t *);
extern void		ifd_socket_close(ifd_socket_t *);
extern int		ifd_socket_call(ifd_socket_t *, unsigned int,
				ifd_buf_t *, ifd_buf_t *);
extern int		ifd_socket_flsbuf(ifd_socket_t *, int);
extern int		ifd_socket_filbuf(ifd_socket_t *);
extern int		ifd_socket_put_packet(ifd_socket_t *,
				header_t *, ifd_buf_t *);
extern int		ifd_socket_get_packet(ifd_socket_t *,
				header_t *, ifd_buf_t *);
extern int		ifd_socket_send(ifd_socket_t *, header_t *,
				ifd_buf_t *);
extern int		ifd_socket_recv(ifd_socket_t *, header_t *,
				ifd_buf_t *);
extern int		ifd_socket_write(ifd_socket_t *, void *, size_t);
extern int		ifd_socket_write_nb(ifd_socket_t *, void *, size_t);
extern int		ifd_socket_read(ifd_socket_t *, void *, size_t);
extern int		ifd_socket_read_nb(ifd_socket_t *, void *, size_t);

#endif /* IFDD_SOCKET_H */
