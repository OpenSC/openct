/*
 * Generic driver functions
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <string.h>
#include "internal.h"

struct ifd_driver_info {
	struct ifd_driver_info *next;

	ifd_driver_t		driver;

	unsigned int		nids;
	char **			id;
};

static struct ifd_driver_info *	list;

/*
 * Find registered driver by name
 */
static struct ifd_driver_info *
find_by_name(const char *name, int create)
{
	struct ifd_driver_info *ip;

	for (ip = list; ip; ip = ip->next) {
		if (!strcmp(ip->driver.name, name))
			return ip;
	}

	if (!create)
		return NULL;

	ip = (struct ifd_driver_info *) calloc(1, sizeof(*ip));
	ip->driver.name = strdup(name);
	ip->next = list;
	list = ip;

	return ip;
}

/*
 * Register a driver
 */
void
ifd_driver_register(const char *name, struct ifd_driver_ops *ops)
{
	struct ifd_driver_info *ip;

	ip = find_by_name(name, 1);
	if (ip->driver.ops == NULL)
		ip->driver.ops = ops;
}

void
ifd_driver_add_id(const char *id, const char *name)
{
	struct ifd_driver_info *ip;

	if (ifd_config.debug > 2)
		ifd_debug("ifd_driver_add_id(%s, %s)", id, name);
	ip = find_by_name(name, 1);
	ip->nids++;
	ip->id = (char **) realloc(ip->id, ip->nids * sizeof(char *));
	ip->id[ip->nids - 1] = strdup(id);
}

const char *
ifd_driver_for_id(const char *id)
{
	struct ifd_driver_info *ip;
	unsigned int n;

	for (ip = list; ip; ip = ip->next) {
		for (n = 0; n < ip->nids; n++) {
			if (!strcmp(ip->id[n], id))
				return ip->driver.name;
		}
	}

	return NULL;
}

const ifd_driver_t *
ifd_driver_get(const char *name)
{
	struct ifd_driver_info *ip;
	int	retries = 2;

	while (retries--) {
		ip = find_by_name(name, ifd_config.autoload);
		if (ip == NULL)
			break;
		if (ip->driver.ops != NULL)
			return &ip->driver;
		if (!ifd_config.autoload
		 || ifd_load_module("driver", name) < 0)
			break;
	}

	return NULL;
}
