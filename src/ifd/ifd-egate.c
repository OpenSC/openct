/*
 * e-gate driver
 *
 * Copyright (C) 2003, Chaskiel Grundman <cg2v@andrew.cmu.edu>
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EG_TIMEOUT	1000

/*
 * Initialize the device
 */
static int
eg_open(ifd_reader_t *reader, const char *device_name)
{
	ifd_device_t *dev;

	reader->name = "Schlumberger E-Gate";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("egate: device %s is not a USB device",
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
eg_activate(ifd_reader_t *reader)
{
	return 0;
}

static int
eg_deactivate(ifd_reader_t *reader)
{
	return 0;
}

/*
 * Card status - always present
 */
static int
eg_card_status(ifd_reader_t *reader, int slot, int *status)
{
	*status = IFD_CARD_PRESENT;
	return 0;
}

static int
eg_card_reset(ifd_reader_t *reader, int slot, void *atr, size_t size)
{
	ifd_device_t *dev = reader->device;
	unsigned char	buffer[256];
	int		rc, atrlen;

	/* Reset the device*/
	rc = ifd_usb_control(dev, 0x40, 0x90, 0, 0, NULL, 0, EG_TIMEOUT);
	if (rc < 0)
		goto failed;

	/* Fetch the ATR */
	rc = ifd_usb_control(dev, 0xc0, 0x83, 0, 0, buffer, 0x23, EG_TIMEOUT);
	if (rc <= 0)
		goto failed;

	if (rc > IFD_MAX_ATR_LEN)
		goto failed;

	if (rc > size)
		rc = size;
	atrlen = rc;
	memcpy(atr, buffer, atrlen);

	return atrlen;

failed:	ct_error("egate: failed to activate token");
	return -1;
}

static int
eg_set_protocol(ifd_reader_t *reader, int s, int proto) 
{
     ifd_slot_t      *slot;
     ifd_protocol_t *p;

     ifd_debug(1, "proto=%d", proto);
     if (proto != IFD_PROTOCOL_T0 && proto != IFD_PROTOCOL_TRANSPARENT) {
          ct_error("%s: protocol %d not supported", reader->name, proto);
          return -1;
     }
     slot = &reader->slot[s];
     p = ifd_protocol_new(IFD_PROTOCOL_TRANSPARENT,
                                    reader, slot->dad);
     if (p == NULL) {
          ct_error("%s: internal error", reader->name);
          return -1;
     }
     if (slot->proto) {
                ifd_protocol_free(slot->proto);
                slot->proto = NULL;
     }
     slot->proto=p;
     return 0;
}

static unsigned char eg_status(ifd_reader_t *reader) {
     int rc;
     unsigned char stat;
     while (1) {
        rc=ifd_usb_control(reader->device, 0xc0, 0xa0, 0, 0, &stat, 1, EG_TIMEOUT);
        if (rc != 1)
             return -1;
        stat &= 0xF0;
        if (stat != 0x40) {
             return stat;
        }
        usleep(100);
    }

}
/*
 * Send/receive routines
 */
static int
eg_transparent(ifd_reader_t *reader, int dad, const void *inbuffer, size_t inlen,
              void *outbuffer, size_t outlen)
{
     int rc;
     unsigned char stat;
     ifd_iso_apdu_t iso;
     char cmdbuf[5];

     stat=eg_status(reader);
     if (stat != 0) {
	rc = ifd_usb_control(reader->device, 0x40, 0x90, 0, 0, NULL, 0, EG_TIMEOUT);
	if (rc < 0)
		return -1;
     }
     if (ifd_iso_apdu_parse(inbuffer, inlen, &iso) < 0) 
         return -1;
     memset(cmdbuf,0,5);
     memmove(cmdbuf, inbuffer, inlen < 5 ? inlen : 5);
     rc=ifd_usb_control(reader->device, 0x40, 0x80, 0, 0,
                    (void *) cmdbuf, 5, -1);
     if (rc != 5)
          return -1;
     stat=eg_status(reader);
     if (inlen > 5 && stat == 0x10) {
          rc=ifd_usb_control(reader->device, 0x40, 0x82, 0, 0,
                         (void *) (((char *)inbuffer)+5), iso.lc, -1);
          if (rc != iso.lc) {
               return -1;
          }
          ifd_debug(1, "sent %d bytes of data", iso.lc);
          stat=eg_status(reader);
     }
     if (stat == 0x10) {
          rc=ifd_usb_control(reader->device, 0xc0, 0x81, 0, 0,
                         (void *) outbuffer, iso.le, EG_TIMEOUT);
          if (rc != iso.le) {
               return -1;
          }
          ifd_debug(1, "received %d bytes of data", iso.le);
          stat=eg_status(reader);
     } else
       iso.le=0;
     if (stat != 0x20)
          return -1;
     rc=ifd_usb_control(reader->device, 0xc0, 0x81, 0, 0,
                    (void *) (((char *)outbuffer)+iso.le), 2, EG_TIMEOUT);
     if (rc != 2)
          return -1;
     ifd_debug(1, "returning a %d byte response", iso.le + 2);
     return iso.le+2;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops	egate_driver = {
	open:		eg_open,
//	close:		eg_close,
	activate:	eg_activate,
	deactivate:	eg_deactivate,
	card_status:	eg_card_status,
	card_reset:	eg_card_reset,
	set_protocol:	eg_set_protocol,
	transparent:	eg_transparent,
};
/*
 * Initialize this module
 */
void
ifd_egate_register(void)
{
	ifd_driver_register("egate", &egate_driver);
}
