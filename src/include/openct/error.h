/*
 * Error codes
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef IFD_ERROR_H
#define IFD_ERROR_H

#define IFD_SUCCESS		0
#define IFD_ERROR_GENERIC	-1
#define IFD_ERROR_TIMEOUT	-2

extern const char *	ifd_hexdump(const unsigned char *, size_t);

#endif /* IFD_ERROR_H */
