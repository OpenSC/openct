/*
 * Initialize the library
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include "internal.h"

extern void	ifd_etoken_register(void);
extern void	ifd_kaan_register(void);
extern void	ifd_towitoko_register(void);

int
ifd_init(void)
{
	/* Register built-in drivers */
	ifd_etoken_register();
	ifd_kaan_register();
	ifd_towitoko_register();

	/* Register all builtin protocols */
	ifd_protocol_register(&ifd_protocol_t0);
	ifd_protocol_register(&ifd_protocol_t1);
	ifd_protocol_register(&ifd_protocol_trans);

	return 0;
}

