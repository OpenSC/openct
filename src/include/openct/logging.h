/*
 * Logging functions of the IFD handler library
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef IFD_LOGGING_H
#define IFD_LOGGING_H

extern void		ct_error(const char *, ...);
extern const char *	ct_strerror(int);
extern void		ct_debug(const char *, ...);

#endif /* IFD_LOGGING_H */
