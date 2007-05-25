/*
 * RIA - remote IFD access
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include <openct/path.h>
#include <openct/socket.h>
#include <openct/server.h>
#include <openct/logging.h>
#include "internal.h"
#include "ria.h"

#define RIA_RESPONSE	255	/* pseudo command code */
#define RIA_QUEUE_LEN	256
#define RIA_SEND_CHUNK	128
#define RIA_DEFAULT_TIMEOUT	4000

static void ifd_remote_close(ifd_device_t *);

ria_client_t *ria_connect(const char *address)
{
	ria_client_t *clnt;
	char path[PATH_MAX];
	int rc;

	if (!address) {
		return NULL;
	}

	if (!ct_format_path(path, PATH_MAX, address)) {
		return NULL;
	}

	clnt = (ria_client_t *) calloc(1, sizeof(*clnt) + RIA_QUEUE_LEN);
	if (!clnt) {
		ct_error("out of memory");
		return NULL;
	}
	ct_buf_init(&clnt->data, (clnt + 1), RIA_QUEUE_LEN);

	clnt->sock = ct_socket_new(1024);
	if ((rc = ct_socket_connect(clnt->sock, path)) < 0) {
		ct_error("Failed to connect to RIA server \"%s\": %s",
			 path, ct_strerror(rc));
		ria_free(clnt);
		return NULL;
	}

	return clnt;
}

void ria_free(ria_client_t * clnt)
{
	if (clnt->sock)
		ct_socket_free(clnt->sock);
	free(clnt);
}

int ria_send(ria_client_t * clnt, unsigned char cmd, const void *arg_buf,
	     size_t arg_len)
{
	unsigned char buffer[512];
	ct_buf_t args;
	header_t header;
	int rc;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_putc(&args, cmd);
	ct_buf_put(&args, arg_buf, arg_len);

	clnt->xid++;
	if (clnt->xid == 0)
		clnt->xid++;

	memset(&header, 0, sizeof(header));
	header.xid = clnt->xid;

	ria_print_packet(clnt->sock, 4, "ria_send", &header, &args);
	if ((rc = ct_socket_put_packet(clnt->sock, &header, &args)) < 0)
		return rc;

	/* Leave transmitting to the main loop */
	return 0;
}

static int ria_recv(ria_client_t * clnt, unsigned char expect, uint32_t xid,
		    void *res_buf, size_t res_len, long timeout)
{
	ct_socket_t *sock = clnt->sock;
	struct timeval begin;
	unsigned char buffer[512];
	ct_buf_t resp;
	header_t header;
	int rc;

	/* Flush out any pending packets */
	if ((rc = ct_socket_flsbuf(sock, 1)) < 0)
		return rc;

	gettimeofday(&begin, NULL);
	if (timeout < 0)
		timeout = 0;
	/* Always slap on addition timeout for round-trip */
	timeout += RIA_DEFAULT_TIMEOUT;

	/* Now receive packets until we get the response.
	 * Handle data packets properly */
	ct_buf_init(&resp, buffer, sizeof(buffer));
	while (1) {
		unsigned char cmd;
		long wait = -1;
		size_t count;

		ct_buf_clear(&resp);
		if ((rc = ct_socket_get_packet(sock, &header, &resp)) < 0)
			return rc;

		/* If there's no complete packet in the receive
		 * buffer, we need to wait for input. */
		if (rc == 0) {
			wait = timeout - ifd_time_elapsed(&begin);
			if (wait < 0)
				return IFD_ERROR_TIMEOUT;

			if ((rc = ct_socket_filbuf(sock, wait)) < 0)
				return rc;
			continue;
		}

		ria_print_packet(sock, 4, "ria_recv", &header, &resp);

		/* Complete packet. Check type */
		if (header.dest != 0) {
			cmd = RIA_RESPONSE;
		} else if (ct_buf_get(&resp, &cmd, 1) < 0)
			continue;

		count = ct_buf_avail(&resp);
		if (cmd == RIA_DATA) {
			ct_buf_put(&clnt->data, ct_buf_head(&resp), count);
			if (expect == RIA_DATA)
				return count;
			continue;
		}

		if (header.xid == xid && cmd == expect) {
			if (header.error < 0)
				return header.error;
			if (count < res_len)
				res_len = count;
			ct_buf_get(&resp, res_buf, res_len);
			return res_len;
		}
	}
	while (header.xid != xid) ;
}

