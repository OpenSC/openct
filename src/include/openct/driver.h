/*
 * Core functions of the IFD handler library
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef IFD_DRIVER_H
#define IFD_DRIVER_H

struct ifd_driver_ops {
	int		default_protocol;

	int		(*open)(ifd_reader_t *);
	int		(*close)(ifd_reader_t *);
	int		(*change_parity)(ifd_reader_t *, int);

	int		(*activate)(ifd_reader_t *);
	int		(*deactivate)(ifd_reader_t *);

	int		(*card_status)(ifd_reader_t *, int, int *);
	int		(*card_reset)(ifd_reader_t *, int, void *, size_t);

	int		(*set_protocol)(ifd_reader_t *, int, int);

	int		(*send)(ifd_reader_t *reader,
				unsigned int dad,
				const void *buffer,
				size_t len);
	int		(*recv)(ifd_reader_t *reader,
				unsigned int dad,
				void *buffer,
				size_t len,
				long timeout);
};

extern void		ifd_driver_register(const char *,
				struct ifd_driver_ops *);
extern const ifd_driver_t *ifd_driver_get(const char *);
extern const char *	ifd_driver_for_id(const char *);

#endif /* IFD_DRIVER_H */
