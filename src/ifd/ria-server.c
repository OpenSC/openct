/*
 * Remote device access - debugging utility that allows to
 * test smart card readers on remote hosts.
 * 
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <openct/socket.h>
#include <openct/server.h>
#include <openct/logging.h>
#include <openct/ifd.h>
#include "internal.h"
#include "ria.h"

typedef struct ria_peer {
	struct ria_peer *next;
	struct ria_peer *prev;
	ct_socket_t *sock;
	struct ria_peer *peer;
	ria_device_t device;
} ria_peer_t;

static unsigned int dev_handle = 1;

static ria_peer_t clients = { &clients, &clients };

static int ria_svc_accept(ct_socket_t *);
static int ria_svc_app_handler(ct_socket_t *, header_t *,
			       ct_buf_t *, ct_buf_t *);
static int ria_svc_dev_handler(ct_socket_t *, header_t *,
			       ct_buf_t *, ct_buf_t *);
static void ria_svc_app_close(ct_socket_t *);
static void ria_svc_dev_close(ct_socket_t *);
static ria_peer_t *ria_peer_new(ct_socket_t *);
static void ria_peer_free(ria_peer_t *, int);
static ria_peer_t *ria_find_device(const char *, size_t);
static void ria_svc_link(ria_peer_t *);
static void ria_svc_unlink(ria_peer_t *);

int ria_svc_listen(const char *address, int trusted)
{
	ct_socket_t *sock;
	int rc;

	sock = ct_socket_new(1024);
	if ((rc = ct_socket_listen(sock, address, 0666)) < 0) {
		ct_error("Cannot bind to network address \"%s\"\n", address);
		ct_socket_free(sock);
		return rc;
	}

	sock->recv = ria_svc_accept;
	sock->send = NULL;
	if (trusted) {
		sock->process = ria_svc_app_handler;
		sock->close = ria_svc_app_close;
	} else {
		sock->process = ria_svc_dev_handler;
		sock->close = ria_svc_dev_close;
	}

	ct_mainloop_add_socket(sock);
	return 0;
}

static int ria_svc_accept(ct_socket_t * listener)
{
	ria_peer_t *clnt;
	ct_socket_t *sock;
	int rc;

	if (!(sock = ct_socket_accept(listener)))
		return 0;

	clnt = ria_peer_new(sock);
	rc = ct_socket_getpeername(sock,
				   clnt->device.address,
				   sizeof(clnt->device.address));
	if (rc < 0) {
		ria_peer_free(clnt, 0);
		return rc;
	}

	ifd_debug(1, "New connection from %s", clnt->device.address);
	sock->user_data = clnt;
	sock->process = listener->process;
	sock->close = listener->close;
	return 0;
}

static void ria_svc_app_close(ct_socket_t * sock)
{
	ria_peer_t *clnt = (ria_peer_t *) sock->user_data;

	ifd_debug(1, "Application on %s closed connection",
		  clnt->device.address);
	ria_peer_free((ria_peer_t *) sock->user_data, 0);
}

static void ria_svc_dev_close(ct_socket_t * sock)
{
	ria_peer_t *clnt = (ria_peer_t *) sock->user_data;

	ifd_debug(1, "Device on %s closed connection", clnt->device.address);
	ria_peer_free((ria_peer_t *) sock->user_data, 1);
}

/*
 * Process commands from local clients (i.e. those allowed
 * to claim a device).
 */
static int ria_svc_app_handler(ct_socket_t * sock, header_t * hdr,
			       ct_buf_t * args, ct_buf_t * resp)
{
	unsigned char cmd;
	ria_peer_t *clnt, *peer;
	int rc;

	clnt = (ria_peer_t *) sock->user_data;
	ria_print_packet(sock, 2, "app >>", hdr, args);

	if (ct_buf_get(args, &cmd, 1) < 0)
		return IFD_ERROR_INVALID_MSG;

	switch (cmd) {
	case RIA_MGR_LIST:
		peer = &clients;
		ifd_debug(1, "%s requests a device listing",
			  clnt->device.address);
		while ((peer = peer->next) != &clients) {
			if (peer->device.name[0] != '\0')
				ct_buf_put(resp, &peer->device,
					   sizeof(peer->device));
		}
		return 0;

	case RIA_MGR_INFO:
		peer =
		    ria_find_device((const char *)ct_buf_head(args),
				    ct_buf_avail(args));
		if (peer == NULL)
			return IFD_ERROR_UNKNOWN_DEVICE;
		ct_buf_put(resp, &peer->device, sizeof(peer->device));
		return 0;

	case RIA_MGR_CLAIM:
		peer =
		    ria_find_device((const char *)ct_buf_head(args),
				    ct_buf_avail(args));
		if (peer == NULL)
			return IFD_ERROR_UNKNOWN_DEVICE;
		if (peer->peer)
			return IFD_ERROR_DEVICE_BUSY;
		ifd_debug(1, "%s claimed %s device %s/%s",
			  clnt->device.address,
			  peer->device.type,
			  peer->device.address, peer->device.name);
		ct_buf_put(resp, &peer->device, sizeof(peer->device));
		clnt->peer = peer;
		peer->peer = clnt;
		return 0;
	}

	if (cmd < __RIA_PEER_CMD_BASE)
		return IFD_ERROR_INVALID_CMD;

	/* All subsequent commands require a device */
	if ((peer = clnt->peer) == NULL)
		return IFD_ERROR_NOT_CONNECTED;

	/* Push back the command byte */
	ct_buf_push(args, &cmd, 1);
	rc = ct_socket_put_packet(peer->sock, hdr, args);

	/* Tell the caller not to send a response */
	hdr->xid = 0;
	return rc;
}