int ria_command(ria_client_t * clnt, unsigned char cmd, const void *arg_buf,
		size_t arg_len, void *res_buf, size_t res_len, long timeout)
{
	int rc;

	if ((rc = ria_send(clnt, cmd, arg_buf, arg_len)) < 0)
		return rc;

	if (timeout < 0)
		timeout = RIA_DEFAULT_TIMEOUT;
	rc = ria_recv(clnt, RIA_RESPONSE, clnt->xid, res_buf, res_len, timeout);
	return rc;
}

static int ria_claim_device(ria_client_t * clnt, const char *name,
			    ria_device_t * info)
{
	return ria_command(clnt, RIA_MGR_CLAIM, name, strlen(name),
			   info, sizeof(*info), -1);
}

/*
 * Reset remote device
 */
static int ifd_remote_reset(ifd_device_t * dev)
{
	ria_client_t *clnt = (ria_client_t *) dev->user_data;

	ifd_debug(2, "called");
	if (clnt == NULL)
		return IFD_ERROR_DEVICE_DISCONNECTED;
	return ria_command(clnt, RIA_RESET_DEVICE, NULL, 0, NULL, 0, -1);
}

/*
 * Device specific portion of RIA client
 */
static int ifd_remote_get_params(ifd_device_t * dev,
				 ifd_device_params_t * params)
{
	ria_client_t *clnt = (ria_client_t *) dev->user_data;

	ifd_debug(2, "called");
	if (clnt == NULL)
		return IFD_ERROR_DEVICE_DISCONNECTED;

	if (dev->type == IFD_DEVICE_TYPE_SERIAL) {
		ria_serial_conf_t rconf;
		int rc;

		rc = ria_command(clnt, RIA_SERIAL_GET_CONFIG,
				 NULL, 0, &rconf, sizeof(rconf), -1);
		params->serial.speed = ntohl(rconf.speed);
		params->serial.bits = rconf.bits;
		params->serial.stopbits = rconf.stopbits;
		params->serial.parity = rconf.parity;
		params->serial.check_parity = rconf.check_parity;
		params->serial.rts = rconf.rts;
		params->serial.dtr = rconf.dtr;
		return 0;
	}

	return IFD_ERROR_NOT_SUPPORTED;
}

static int ifd_remote_set_params(ifd_device_t * dev,
				 const ifd_device_params_t * params)
{
	ria_client_t *clnt = (ria_client_t *) dev->user_data;

	ifd_debug(2, "called");
	if (clnt == NULL)
		return IFD_ERROR_DEVICE_DISCONNECTED;

	if (dev->type == IFD_DEVICE_TYPE_SERIAL) {
		ria_serial_conf_t rconf;

		rconf.speed = htonl(params->serial.speed);
		rconf.bits = params->serial.bits;
		rconf.stopbits = params->serial.stopbits;
		rconf.parity = params->serial.parity;
		rconf.check_parity = params->serial.check_parity;
		rconf.rts = params->serial.rts;
		rconf.dtr = params->serial.dtr;
		return ria_command(clnt, RIA_SERIAL_SET_CONFIG,
				   &rconf, sizeof(rconf), NULL, 0, -1);
	}

	return IFD_ERROR_NOT_SUPPORTED;
}

static void ifd_remote_flush(ifd_device_t * dev)
{
	ria_client_t *clnt = (ria_client_t *) dev->user_data;

	ifd_debug(2, "called");
	if (clnt == NULL)
		return;

	ria_command(clnt, RIA_FLUSH_DEVICE, NULL, 0, NULL, 0, -1);
	ct_buf_clear(&clnt->data);
}

