/*
 * USB handling
 */

#include <stdlib.h>
#include <string.h>
#include <usb.h>
#include "internal.h"

struct usb_search_spec {
	unsigned int	bus, dev;
	unsigned int	vendor;
	unsigned int	product;
	const char *	filename;

	struct usb_device *found;
};

typedef struct ifd_usb {
	ifd_device_t	base;

	struct usb_dev_handle *h;
} ifd_usb_t;

static int	ifd_usb_init(void);
static int	ifd_usb_scan(struct usb_search_spec *,
				int (*)(struct usb_device *, void *),
				void *);
static int	ifd_usb_match(struct usb_device *, struct usb_search_spec *);

/*
 * Send/receive USB control block
 */
static int
usb_control(ifd_device_t *dev, void *data, size_t len)
{
	ifd_usb_t	*usb = (ifd_usb_t *) dev;
	ifd_usb_cmsg_t	*cmsg;
	int		n;

	cmsg = (ifd_usb_cmsg_t *) data;
	if (len < sizeof(*cmsg) || cmsg->guard != IFD_DEVICE_TYPE_USB)
		return -1;

	n = usb_control_msg(usb->h,
			cmsg->requesttype,
			cmsg->request,
			cmsg->value,
			cmsg->index,
			cmsg->data,
			cmsg->len,
			10000);

	if (n < 0)
		ifd_error("USB: %s", usb_strerror());

	return n;
}

static struct ifd_device_ops	ifd_usb_ops = {
	control:	usb_control,
};

/*
 * Open USB device
 */
ifd_device_t *
ifd_open_usb(const char *device)
{
	struct usb_search_spec spec;
	struct usb_dev_handle *uh;
	ifd_usb_t	*dev;

	ifd_usb_init();

	memset(&spec, 0, sizeof(spec));

	if (device[0] == '/') {
		spec.filename = device;
	} else {
		const char	*s;

		for (s = device; *s; ) {
			char *end;

			while (*s == ',')
				s++;
			if (!strncmp(s, "id=", 3)) {
				spec.vendor = strtoul(s+3, &end, 16);
				if (*end++ != ':')
					goto badspec;
				spec.product  = strtoul(end, &end, 16);
				s = end;
			} else
			if (!strncmp(s, "dev=", 4)) {
				spec.bus = strtoul(s+4, &end, 10);
				if (*end++ != ':')
					goto badspec;
				spec.dev  = strtoul(end, &end, 10);
				s = end;
			} else {
				goto badspec;
			}

			if (*s && *s != ',')
				goto badspec;

		}
	}

	if (ifd_usb_scan(&spec, NULL, NULL) <= 0) {
		ifd_error("%s: no such USB device", device);
		return NULL;
	}

	if (!(uh = usb_open(spec.found))) {
		ifd_error("%s: unable to open USB device: %s",
				device,
				usb_strerror());
		return NULL;
	}

	dev = (ifd_usb_t *) ifd_device_new(spec.found->filename,
			&ifd_usb_ops, sizeof(*dev));
	dev->base.type = IFD_DEVICE_TYPE_USB;
	dev->h = uh;

	return (ifd_device_t *) dev;

badspec:ifd_error("Cannot parse USB device name \"%s\"", device);
	return NULL;
}

/*
 * Initialize USB subsystem
 */
int
ifd_usb_init(void)
{
	static int initialized = 0;

	if (!initialized) {
		initialized = 1;
		usb_init();
	}
	return 0;
}

/*
 * Scan the USB bus for a particular device
 */
int
ifd_usb_scan(struct usb_search_spec *spec,
		int (*func)(struct usb_device *, void *),
		void *call_data)
{
	struct usb_bus	*bus;
	struct usb_device *dev;
	int n;

	usb_find_busses();
	usb_find_devices();

	for (bus = usb_busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			if (spec && !ifd_usb_match(dev, spec))
				continue;
			if (func) {
				n = func(dev, call_data);
				if (n != 0)
					return n;
			} else if (spec) {
				spec->found = dev;
				return 1;
			}
		}
	}

	return 0;
}

/*
 * Match device against search spec
 */
int
ifd_usb_match(struct usb_device *dev, struct usb_search_spec *spec)
{
	struct usb_device_descriptor *d = &dev->descriptor;
	unsigned int num;

	if (spec->bus) {
		num = strtoul(dev->bus->dirname, NULL, 10);
		if (spec->bus != num)
			return 0;
	}
	if (spec->dev) {
		num = strtoul(dev->filename, NULL, 10);
		if (spec->dev != num)
			return 0;
	}

	if ((spec->vendor && d->idVendor != spec->vendor)
	 || (spec->product && d->idProduct != spec->product))
		return 0;
	return 1;
}
