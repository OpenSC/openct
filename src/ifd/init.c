/*
 * Initialize the library
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include "internal.h"

extern void	ifd_etoken_register(void);
extern void	ifd_kaan_register(void);
extern void	ifd_towitoko_register(void);

static void	configure_driver(ifd_conf_node_t *cf);

int
ifd_init(void)
{
	unsigned int	ival;
	char		*sval;
	ifd_conf_node_t	**nodes;
	int		i, n;

	/* Register built-in drivers */
	ifd_etoken_register();
	ifd_kaan_register();
	ifd_towitoko_register();

	/* Register all builtin protocols */
	ifd_protocol_register(&ifd_protocol_t0);
	ifd_protocol_register(&ifd_protocol_t1);
	ifd_protocol_register(&ifd_protocol_trans);

	if (ifd_conf_get_integer("debug", &ival) >= 0
	 && ival > ct_config.debug)
		ct_config.debug = ival;

	if (ifd_conf_get_string("ifdhandler", &sval) >= 0)
		ct_config.ifdhandler = sval;

	/* Register all driver information (usu hotplug ids) */
	n = ifd_conf_get_nodes("driver", NULL, 0);
	if (n >= 0) {
		nodes = (ifd_conf_node_t **) calloc(n, sizeof(*nodes));
		n = ifd_conf_get_nodes("driver", nodes, n);
		for (i = 0; i < n; i++)
			configure_driver(nodes[i]);
		free(nodes);
	}

	/* Initialize hotplugging */
	//ifd_hotplug_init();

	return 0;
}

/*
 * Configure a reader driver
 */
void
configure_driver(ifd_conf_node_t *cf)
{
	const char	*driver;
	char		**ids;
	int		j, n;

	if (!(driver = cf->value))
		return;
	if ((n = ifd_conf_node_get_string_list(cf, "ids", NULL, 0)) >= 0) {
		ids = (char **) calloc(n, sizeof(char *));
		n = ifd_conf_node_get_string_list(cf, "ids", ids, n);
		for (j = 0; j < n; j++)
			ifd_driver_add_id(ids[j], driver);
		free(ids);
	}
}