static void ifd_remote_send_break(ifd_device_t * dev, unsigned int usec)
{
	ria_client_t *clnt = (ria_client_t *) dev->user_data;
	unsigned int wait;

	ifd_debug(2, "called");
	if (clnt == NULL)
		return;
	wait = htonl(usec);
	ria_command(clnt, RIA_SEND_BREAK, &wait, sizeof(wait), NULL, 0, -1);
	ct_buf_clear(&clnt->data);
}

static int ifd_remote_send(ifd_device_t * dev, const unsigned char *buffer,
			   size_t len)
{
	ria_client_t *clnt = (ria_client_t *) dev->user_data;
	unsigned int n, count = 0;
	int rc;

	ifd_debug(2, "called, data:%s", ct_hexdump(buffer, len));
	if (clnt == NULL)
		return IFD_ERROR_DEVICE_DISCONNECTED;

	while (count < len) {
		if ((n = len - count) > RIA_SEND_CHUNK)
			n = RIA_SEND_CHUNK;
		if ((rc = ria_send(clnt, RIA_DATA, buffer, n)) < 0) {
			if (rc == IFD_ERROR_NOT_CONNECTED) {
				ifd_remote_close(dev);
				return IFD_ERROR_DEVICE_DISCONNECTED;
			}
			return rc;
		}
		count += n;
	}

	return count;
}

static int ifd_remote_recv(ifd_device_t * dev, unsigned char *buffer,
			   size_t len, long timeout)
{
	ria_client_t *clnt = (ria_client_t *) dev->user_data;
	size_t total = len;
	struct timeval begin;
	int n;

	gettimeofday(&begin, NULL);
	ifd_debug(2, "called, timeout=%ld, len=%u", timeout, len);
	if (clnt == NULL)
		return IFD_ERROR_DEVICE_DISCONNECTED;

	while (len) {
		long wait;

		/* See if there's any data queued */
		if ((n = ct_buf_avail(&clnt->data)) != 0) {
			if (n > len)
				n = len;
			ct_buf_get(&clnt->data, buffer, n);
			if (ct_config.debug >= 9)
				ifd_debug(9, "got %s", ct_hexdump(buffer, n));
			buffer += n;
			len -= n;
			continue;
		}

		if ((wait = timeout - ifd_time_elapsed(&begin)) < 0)
			goto timeout;

		ifd_debug(8, "Need another %u bytes of data, "
			  "remaining timeout %ld", len, wait);
		n = ria_recv(clnt, RIA_DATA, 0, NULL, 0, wait);
		if (n < 0) {
			ct_error("%s: error while waiting for input: %s",
				 dev->name, ct_strerror(n));
			if (n == IFD_ERROR_NOT_CONNECTED) {
				ifd_remote_close(dev);
				return IFD_ERROR_DEVICE_DISCONNECTED;
			}
			return n;
		}
	}

	return total;

      timeout:			/* Timeouts are a little special; they may happen e.g.
				 * when trying to obtain the ATR */
	if (!ct_config.suppress_errors)
		ct_error("%s: timed out while waiting for input", dev->name);
	ifd_debug(9, "(%u bytes received so far)", total - len);
	return IFD_ERROR_TIMEOUT;
}

static int ifd_remote_poll_presence(ifd_device_t * dev, struct pollfd *pfd)
{
	if (dev->user_data == NULL)
		return 0;
	return IFD_ERROR_NOT_SUPPORTED;
}

static void ifd_remote_close(ifd_device_t * dev)
{
	ria_client_t *clnt = (ria_client_t *) dev->user_data;

	dev->user_data = NULL;
	if (clnt)
		ria_free(clnt);
}

static struct ifd_device_ops ifd_remote_ops;

/*
 * Open remote IFD
 */
