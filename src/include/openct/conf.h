/*
 * Configuration stuff for IFD library
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_CONF_H
#define OPENCT_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

extern struct ct_config {
	int			debug;
	int			autoload;
	int			hotplug;
	int			suppress_errors;
	const char *		ifdhandler;
	const char *		modules_dir;
	const char *		driver_modules_dir;
	const char *		protocol_modules_dir;
	const char *		socket_dir;
} ct_config;

typedef struct ifd_conf_node {
	struct ifd_conf_node *next;
	struct ifd_conf_node *children;
	char *		name;
	char *		value;
} ifd_conf_node_t;

extern int	ifd_config_parse(const char *);
extern int	ifd_conf_get_string(const char *, char **);
extern int	ifd_conf_get_integer(const char *, unsigned int *);
extern int	ifd_conf_get_bool(const char *, unsigned int *);
extern int	ifd_conf_get_string_list(const char *, char **, size_t);
extern int	ifd_conf_get_nodes(const char *, ifd_conf_node_t **, size_t);
extern int	ifd_conf_node_get_string(ifd_conf_node_t *,
				const char *, char **);
extern int	ifd_conf_node_get_integer(ifd_conf_node_t *,
				const char *, unsigned int *);
extern int	ifd_conf_node_get_bool(ifd_conf_node_t *,
				const char *, unsigned int *);
extern int	ifd_conf_node_get_string_list(ifd_conf_node_t *,
				const char *, char **, size_t);
extern int	ifd_conf_node_get_nodes(ifd_conf_node_t *,
				const char *, ifd_conf_node_t **, size_t);

#ifdef __cplusplus
}
#endif

#endif /* OPENCT_CONF_H */
