/*
 * eToken driver
 *
 */

#include <ifd/core.h>
#include <ifd/driver.h>
#include <ifd/device.h>
#include <ifd/logging.h>
#include <ifd/config.h>
#include <ifd/error.h>


/*
 * Initialize the device
 */
static int
et_open(ifd_reader_t *reader, const char *device_name)
{
	ifd_device_t *dev;

	reader->name = "Aladdin eToken PRO";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ifd_error("etoken: device %s is not a USB device",
				device_name);
		ifd_device_close(dev);
		return -1;
	}

	reader->device = dev;
	return 0;
}

/*
 * Power up the reader
 */
static int
et_activate(ifd_reader_t *reader)
{
	return 0;
}

static int
et_deactivate(ifd_reader_t *reader)
{
	return 0;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops	etoken_driver = {
	open:		et_open,
//	close:		et_close,
	activate:	et_activate,
	deactivate:	et_deactivate,
//	card_status:	et_card_status,
//	card_reset:	et_card_reset,
//	send:		et_send,
//	recv:		et_recv,
};
/*
 * Initialize this module
 */
void
ifd_init_module(void)
{
	ifd_driver_register("etoken", &etoken_driver);
}