ifd_device_t *ifd_open_remote(const char *ident)
{
	ria_client_t *clnt;
	ria_device_t devinfo;
	ifd_device_t *dev;
	char name[256], *addr;
	int rc, type;

	strncpy(name, ident, sizeof(name));
	name[sizeof(name) - 1] = '\0';

	if ((addr = strchr(name, '@')) == NULL) {
		ct_error("remote device name must be handle@host");
		return NULL;
	}
	*addr++ = '\0';

	/* Connect to RIA server */
	if (!(clnt = ria_connect(addr)))
		return NULL;

	if ((rc = ria_claim_device(clnt, name, &devinfo)) < 0) {
		ct_error("unable to claim device \"%s\": %s",
			 name, ct_strerror(rc));
		ria_free(clnt);
		return NULL;
	}

	if (!strcmp(devinfo.type, "serial")) {
		type = IFD_DEVICE_TYPE_SERIAL;
	} else if (!strcmp(devinfo.type, "usb")) {
		type = IFD_DEVICE_TYPE_USB;
	} else {
		ct_error("Unknown device type \"%s\"", devinfo.type);
		ria_free(clnt);
		return NULL;
	}

	ifd_remote_ops.reset = ifd_remote_reset;
	ifd_remote_ops.set_params = ifd_remote_set_params;
	ifd_remote_ops.get_params = ifd_remote_get_params;
	ifd_remote_ops.flush = ifd_remote_flush;
	ifd_remote_ops.send = ifd_remote_send;
	ifd_remote_ops.send_break = ifd_remote_send_break;
	ifd_remote_ops.recv = ifd_remote_recv;
	ifd_remote_ops.close = ifd_remote_close;
	ifd_remote_ops.poll_presence = ifd_remote_poll_presence;

	dev = ifd_device_new(ident, &ifd_remote_ops, sizeof(*dev));
	dev->hotplug = 1;
	dev->timeout = 2000;
	dev->type = type;
	dev->user_data = clnt;

	if ((rc = ifd_device_reset(dev)) < 0) {
		ct_error("Failed to reset device: %s", ct_strerror(rc));
		ifd_device_close(dev);
		return NULL;
	}

	return dev;
}

/*
 * Debugging aid: print packet
 */
void ria_print_packet(ct_socket_t * sock, int level, const char *func,
		      header_t * hdr, ct_buf_t * args)
{
	ct_buf_t temp = *args;
	char buffer[128], *msg;
	unsigned char cmd;
	unsigned int len;

	if (level > ct_config.debug)
		return;

	if (hdr->dest) {
		int err = hdr->error;

		msg = "RESP";
		if (err) {
			snprintf(buffer, sizeof(buffer),
				 "RESP, err=%d (%s)", err, ct_strerror(err));
			msg = buffer;
		}
	} else if (ct_buf_get(&temp, &cmd, 1) < 0) {
		msg = "TRUNC-CALL";
	} else {
		switch (cmd) {
		case RIA_MGR_LIST:
			msg = "LIST";
			break;
		case RIA_MGR_INFO:
			msg = "INFO";
			break;
		case RIA_MGR_CLAIM:
			msg = "CLAIM";
			break;
		case RIA_MGR_REGISTER:
			msg = "REGISTER";
			break;
		case RIA_RESET_DEVICE:
			msg = "RESET_DEVICE";
			break;
		case RIA_FLUSH_DEVICE:
			msg = "FLUSH_DEVICE";
			break;
		case RIA_SEND_BREAK:
			msg = "SEND_BREAK";
			break;
		case RIA_SERIAL_GET_CONFIG:
			msg = "SERIAL_GET_CONFIG";
			break;
		case RIA_SERIAL_SET_CONFIG:
			msg = "SERIAL_SET_CONFIG";
			break;
		case RIA_DATA:
			msg = "DATA";
			break;
		default:
			snprintf(buffer, sizeof(buffer), "CALL%u", cmd);
			msg = buffer;
		}
	}

	len = ct_buf_avail(&temp);
	if (len == 0) {
		ct_debug("%s: [%08x] %s", func, hdr->xid, msg);
	} else if (len < 16) {
		ct_debug("%s: [%08x] %s, args%s", func, hdr->xid, msg,
			 ct_hexdump(ct_buf_head(&temp), len));
	} else {
		ct_debug("%s: [%08x] %s, args%s ... (%u bytes total)",
			 func, hdr->xid, msg,
			 ct_hexdump(ct_buf_head(&temp), 16), len);
	}
}