/*
 * Process commands from remote clients (i.e. those offering a device).
 */
static int ria_svc_dev_handler(ct_socket_t * sock, header_t * hdr,
			       ct_buf_t * args, ct_buf_t * resp)
{
	unsigned char cmd;
	ria_peer_t *clnt, *peer;
	ria_device_t devinfo;
	int rc;

	clnt = (ria_peer_t *) sock->user_data;
	ria_print_packet(sock, 2, "dev <<", hdr, args);

	/* bounce response to peer right away */
	if (hdr->dest)
		goto bounce_to_peer;

	if (ct_buf_get(args, &cmd, 1) < 0)
		return IFD_ERROR_INVALID_MSG;

	switch (cmd) {
	case RIA_MGR_REGISTER:
		if (clnt->device.name[0])
			return IFD_ERROR_INVALID_ARG;
		if ((rc = ct_buf_get(args, &devinfo, sizeof(devinfo))) < 0)
			return IFD_ERROR_INVALID_ARG;
		if (devinfo.type[0] == '\0')
			return IFD_ERROR_INVALID_ARG;
		/* For security reasons, don't allow the handle counter
		 * to wrap around. */
		if (dev_handle == 0)
			return IFD_ERROR_GENERIC;

		memcpy(&devinfo.address, clnt->device.address, RIA_NAME_MAX);
		clnt->device = devinfo;
		snprintf(clnt->device.handle, RIA_NAME_MAX,
			 "%s%u", clnt->device.type, dev_handle++);
		ifd_debug(1,
			  "%s registered new %s device , handle '%s', name `%s'",
			  clnt->device.address, clnt->device.type,
			  clnt->device.handle, clnt->device.name);
		return 0;
	}

	if (cmd < __RIA_PEER_CMD_BASE)
		return IFD_ERROR_INVALID_CMD;

	/* Push back the command byte */
	ct_buf_push(args, &cmd, 1);

      bounce_to_peer:
	if ((peer = clnt->peer) == NULL)
		return IFD_ERROR_NOT_CONNECTED;

	rc = ct_socket_put_packet(peer->sock, hdr, args);

	/* Tell the caller not to send a response */
	hdr->xid = 0;
	return rc;
}

static ria_peer_t *ria_peer_new(ct_socket_t * sock)
{
	ria_peer_t *clnt;

	clnt = (ria_peer_t *) calloc(1, sizeof(ria_peer_t));
	if (!clnt) {
		ct_error("out of memory");
		return NULL;
	}
	clnt->sock = sock;
	ria_svc_link(clnt);

	return clnt;
}

static void ria_peer_free(ria_peer_t * clnt, int detach_peer)
{
	ria_peer_t *peer;

	if ((peer = clnt->peer) != NULL) {
		if (detach_peer)
			shutdown(peer->sock->fd, SHUT_RD);
		peer->peer = NULL;
	}
	if (clnt->device.name[0])
		ifd_debug(1, "Removing device `%s' on %s",
			  clnt->device.name, clnt->device.address);
	ria_svc_unlink(clnt);
	memset(clnt, 0, sizeof(*clnt));
	free(clnt);
}

static void ria_svc_link(ria_peer_t * clnt)
{
	ria_peer_t *prev;

	prev = clients.prev;
	clnt->next = &clients;
	clnt->prev = prev;
	prev->next = clnt;
	clients.prev = clnt;
}

static ria_peer_t *ria_find_device(const char *handle, size_t len)
{
	ria_peer_t *peer;

	ifd_debug(2, "handle=%*.*s", (int)len, (int)len, handle);

	if (len == 0 || len > RIA_NAME_MAX - 1)
		return NULL;

	peer = &clients;
	while ((peer = peer->next) != &clients) {
		if (!memcmp(peer->device.handle, handle, len)
		    && peer->device.handle[len] == '\0')
			return peer;
		if (!memcmp(peer->device.name, handle, len)
		    && peer->device.name[len] == '\0')
			return peer;
	}

	return NULL;
}

static void ria_svc_unlink(ria_peer_t * clnt)
{
	ria_peer_t *prev, *next;

	prev = clnt->prev;
	next = clnt->next;
	prev->next = next;
	next->prev = prev;
	clnt->prev = clnt->next = clnt;
}
