/*
 * Error handling
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>
#include <string.h>
#include <openct/error.h>
#include <openct/logging.h>

enum {
	DST_STDERR,
	DST_SYSLOG
};

static int	log_dest = DST_STDERR;

void
ct_log_destination(const char *dest)
{
	if (!strcmp(dest, "@stderr")) {
		log_dest = DST_STDERR;
	} else if (!strcmp(dest, "@syslog")) {
		log_dest = DST_SYSLOG;
		openlog("ifdd", LOG_PID, LOG_DAEMON);
	} else {
		log_dest = DST_STDERR;
		ct_error("log destination %s not implemented yet", dest);
	}
}

void
ct_error(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	if (log_dest == DST_STDERR) {
		fprintf(stderr, "Error: ");
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
	} else {
		vsyslog(LOG_WARNING, fmt, ap);
	}
	va_end(ap);
}

void
ct_debug(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	if (log_dest == DST_STDERR) {
		fprintf(stderr, "Debug: ");
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
	} else {
		vsyslog(LOG_DEBUG, fmt, ap);
	}
	va_end(ap);
}

const char *
ct_strerror(int rc)
{
	static char	message[64];

	snprintf(message, sizeof(message),
		"error code %d", -rc);
	return message;
}
