/*
 * Buffer handling functions
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <string.h>
#include "internal.h"

void
ifd_buf_init(ifd_buf_t *bp, void *mem, size_t len)
{
	memset(bp, 0, sizeof(*bp));
	bp->base = (unsigned char *) mem;
	bp->size = len;
}

void
ifd_buf_set(ifd_buf_t *bp, void *mem, size_t len)
{
	ifd_buf_init(bp, mem, len);
	bp->tail = len;
}

void
ifd_buf_clear(ifd_buf_t *bp)
{
	bp->head = bp->tail = 0;
}

int
ifd_buf_get(ifd_buf_t *bp, void *mem, size_t len)
{
	if (len > bp->tail - bp->head)
		return -1;
	if (mem)
		memcpy(mem, bp->base + bp->head, len);
	bp->head += len;
	return len;
}

int
ifd_buf_put(ifd_buf_t *bp, const void *mem, size_t len)
{
	if (len > bp->size - bp->tail)
		return -1;
	if (mem)
		memcpy(bp->base + bp->tail, mem, len);
	bp->tail += len;
	return len;
}

unsigned int
ifd_buf_avail(ifd_buf_t *bp)
{
	return bp->tail - bp->head;
}

unsigned int
ifd_buf_tailroom(ifd_buf_t *bp)
{
	return bp->size - bp->tail;
}

void *
ifd_buf_head(ifd_buf_t *bp)
{
	return bp->base + bp->head;
}

void *
ifd_buf_tail(ifd_buf_t *bp)
{
	return bp->base + bp->tail;
}
