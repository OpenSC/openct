/*
 * Logging functions
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_LOGGING_H
#define OPENCT_LOGGING_H

#ifdef __cplusplus
extern "C" {
#endif

extern void		ct_log_destination(const char *);

extern void		ct_error(const char *, ...);
extern void		ct_debug(const char *, ...);

extern const char *	ct_hexdump(const void *, size_t);

#ifdef __cplusplus
}
#endif

#endif /* OPENCT_LOGGING_H */
