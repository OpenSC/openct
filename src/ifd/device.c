/*
 * Generic IFD device layer
 *
 *
 */

#include <stdlib.h>
#include <string.h>
#include "internal.h"

ifd_device_t *
ifd_device_new(const char *name, struct ifd_device_ops *ops, size_t size)
{
	ifd_device_t *dev;

	dev = (ifd_device_t *) calloc(1, size);
	dev->name = strdup(name);
	dev->ops = ops;

	return dev;
}

void
ifd_device_free(ifd_device_t *dev)
{
	if (dev->name)
		free(dev->name);
	memset(dev, 0, sizeof(*dev));
	free(dev);
}

int
ifd_device_type(ifd_device_t *dev)
{
	return dev->type;
}

int
ifd_device_identify(ifd_device_t *dev, char *name, size_t len)
{
	if (!dev || !dev->ops || !dev->ops->identify)
		return -1;
	return dev->ops->identify(dev, name, len);
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
ifd_device_recv(ifd_device_t *dev, void *data, size_t len, long timeout)
{
	if (timeout < 0)
		timeout = dev->timeout;

	if (!dev || !dev->ops || !dev->ops->recv)
		return -1;
	return dev->ops->recv(dev, data, len, timeout);
}

int
ifd_device_transceive(ifd_device_t *dev, ifd_apdu_t *apdu, long timeout)
{
	if (timeout < 0)
		timeout = dev->timeout;

	if (!dev || !dev->ops)
		return -1;
	if (dev->ops->transceive)
		return dev->ops->transceive(dev, apdu, timeout);

	/* Fall back to send/recv */
	if (ifd_device_send(dev, apdu->snd_buf, apdu->snd_len) < 0)
		return -1;
	return ifd_device_recv(dev, apdu->rcv_buf, apdu->rcv_len, timeout);
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
