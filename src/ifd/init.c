/*
 * Initialize the library
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <stdlib.h>
#include <ltdl.h>

static int configure_driver(ifd_conf_node_t * cf);

int ifd_init(void)
{
	unsigned int ival;
	char *sval;
	ifd_conf_node_t **nodes;
	int i, n;

	/* initialize ltdl */
	i = lt_dlinit();
	if (i != 0)
		ct_error("lt_dlinit returned %d", i);

	/* Register built-in drivers */
	ifd_acr30u_register();
	ifd_cardman_register();
	ifd_cm4000_register();
	ifd_egate_register();
	ifd_epass3k_register();
	ifd_etoken_register();
	ifd_etoken64_register();
	ifd_eutron_register();
	ifd_gempc_register();
	ifd_ikey2k_register();
	ifd_ikey3k_register();
	ifd_kaan_register();
	ifd_pertosmart_ac1030_register();
	ifd_pertosmart_ac1038_register();
	ifd_smartboard_register();
	ifd_smph_register();
	ifd_starkey_register();
	ifd_towitoko_register();
	ifd_rutoken_register();
	/* ifd_wbeiuu_register();	driver not working yet */
	ifd_cyberjack_register();	
	/* ccid last */
	ifd_ccid_register();

	/* Register all builtin protocols */
	ifd_protocol_register(&ifd_protocol_t0);
	ifd_protocol_register(&ifd_protocol_t1);
	ifd_protocol_register(&ifd_protocol_gbp);
	ifd_protocol_register(&ifd_protocol_trans);
	ifd_protocol_register(&ifd_protocol_i2c_short);
	ifd_protocol_register(&ifd_protocol_i2c_long);
	ifd_protocol_register(&ifd_protocol_2wire);
	ifd_protocol_register(&ifd_protocol_3wire);
	ifd_protocol_register(&ifd_protocol_eurochip);
	ifd_protocol_register(&ifd_protocol_esc);

	if (ifd_conf_get_integer("debug", &ival) >= 0 && ival > ct_config.debug)
		ct_config.debug = ival;

	if (ifd_conf_get_string("ifdhandler.program", &sval) >= 0)
		ct_config.ifdhandler = sval;

	/* Register all driver information (use hotplug ids) */
	n = ifd_conf_get_nodes("driver", NULL, 0);
	if (n >= 0) {
		nodes = (ifd_conf_node_t **) calloc(n, sizeof(*nodes));
		if (!nodes) {
			ct_error("out of memory");
			return 1;
		}
		n = ifd_conf_get_nodes("driver", nodes, n);
		for (i = 0; i < n; i++) {
			if (configure_driver(nodes[i])) {
				free(nodes);
				return 1;
			}
		}
		free(nodes);
	}
	return 0;
}

/*
 * Configure a reader driver
 */
static int configure_driver(ifd_conf_node_t * cf)
{
	const char *driver;
	char **ids;
	int j, n;

	if (!(driver = cf->value))
		return 1;
	if ((n = ifd_conf_node_get_string_list(cf, "ids", NULL, 0)) >= 0) {
		ids = (char **)calloc(n, sizeof(char *));
		if (!ids) {
			ct_error("out of memory");
			return 1;
		}
		n = ifd_conf_node_get_string_list(cf, "ids", ids, n);
		for (j = 0; j < n; j++)
			ifd_driver_add_id(ids[j], driver);
		free(ids);
	}
	return 0;
}
