/*
 * Locking functions - these are somewhat simplified
 * by the fact that we have one manager process per reader,
 * so we don't have to worry about different readers here,
 * just different slots.
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 *
 * FIXME - prevent denial of service from clients allocating
 * huge numbers of locks. There should be a maximum of one shared
 * and one exclusive lock per client.
 */

#include <stdlib.h>
#include <ifd/error.h>
#include <ifd/logging.h>
#include "internal.h"

typedef struct ct_lock {
	struct ct_lock *next;
	unsigned int	slot;
	uid_t		uid;
	ct_lock_handle	handle;
	ifd_socket_t *	owner;
	int		exclusive;
} ct_lock_t;

static ct_lock_t *	locks;
static unsigned int	lock_handle = 0;

/*
 * Try to establish a lock
 */
int
mgr_lock(ifd_socket_t *sock, int slot, int type, ct_lock_handle *res)
{
	ct_lock_t	*l;
	int		rc;

	/* See if we have a locking conflict */
	if ((rc = mgr_check_lock(sock, slot, type)) < 0)
		return rc;

	/* No conflict - grant lock and record this fact */
	l = (ct_lock_t *) calloc(1, sizeof(*l));
	l->exclusive = (type == IFD_LOCK_EXCLUSIVE);
	l->uid = sock->client_uid;
	l->handle = lock_handle++;
	l->owner = sock;
	l->slot = slot;

	l->next = locks;
	locks = l;

	ifd_debug("granted %s lock %u for slot %u by uid=%u",
			l->exclusive? "excl" : "shared",
			l->handle, l->slot, l->uid);

	*res = l->handle;
	return 0;
}

/*
 * Check if a slot is locked by someone else
 */
int
mgr_check_lock(ifd_socket_t *sock, int slot, int type)
{
	ct_lock_t	*l;

	for (l = locks; l; l = l->next) {
		if (l->slot != slot)
			continue;

		if (l->owner == sock)
			continue;

		if (l->exclusive
		 || type == IFD_LOCK_EXCLUSIVE
		 || l->uid != sock->client_uid)
			return IFD_ERROR_LOCKED;
	}

	return 0;
}

/*
 * Release a lock
 */
int
mgr_unlock(ifd_socket_t *sock, int slot, ct_lock_handle handle)
{
	ct_lock_t	*l, **lp;

	for (lp = &locks; (l = *lp) != NULL; lp = &l->next) {
		if (l->owner == sock && l->slot == slot
		 && l->handle == handle) {
			ifd_debug("released %s lock %u for slot %u by uid=%u",
					l->exclusive? "excl" : "shared",
					l->handle, l->slot, l->uid);

			*lp = l->next;
			free(l);
			return 0;
		}
	}

	return IFD_ERROR_NOLOCK;
}

/*
 * Release all locks held by a client
 * (called when the client socket is closed)
 */
void
mgr_unlock_all(ifd_socket_t *sock)
{
	ct_lock_t	*l, **lp;

	lp = &locks;
	while ((l = *lp) != NULL) {
		if (l->owner == sock) {
			ifd_debug("released %s lock %u for slot %u by uid=%u",
					l->exclusive? "excl" : "shared",
					l->handle, l->slot, l->uid);
			*lp = l->next;
			free(l);
		} else {
			lp = &l->next;
		}
	}
}
