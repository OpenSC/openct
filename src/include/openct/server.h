/*
 * Server side functionality
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_SERVER_H
#define OPENCT_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/poll.h>

extern void	ct_mainloop_add_socket(ct_socket_t *);
extern void	ct_mainloop(void);
extern void	ct_mainloop_leave(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENCT_SERVER_H */
