/**
 * @file
 * Generic driver functions.
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>

struct ifd_driver_info {
	struct ifd_driver_info *next;

	ifd_driver_t driver;

	unsigned int nids;
	ifd_devid_t *id;
};

static struct ifd_driver_info *list;

/*
 * Find registered driver by name
 */
static struct ifd_driver_info *find_by_name(const char *name, int create)
{
	struct ifd_driver_info *ip;

	for (ip = list; ip; ip = ip->next) {
		if (!strcmp(ip->driver.name, name))
			return ip;
	}

	if (!create)
		return NULL;

	ip = (struct ifd_driver_info *)calloc(1, sizeof(*ip));
	if (!ip) {
		ct_error("out of memory");
		return NULL;
	}
	ip->driver.name = strdup(name);
	ip->next = list;
	list = ip;

	return ip;
}

/**
 * Register a driver.
 *
 * @param name Driver name.
 * @param ops Driver operations.
 */
void ifd_driver_register(const char *name, struct ifd_driver_ops *ops)
{
	struct ifd_driver_info *ip;

	ip = find_by_name(name, 1);
	if (ip->driver.ops == NULL)
		ip->driver.ops = ops;
}

/**
 * Add a device ID to a driver.
 *
 * @param id Device ID.
 * @param name Driver name.
 *
 * Device which support plug-and-play can be mapped to drivers based on the
 * device ID. This function adds a device ID to a driver, so that the driver
 * can be looked-up at device detection time. The driver doesn't have to be
 * registered before calling this function.
 *
 * Device IDs start with the device type followed by a semi-colon and by a
 * device type specific ID. The following device types are supported:
 *
 * @li USB usb:vendor_id/device_id
 *
 * @return Error code <0 if failure.
 */
int ifd_driver_add_id(const char *id, const char *name)
{
	struct ifd_driver_info *ip;

	ifd_debug(3, "ifd_driver_add_id(%s, %s)", id, name);
	ip = find_by_name(name, 1);
	if (!ip)
		return -1;

	ip->id = (ifd_devid_t *) realloc(ip->id,
					 (ip->nids + 1) * sizeof(ifd_devid_t));
	if (!ip->id) {
		ct_error("out of memory");
		return IFD_ERROR_NO_MEMORY;
	}
	if (ifd_device_id_parse(id, &ip->id[ip->nids]) >= 0)
		ip->nids++;

	return 0;
}

/**
 * Get the driver name for a given device ID.
 *
 * @param id Device ID.
 *
 * @sa ifd_driver_add_id
 * @return Driver name or NULL if no driver is found.
 */
const char *ifd_driver_for_id(ifd_devid_t * id)
{
	struct ifd_driver_info *ip;
	unsigned int n;

	for (ip = list; ip; ip = ip->next) {
		for (n = 0; n < ip->nids; n++) {
			if (ifd_device_id_match(&ip->id[n], id))
				return ip->driver.name;
		}
	}

	return NULL;
}

/**
 * Lookup a driver by name.
 *
 * @param name Driver name.
 *
 * If the configuration parameter @a autoload is set, OpenCT will try to load
 * an external module for the requested driver.
 *
 * @return Pointer the the driver structure, or NULL if no driver is found.
 */
const ifd_driver_t *ifd_driver_get(const char *name)
{
	struct ifd_driver_info *ip;
	int retries = 2;

	while (retries--) {
		ip = find_by_name(name, ct_config.autoload);
		if (ip == NULL)
			break;
		if (ip->driver.ops != NULL)
			return &ip->driver;
		if (!ct_config.autoload || ifd_load_module("driver", name) < 0)
			break;
	}

	return NULL;
}

/**
 * Get a list of registered drivers.
 * 
 * @param names Name array.
 * @param max Size of the name array.
 *
 * This function fills the array pointed by @a names with pointers to the
 * driver names. At most @a max entries are returned. The names must @b not
 * be freed by the caller.
 *
 * @return Number of driver names.
 */
unsigned int ifd_drivers_list(const char **names, size_t max)
{
	struct ifd_driver_info *ip;
	unsigned int n;

	for (ip = list, n = 0; ip && n < max; ip = ip->next, n++) {
		names[n] = ip->driver.name;
	}
	return n;
}
