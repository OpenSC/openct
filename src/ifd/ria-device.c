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

static int ria_devsock_process(ct_socket_t *, header_t *,
			       ct_buf_t *, ct_buf_t *);
static void ria_devsock_close(ct_socket_t *);
static int ria_poll_device(ct_socket_t *, struct pollfd *);
static void ria_close_device(ct_socket_t *);

/*
 * Handle device side of things
 */
ria_client_t *ria_export_device(const char *address, const char *device)
{
	ifd_device_t *dev;
	ria_client_t *ria;
	ct_socket_t *sock;

	/* Open device */
	if (!(dev = ifd_device_open(device))) {
		ct_error("Unable to open device %s\n", device);
		exit(1);
	}

	if (dev->type != IFD_DEVICE_TYPE_SERIAL) {
		ct_error("Unable to handle devices other that serial");
		exit(1);
	}

	/* Connect to ifd proxy */
	if (!(ria = ria_connect(address)))
		exit(1);
	ria->user_data = dev;
	ria->sock->process = ria_devsock_process;
	ria->sock->close = ria_devsock_close;
	ria->sock->user_data = ria;

	ct_mainloop_add_socket(ria->sock);

	/* Set up the fake socket encapsulating the device */
	sock = ct_socket_new(0);
	sock->fd = 0x7FFFFFFF;
	sock->user_data = ria;
	sock->poll = ria_poll_device;
	sock->close = ria_close_device;
	sock->recv = NULL;
	sock->send = NULL;
	ct_mainloop_add_socket(sock);

	return ria;
}

int ria_register_device(ria_client_t * ria, const char *name)
{
	ifd_device_t *dev = (ifd_device_t *) ria->user_data;
	ria_device_t devinfo;

	memset(&devinfo, 0, sizeof(devinfo));
	strncpy(devinfo.name, name, RIA_NAME_MAX - 1);
	if (dev->type == IFD_DEVICE_TYPE_SERIAL)
		strcpy(devinfo.type, "serial");
	else if (dev->type == IFD_DEVICE_TYPE_USB)
		strcpy(devinfo.type, "usb");
	else
		strcpy(devinfo.type, "other");

	return ria_command(ria, RIA_MGR_REGISTER,
			   &devinfo, sizeof(devinfo), NULL, 0, -1);
}

static int ria_devsock_process(ct_socket_t * sock, header_t * hdr,
			       ct_buf_t * args, ct_buf_t * resp)
{
	ria_client_t *ria = (ria_client_t *) sock->user_data;
	ifd_device_t *dev = (ifd_device_t *) ria->user_data;
	unsigned char cmd;
	int rc, count;

	ria_print_packet(sock, 2, "ria_devsock_process", hdr, args);

	/* Unexpected reply on this socket - simply drop */
	if (hdr->dest != 0) {
		hdr->xid = 0;
		return 0;
	}

	if ((rc = ct_buf_get(args, &cmd, 1)) < 0)
		return rc;

	switch (cmd) {
	case RIA_FLUSH_DEVICE:
		ifd_device_flush(dev);
		return 0;
	case RIA_SEND_BREAK:
		{
			unsigned int usec;
			if ((rc = ct_buf_get(args, &usec, sizeof(usec))) < 0)
				return rc;
			ifd_device_send_break(dev, ntohl(usec));
			return 0;
		}
	case RIA_SERIAL_GET_CONFIG:
		{
			ifd_device_params_t parms;
			ria_serial_conf_t conf;

			if (dev->type != IFD_DEVICE_TYPE_SERIAL)
				return IFD_ERROR_INCOMPATIBLE_DEVICE;
			if ((rc = ifd_device_get_parameters(dev, &parms)) < 0)
				return rc;
			conf.speed = htonl(parms.serial.speed);
			conf.bits = parms.serial.bits;
			conf.stopbits = parms.serial.stopbits;
			conf.parity = parms.serial.parity;
			conf.check_parity = parms.serial.check_parity;
			conf.rts = parms.serial.rts;
			conf.dtr = parms.serial.dtr;
			return ct_buf_put(resp, &conf, sizeof(conf));
		}

	case RIA_SERIAL_SET_CONFIG:
		{
			ifd_device_params_t parms;
			ria_serial_conf_t conf;

			if (dev->type != IFD_DEVICE_TYPE_SERIAL)
				return IFD_ERROR_INCOMPATIBLE_DEVICE;
			if ((rc = ct_buf_get(args, &conf, sizeof(conf))) < 0)
				return rc;
			parms.serial.speed = ntohl(conf.speed);
			parms.serial.bits = conf.bits;
			parms.serial.stopbits = conf.stopbits;
			parms.serial.parity = conf.parity;
			parms.serial.check_parity = conf.check_parity;
			parms.serial.rts = conf.rts;
			parms.serial.dtr = conf.dtr;
			if ((rc = ifd_device_set_parameters(dev, &parms)) < 0)
				return rc;
			return 0;
		}

	case RIA_DATA:
		hdr->xid = 0;	/* no reponse */
		count = ct_buf_avail(args);
		rc = ct_buf_put(&ria->data, ct_buf_head(args), count);
		if (rc < 0) {
			ct_buf_compact(&ria->data);
			rc = ct_buf_put(&ria->data, ct_buf_head(args), count);
		}
		if (rc < 0)
			ifd_debug(1, "unable to queue %u bytes for device",
				  count);
		return 0;
	default:
		ct_error("Unexpected command 0x02%x\n", cmd);
		return IFD_ERROR_INVALID_CMD;
	}

}

static void ria_devsock_close(ct_socket_t * sock)
{
	ct_error("Network connection closed, exiting\n");
	exit(0);
}

static int ria_poll_device(ct_socket_t * sock, struct pollfd *pfd)
{
	unsigned char buffer[512];
	ria_client_t *ria = (ria_client_t *) sock->user_data;
	ifd_device_t *dev = (ifd_device_t *) ria->user_data;
	int n, rc;

	pfd->fd = dev->fd;
	if (pfd->revents & POLLIN) {
		n = read(dev->fd, buffer, sizeof(buffer));
		if (n < 0) {
			ct_error("error reading from device: %m");
			return -1;
		}

		ifd_debug(2, "read%s", ct_hexdump(buffer, n));
		if ((rc = ria_send(ria, RIA_DATA, buffer, n)) < 0)
			return rc;
	}
	if (pfd->revents & POLLOUT) {
		n = write(dev->fd, ct_buf_head(&ria->data),
			  ct_buf_avail(&ria->data));
		if (n < 0) {
			ct_error("error writing to device: %m");
			return -1;
		}

		ifd_debug(2, "wrote%s", ct_hexdump(ct_buf_head(&ria->data), n));
		ct_buf_get(&ria->data, NULL, n);
	}

	if (ifd_device_poll_presence(dev, pfd) == 0) {
		ifd_debug(1, "Device detached, exiting");
		exit(0);
	}

	pfd->events |= POLLIN;
	if (ct_buf_avail(&ria->data))
		pfd->events |= POLLOUT;

	if (1 /* hotplug */ )
		pfd->events |= POLLHUP;

	return 1;
}

static void ria_close_device(ct_socket_t * sock)
{
	ct_error("Dispatcher requests that device is closed, abort");
	exit(1);
}
