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
#define IFD_ERROR_INVALID_SLOT	-3
#define IFD_ERROR_NOT_SUPPORTED	-4
#define IFD_ERROR_COMM_ERROR	-5
#define IFD_ERROR_NO_CARD	-6
#define IFD_ERROR_LOCKED	-7
#define IFD_ERROR_NOLOCK	-8
#define IFD_ERROR_INVALID_ARG	-9

/* for application/resource manager protocol */
#define IFD_ERROR_INVALID_MSG	-100
#define IFD_ERROR_INVALID_CMD	-101
#define IFD_ERROR_MISSING_ARG	-102

extern const char *	ct_hexdump(const unsigned char *, size_t);

#endif /* IFD_ERROR_H */
