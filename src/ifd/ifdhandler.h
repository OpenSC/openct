/*
 * Internal ifdhandler functions
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#ifndef IFD_IFDHANDLER_H
#define IFD_IFDHANDLER_H

#include <openct/buffer.h>
#include <openct/socket.h>
#include <openct/ifd.h>

extern int ifdhandler_process(ct_socket_t *, ifd_reader_t *,
			      ct_buf_t *, ct_buf_t *);
extern int ifdhandler_lock(ct_socket_t *, int, int, ct_lock_handle *);
extern int ifdhandler_check_lock(ct_socket_t *, int, int);
extern int ifdhandler_unlock(ct_socket_t *, int, ct_lock_handle);
extern void ifdhandler_unlock_all(ct_socket_t *);

#endif				/* IFD_IFDHANDLER_H */
