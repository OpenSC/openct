/*
 * Path handling routines
 *
 * Copyright (C) 2006, Andreas Jellinghaus <aj@dungeon.inka.de>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <openct/path.h>

/*
 * Format path
 */
int ct_format_path(char *path, const size_t pathlen, const char *file)
{
	int rc;

	if (!file)
		return 0;

#if defined (sunray) || defined (sunrayclient)
	{
		if (getenv("UTDEVROOT"))
			rc = snprintf(path, pathlen,
				      "%s/openct/%s", getenv("UTDEVROOT"),
				      file);
		else if (getenv("OPENCT_SOCKETDIR"))
			rc = snprintf(path, pathlen,
				      "%s/%s", getenv("OPENCT_SOCKETDIR"),
				      file);
		else
			rc = snprintf(path, pathlen,
				      "%s/%s", OPENCT_SOCKET_PATH, file);
	}
#else
	if (getenv("OPENCT_SOCKETDIR")) {
		rc = snprintf(path, pathlen,
			      "%s/%s", getenv("OPENCT_SOCKETDIR"), file);
	} else {
		rc = snprintf(path, pathlen, "%s/%s", OPENCT_SOCKET_PATH, file);
	}
#endif
	if (rc < 0) {
		/* hmm. error handling? */
		return 0;

	}
	if (rc >= pathlen) {
		/* truncated */
		return 0;
	}

	return 1;
}
