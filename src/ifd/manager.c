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

static ifd_reader_t *
ifd_add_reader(ifd_device_t *dev, const char *driver_name)
{
	ifd_reader_t	*reader;
	ifd_driver_t	*driver;
	unsigned int	slot;

	for (slot = 0; slot < IFD_MAX_READERS; slot++) {
		if (!ifd_readers[slot])
			break;
	}

	if (slot >= IFD_MAX_READERS) {
		ifd_error("Too many readers");
		return NULL;
	}

	if (!driver_name || !strcmp(driver_name, "auto")) {
		char	name[64];

		if (ifd_device_identify(dev, name, sizeof(name)) < 0) {
			ifd_error("%s: unable to identify device, "
			      "please specify driver",
			      dev->name);
			return NULL;
		}
		driver = ifd_get_driver(name);
		if (driver == NULL) {
			ifd_error("%s: driver not available (identified as %s)",
				       	dev->name, name);
			return NULL;
		}
	} else {
		driver = ifd_get_driver(driver_name);
		if (driver == NULL) {
			ifd_error("%s: driver not available", driver_name);
			return NULL;
		}
	}

	reader = (ifd_reader_t *) calloc(1, sizeof(*reader));
	reader->handle = ifd_reader_handle++;
	reader->num = slot;
	reader->device = dev;
	reader->driver = driver;

	if (ifd_set_protocol(reader, IFD_PROTOCOL_DEFAULT) < 0) {
		free(reader);
		return NULL;
	}

	ifd_readers[slot] = reader;
	return reader;
}

static ifd_reader_t *
ifd_attach(ifd_device_t *device, const char *device_name, const char *driver_name)
{
	ifd_reader_t *reader;

	if (device == NULL) {
		ifd_error("Unable to open %s: %m", device_name);
		return NULL;
	}

	reader = ifd_add_reader(device, driver_name);
	if (reader == NULL) {
		ifd_device_close(device);
		return NULL;
	}

	return reader;
}

ifd_reader_t *
ifd_attach_serial(const char *device_name, const char *driver_name)
{
	return ifd_attach(ifd_open_serial(device_name),
				device_name, driver_name);
}

ifd_reader_t *
ifd_attach_usb(const char *device_name, const char *driver_name)
{
	return ifd_attach(ifd_open_usb(device_name),
				device_name, driver_name);
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


int
ifd_detach(ifd_reader_t *reader)
{
	unsigned int slot;

	if ((slot = reader->num) >= IFD_MAX_READERS
	 || ifd_readers[slot] != reader) {
		ifd_error("ifd_detach: unknown reader");
		return -1;
	}

	ifd_readers[slot] = NULL;
	ifd_device_close(reader->device);

	memset(reader, 0, sizeof(*reader));
	free(reader);
	return 0;
}
