/*
 * Hotplug support
 *
 * These functions handle hotplug attach/detach events.
 * A hotplug event includes an ID and a device file name
 * 
 * The format of the ID is
 * 	usb:id=vendor/product
 * 	pcmcia:id=vendor/product
 */

#include <stdlib.h>
#include "internal.h"

int
ifd_hotplug_init(void)
{
	unsigned int	enable;

	ifd_debug(1, "called");
	if (ifd_conf_get_bool("hotplug", &enable) >= 0)
		ct_config.hotplug = enable;

	if (ct_config.hotplug)
		ifd_sysdep_usb_scan();
	return 0;
}

ifd_reader_t *
ifd_hotplug_attach(const char *device, const char *id)
{
	const char	*driver;
	ifd_reader_t	*reader;

	if (!(driver = ifd_driver_for_id(id)))
		return NULL;

	if (!(reader = ifd_open(driver, device)))
		return NULL;

	if (ifd_attach(reader) < 0) {
		ifd_close(reader);
		return NULL;
	}

	reader->flags |= IFD_READER_HOTPLUG;
	return reader;
}

int
ifd_hotplug_detach(const char *device, const char *id)
{
	ct_error("hotplug detach not yet implemented");
	return -1;
}
