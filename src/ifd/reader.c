/*
 * IFD reader
 *
 */

#include "internal.h"

int
ifd_set_protocol(ifd_reader_t *reader, int prot)
{
	ifd_driver_t *drv = reader->driver;
	ifd_protocol_t *p;

	if (drv && drv->ops && drv->ops->set_protocol)
		return drv->ops->set_protocol(reader, prot);

	if (prot < 0)
		prot = IFD_PROTOCOL_T0;
	p = ifd_protocol_by_id(prot);
	if (p == 0) {
		ifd_error("unknown protocol id %u\n", prot);
		return -1;
	}

	reader->proto = p;
	return 0;
}

int
ifd_transceive(ifd_reader_t *reader, int dad, ifd_apdu_t *apdu)
{
}
