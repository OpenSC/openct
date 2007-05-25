/*
 * IFD manager
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <string.h>
#include <stdlib.h>

static ifd_reader_t *ifd_readers[OPENCT_MAX_READERS];
static unsigned int ifd_reader_handle = 1;

/*
 * Return number of readers available
 */
int ifd_reader_count(void)
{
	return OPENCT_MAX_READERS;
}

/*
 * Register a reader
 */
int ifd_attach(ifd_reader_t * reader)
{
	unsigned int slot;

	if (!reader)
		return -1;
	if (reader->num)
		return 0;

	for (slot = 0; slot < OPENCT_MAX_READERS; slot++) {
		if (!ifd_readers[slot])
			break;
	}

	if (slot >= OPENCT_MAX_READERS) {
		ct_error("Too many readers");
		return -1;
	}

	reader->handle = ifd_reader_handle++;
	reader->num = slot;
	ifd_readers[slot] = reader;

	return 0;
}

/*
 * Functions to look up registered readers
 */
ifd_reader_t *ifd_reader_by_handle(unsigned int handle)
{
	ifd_reader_t *reader;
	unsigned int i;

	for (i = 0; i < OPENCT_MAX_READERS; i++) {
		if ((reader = ifd_readers[i])
		    && reader->handle == handle)
			return reader;
	}
	return NULL;
}

ifd_reader_t *ifd_reader_by_index(unsigned int idx)
{
	ifd_reader_t *reader;

	if (idx >= OPENCT_MAX_READERS) {
		ct_error("ifd_reader_by_index: invalid index %u", idx);
		return NULL;
	}
	if (!(reader = ifd_readers[idx]))
		return NULL;

	return reader;
}

/*
 * Unregister a reader
 */
void ifd_detach(ifd_reader_t * reader)
{
	unsigned int slot;

	if (reader->num == 0)
		return;

	if ((slot = reader->num) >= OPENCT_MAX_READERS
	    || ifd_readers[slot] != reader) {
		ct_error("ifd_detach: unknown reader");
		return;
	}

	ifd_readers[slot] = NULL;
}
