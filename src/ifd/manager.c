/*
 * IFD manager
 *
 */

#include "internal.h"
#include <string.h>
#include <stdlib.h>

#define IFD_MAX_READERS		64

static ifd_reader_t *		ifd_readers[IFD_MAX_READERS];
static unsigned int		ifd_reader_handle = 1;

int
ifd_attach(ifd_reader_t *reader)
{
	unsigned int	slot;

	if (!reader)
		return -1;
	if (reader->num)
		return 0;

	for (slot = 1; slot < IFD_MAX_READERS; slot++) {
		if (!ifd_readers[slot])
			break;
	}

	if (slot >= IFD_MAX_READERS) {
		ifd_error("Too many readers");
		return -1;
	}

	reader->handle = ifd_reader_handle++;
	reader->num = slot;
	ifd_readers[slot] = reader;

	return 0;
}

ifd_reader_t *
ifd_reader_by_handle(unsigned int handle)
{
	ifd_reader_t *reader;
	unsigned int i;

	for (i = 0; i < IFD_MAX_READERS; i++) {
		if ((reader = ifd_readers[i])
		 && reader->handle == handle)
			return reader;
	}
	return NULL;
}


void
ifd_detach(ifd_reader_t *reader)
{
	unsigned int slot;

	if (reader->num == 0)
		return;

	if ((slot = reader->num) >= IFD_MAX_READERS
	 || ifd_readers[slot] != reader) {
		ifd_error("ifd_detach: unknown reader");
		return;
	}

	ifd_readers[slot] = NULL;
}
