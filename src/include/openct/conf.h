/*
 * Configuration stuff for IFD library
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef IFD_CONFIG_H
#define IFD_CONFIG_H

#define IFD_DEFAULT_MODULES_DIR		"/usr/lib/ifd"

extern struct ifd_config {
	int			autoload;
	int			debug;
	int			hush_errors;
	const char *		modules_dir;
	const char *		driver_modules_dir;
	const char *		protocol_modules_dir;
} ifd_config;

#endif /* IFD_CONFIG_H */
