/*
 * Generic IFD device layer
 *
 *
 */

#include "internal.h"
#include <stdlib.h>

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
	if (!dev || !dev->ops || !dev->ops->recv)
		return -1;
	return dev->ops->recv(dev, data, len, timeout);
}

void
ifd_device_close(ifd_device_t *dev)
{
	if (!dev)
		return;
	if (dev->ops && dev->ops->close)
		dev->ops->close(dev);
	free(dev);
}
