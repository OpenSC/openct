/*
 * Buffer handling functions of the IFD handler library
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef IFD_BUFFER_H
#define IFD_BUFFER_H

typedef struct ifd_buf {
	unsigned char *		base;
	unsigned int		head, tail, size;
} ifd_buf_t;

extern void		ifd_buf_init(ifd_buf_t *, void *, size_t);
extern void		ifd_buf_set(ifd_buf_t *, void *, size_t);
extern void		ifd_buf_clear(ifd_buf_t *);
extern int		ifd_buf_get(ifd_buf_t *, void *, size_t);
extern int		ifd_buf_put(ifd_buf_t *, const void *, size_t);
extern unsigned int	ifd_buf_avail(ifd_buf_t *);
extern unsigned int	ifd_buf_tailroom(ifd_buf_t *);
extern unsigned int	ifd_buf_size(ifd_buf_t *);
extern void *		ifd_buf_head(ifd_buf_t *);
extern void *		ifd_buf_tail(ifd_buf_t *);
extern int		ifd_buf_read(ifd_buf_t *, int);
extern void		ifd_buf_compact(ifd_buf_t *);


#endif /* IFD_BUFFER_H */
