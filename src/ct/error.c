/*
 * Error handling
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <stdio.h>
#include <stdarg.h>
#include <openct/error.h>
#include <openct/logging.h>
#include <openct/conf.h>

void
ct_error(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	fprintf(stderr, "Error: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

void
ct_debug(const char *fmt, ...)
{
	va_list	ap;

	if (!ct_config.debug)
		return;

	va_start(ap, fmt);
	fprintf(stderr, "Debug: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}
