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

#define MAX_ERROR	256
static const char *	ct_errors[MAX_ERROR] = {
  [IFD_SUCCESS]			= "Success",
  [-IFD_ERROR_GENERIC]		= "Generic error",
  [-IFD_ERROR_TIMEOUT]		= "Command timed out",
  [-IFD_ERROR_INVALID_SLOT]	= "Invalid slot",
  [-IFD_ERROR_NOT_SUPPORTED]	= "Operation not supported",
  [-IFD_ERROR_COMM_ERROR]	= "Communication error",
  [-IFD_ERROR_NO_CARD]		= "No card present",
  [-IFD_ERROR_LOCKED]		= "Reader already locked",
  [-IFD_ERROR_NOLOCK]		= "Reader not locked",
  [-IFD_ERROR_INVALID_ARG]	= "Invalid argument",
  [-IFD_ERROR_NO_MEMORY]	= "Out of memory",
  [-IFD_ERROR_BUFFER_TOO_SMALL]	= "Buffer too small",
  [-IFD_ERROR_USER_TIMEOUT]	= "Timeout on user input",
  [-IFD_ERROR_USER_ABORT]	= "Operation aborted by user",
  [-IFD_ERROR_PIN_MISMATCH]	= "PIN mismatch",
  [-IFD_ERROR_NO_ATR]		= "Unable to reset card",
  [-IFD_ERROR_INVALID_MSG]	= "Invalid message",
  [-IFD_ERROR_INVALID_CMD]	= "Invalid command",
  [-IFD_ERROR_MISSING_ARG]	= "Missing argument",
  [-IFD_ERROR_NOT_CONNECTED]	= "Not connected to IFD handler",
};

const char *
ct_strerror(int rc)
{
	static char	message[64];
	const char	*msg = NULL;

	rc = -rc;
	if (0 <= rc && rc < MAX_ERROR)
		msg = ct_errors[rc];
	if (msg == NULL) {
		msg = message;
		snprintf(message, sizeof(message),
			"Unknown OpenCT error %d", -rc);
	}
	return msg;
}
