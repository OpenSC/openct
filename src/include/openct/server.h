/*
 * Server side functionality
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_SERVER_H
#define OPENCT_SERVER_H

#include <sys/poll.h>

typedef int	ct_poll_fn_t(struct pollfd *, unsigned int, void *);

extern void	ct_mainloop(ct_socket_t *, ct_poll_fn_t *, void *);
extern void	ct_mainloop_leave();

#endif /* OPENCT_SERVER_H */
