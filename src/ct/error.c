/*
 * Error handling
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
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

#define DIM(v)		(sizeof(v)/(sizeof((v)[0])))

const char *ct_strerror(int rc)
{
	const char *proto_errors[] = {
		"Invalid message",
		"Invalid command",
		"Missing argument",
		"Not connected to IFD handler",
	};
	const int proto_base = -IFD_ERROR_INVALID_MSG;
	const char *gen_errors[] = {
		"Success",
		"Generic error",
		"Command timed out",
		"Invalid slot",
		"Operation not supported",
		"Communication error",
		"No card present",
		"Reader already locked",
		"Reader not locked",
		"Invalid argument",
		"Out of memory",
		"Buffer too small",
		"Timeout on user input",
		"Operation aborted by user",
		"PIN mismatch",
		"Unable to reset card",
	};
	const int gen_base = -IFD_SUCCESS;
	const char **errors = NULL, *msg = NULL;
	int count = 0, err_base = 0, error = rc;
	static char message[64];

	if (error < 0)
		error = -error;
	if (error >= proto_base) {
		errors = proto_errors;
		count = DIM(proto_errors);
		err_base = proto_base;
	} else if (error >= gen_base) {
		errors = gen_errors;
		count = DIM(gen_errors);
		err_base = gen_base;
	}
	error -= err_base;
	if (error >= count || count == 0) {
		msg = message;
		snprintf(message, sizeof(message),
			"Unknown OpenCT error %d", -rc);
	} else {
		msg = errors[error];
	}
	return msg;
}
