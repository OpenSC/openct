/*
 * Generic IFD device layer
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <string.h>
#include "internal.h"

/*
 * Open a device given the name
 */
ifd_device_t *
ifd_device_open(const char *name)
{
	if (!strncmp(name, "serial:", 7))
		return ifd_open_serial(name + 7);
	if (!strncmp(name, "usb:", 4))
		return ifd_open_usb(name + 4);

	switch (ifd_sysdep_device_type(name)) {
	case IFD_DEVICE_TYPE_SERIAL:
		return ifd_open_serial(name);
	case IFD_DEVICE_TYPE_USB:
		return ifd_open_usb(name);
	/* Other types to be added */
	}
	return NULL;
}

/*
 * Open a device given a numeric "channel" as used in
 * the CTAPI and PCSC interface
 */
ifd_device_t *
ifd_device_open_channel(unsigned int num)
{
	const char	*name;

	if (!(name = ifd_sysdep_channel_to_name(num)))
		return NULL;
	return ifd_device_open(name);
}

/*
 * Create a new device struct
 * This is an internal function called by the different device
 * type handlers (serial, usb, etc)
 */
ifd_device_t *
ifd_device_new(const char *name, struct ifd_device_ops *ops, size_t size)
{
	ifd_device_t *dev;

	dev = (ifd_device_t *) calloc(1, size);
	dev->name = strdup(name);
	dev->ops = ops;

	return dev;
}

/*
 * Destroy a device handle
 */
void
ifd_device_free(ifd_device_t *dev)
{
	if (dev->name)
		free(dev->name);
	memset(dev, 0, sizeof(*dev));
	free(dev);
}

/*
 * Miscellaneous device operations. These functions
 * just do a consistency check on the handle, and route
 * the call to the appropriate member function
 */
int
ifd_device_type(ifd_device_t *dev)
{
	return dev->type;
}

int
ifd_device_identify(const char *name, char *ident, size_t len)
{
	ifd_device_t *dev;
	int res = -1;

	if (!(dev = ifd_device_open(name)))
		return -1;
	if (dev->ops && dev->ops->identify)
		res = dev->ops->identify(dev, ident, len);
	ifd_device_close(dev);
	return res;
}

int
ifd_device_set_parameters(ifd_device_t *dev, const ifd_device_params_t *parms)
{
	if (!dev || !dev->ops || !dev->ops->set_params)
		return -1;
	return dev->ops->set_params(dev, parms);
}

int
ifd_device_get_parameters(ifd_device_t *dev, ifd_device_params_t *parms)
{
	if (!dev || !dev->ops || !dev->ops->get_params)
		return -1;
	return dev->ops->get_params(dev, parms);
}

void
ifd_device_flush(ifd_device_t *dev)
{
	if (!dev || !dev->ops || !dev->ops->flush)
		return;
	dev->ops->flush(dev);
}

int
ifd_device_send(ifd_device_t *dev, const void *data, size_t len)
{
	if (!dev || !dev->ops || !dev->ops->send)
		return -1;
	return dev->ops->send(dev, data, len);
}

int
ifd_device_control(ifd_device_t *dev, void *cmsg, size_t len)
{
	if (!dev || !dev->ops || !dev->ops->control)
		return -1;
	return dev->ops->control(dev, cmsg, len);
}

int
ifd_device_recv(ifd_device_t *dev, void *data, size_t len, long timeout)
{
	if (timeout < 0)
		timeout = dev->timeout;

	if (!dev || !dev->ops || !dev->ops->recv)
		return -1;
	return dev->ops->recv(dev, data, len, timeout);
}

int
ifd_device_transceive(ifd_device_t *dev, 
		const void *sbuf, size_t slen,
		void *rbuf, size_t rlen,
		long timeout)
{
	if (timeout < 0)
		timeout = dev->timeout;

	if (!dev || !dev->ops)
		return -1;
	if (dev->ops->transceive)
		return dev->ops->transceive(dev,
						sbuf, slen,
						rbuf, rlen, timeout);

	/* Fall back to send/recv */
	ifd_device_flush(dev);
	if (ifd_device_send(dev, sbuf, slen) < 0)
		return -1;
	return ifd_device_recv(dev, rbuf, rlen, timeout);
}

int
ifd_device_poll_presence(ifd_device_t *dev, struct pollfd *pfd)
{
	if (!dev || !dev->ops || !dev->ops->poll_presence)
		return 1;
	return dev->ops->poll_presence(dev, pfd);
}

void
ifd_device_close(ifd_device_t *dev)
{
	if (!dev)
		return;
	if (dev->ops && dev->ops->close)
		dev->ops->close(dev);
	ifd_device_free(dev);
}

/*
 * Device ID handling
 */
int
ifd_device_id_parse(const char *str, ifd_devid_t *id)
{
	unsigned int	n;

	id->type = IFD_DEVICE_TYPE_OTHER;

	n = strcspn(str, ":");
	if (str[n] == ':') {
		if (!strncmp(str, "usb", n))
			id->type = IFD_DEVICE_TYPE_USB;
		else
		if (!strncmp(str, "pcmcia", n))
			id->type = IFD_DEVICE_TYPE_PCMCIA;
		else
			return -1;
		str += n + 1;
	}

	for (n = 0; *str && n < IFD_MAX_DEVID_PARTS; n++) {
		id->val[n] = strtoul(str, (char **) &str, 16);
		if (*str == '/')
			str++;
	}

	if (*str || n == 0)
		return -1;
	id->num = n;
	return 0;
}

int
ifd_device_id_match(const ifd_devid_t *match, const ifd_devid_t *id)
{
	if (id->type != match->type
	 || id->num < match->num
	 || memcmp(id->val, match->val, match->num * sizeof(id->val[0])))
		return 0;
	return 1;
}
