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
static void	configure_reader(ifd_conf_node_t *cf);

int
ifd_init(void)
{
	unsigned int	ival;
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

	if (ifd_conf_get_integer("debug", &ival)
	 && ival > ct_config.debug)
		ct_config.debug = ival;

	/* Register all driver information (usu hotplug ids) */
	n = ifd_conf_get_nodes("driver", NULL, 0);
	if (n >= 0) {
		nodes = (ifd_conf_node_t **) calloc(n, sizeof(*nodes));
		n = ifd_conf_get_nodes("driver", nodes, n);
		for (i = 0; i < n; i++)
			configure_driver(nodes[i]);
		free(nodes);
	}

	/* Register all statically defined readers */
	n = ifd_conf_get_nodes("reader", NULL, 0);
	if (n >= 0) {
		nodes = (ifd_conf_node_t **) calloc(n, sizeof(*nodes));
		n = ifd_conf_get_nodes("reader", nodes, n);
		for (i = 0; i < n; i++)
			configure_reader(nodes[i]);
		free(nodes);
	}

	/* Initialize hotplugging */
	ifd_hotplug_init();

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

/*
 * Configure a reader using info from the config file
 */
void
configure_reader(ifd_conf_node_t *cf)
{
	ifd_reader_t	*reader;
	char		*device, *driver;

	if (ifd_conf_node_get_string(cf, "device", &device) < 0) {
		ct_error("no device specified in reader configuration");
		return;
	}

	if (ifd_conf_node_get_string(cf, "driver", &driver) < 0)
		driver = "auto";

	if (!(reader = ifd_open(driver, device))) {
		ct_error("failed to open %s reader (device=%s)",
				driver, device);
		return;
	}

	/* Handle additional parameters (e.g. serial settings)
	 * if the need arises */

	ifd_attach(reader);
}
