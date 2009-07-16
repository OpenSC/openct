/*
 * driver for some CCID-compliant devices
 *
 * Copyright 2003, Chaskiel Grundman <cg2v@andrew.cmu.edu>
 *
 * 2005-04-20: Harald Welte <laforge@gnumonks.org>
 * 	Add support for PCMCIA based CCID Device (CardMan 4040)
 *
 * 2005-05-22: Harald Welte <laforge@gnumonks.org>
 * 	Add suport for OmniKey Cardman 5121 RFID extensions
 */

#include "internal.h"
#include "usb-descriptors.h"
#include "atr.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CCID_ERR_ABORTED	0xFF	/* CMD ABORTED */
#define CCID_ERR_ICC_MUTE	0xFE
#define CCID_ERR_XFR_PARITY	0xFD	/* XFR PARITY ERROR */
#define CCID_ERR_OVERRUN	0xFC	/* XFR OVERRUN */
#define CCID_ERR_HW_ERROR	0xFB
#define CCID_ERR_BAD_ATR_TS	0xF8
#define CCID_ERR_BAD_ATR_TCK	0xF7
#define CCID_ERR_PROT_NOSUP	0xF6	/* ICC PROTOCOL NOT SUPPORTED */
#define CCID_ERR_CLASS_NOSUP	0xF5	/* ICC CLASS NOT SUPPORTED */
#define CCID_ERR_BAD_PROC_BYTE	0xF4	/* PROCEDURE BYTE CONFLICT */
#define CCID_ERR_XXX		0xF3	/* DEACTIVATED PROTOCOL (?) */
#define CCID_ERR_BUSY_AUTO_SEQ	0xF2	/* BUSY WITH AUTO SEQUENCE */
#define CCID_ERR_PIN_TIMEOUT	0xF0
#define CCID_ERR_PIN_CANCELED	0xEF
#define CCID_ERR_SLOT_BUSY	0xE0	/* CMD SLOT BUSY */

#define CCID_OFFSET_MSGTYPE	0
#define CCID_OFFSET_LENGTH	1
#define CCID_OFFSET_SLOT	5
#define CCID_OFFSET_SEQ		6

#define CCID_REQ_ABORT		1
#define CCID_REQ_GETCLOCKRATE	2
#define CCID_REQ_GETDATARATE	3

#define CCID_CMD_FIRST		0x60

#define CCID_CMD_ICCPOWERON	0x62
#define CCID_CMD_ICCPOWEROFF	0x63
#define CCID_CMD_GETSLOTSTAT	0x65
#define CCID_CMD_XFRBLOCK	0x6F
#define CCID_CMD_GETPARAMS	0x6C
#define CCID_CMD_RESETPARAMS	0x6D
#define CCID_CMD_SETPARAMS	0x61
#define CCID_CMD_ESCAPE		0x6B
#define CCID_CMD_ICCCLOCK	0x6E
#define CCID_CMD_T0APDU		0x6A
#define CCID_CMD_SECURE		0x69
#define CCID_CMD_MECHANICAL	0x71
#define CCID_CMD_ABORT		0x72
#define CCID_CMD_SET_DR_FREQ	0x73

#define CCID_RESP_DATA		0x80
#define CCID_RESP_SLOTSTAT	0x81
#define CCID_RESP_PARAMS	0x82
#define CCID_RESP_ESCAPE	0x83
#define CCID_RESP_DR_FREQ	0x84

/* maximum sensical size:
 *  10 bytes ccid header + 4 bytes command header +
 *  1 byte Lc + 255 bytes data + 1 byte Le = 271
 */
#define CCID_MAX_MSG_LEN	271

static int msg_expected[] = {
	0,
	CCID_RESP_PARAMS,
	CCID_RESP_DATA,
	CCID_RESP_SLOTSTAT,
	0,
	CCID_RESP_SLOTSTAT,
	0, 0, 0,
	CCID_RESP_DATA,
	CCID_RESP_SLOTSTAT,
	CCID_RESP_ESCAPE,
	CCID_RESP_PARAMS,
	CCID_RESP_PARAMS,
	CCID_RESP_SLOTSTAT,
	CCID_RESP_DATA,
	0,
	CCID_RESP_SLOTSTAT,
	CCID_RESP_SLOTSTAT,
	CCID_RESP_DR_FREQ
};

enum {
	TYPE_APDU,
	TYPE_TPDU,
	TYPE_CHAR
};

/* Some "ccid" devices have non-compliant descriptors. (perhaps their
   design predates the approval of the standard?)
   Attempt to recognize them anyway.
*/
static struct force_parse_device_st {
	unsigned short vendor;
	unsigned short product;
} force_parse_devices[] = {
	{
	0x04e6, 0xe003},	/* SCM SPR 532 */
	{
	0x046a, 0x003e},	/* Cherry SmartTerminal ST-2XXX */
	{
	0x413c, 0x2100},	/* Dell USB Smartcard Keyboard */
	{
	0x04e6, 0x5120},	/* SCM SCR331-DI (NTT) */
	{
	0x04e6, 0x5111},	/* SCM SCR331-DI */
	{
	0x08e6, 0x1359},	/* Verisign secure storage token */
	{
	0x08e6, 0xACE0},	/* Verisign secure token */
	{
	0, 0}
};

#define SUPPORT_T0	0x1
#define SUPPORT_T1	0x2
#define SUPPORT_ESCAPE	0x80

#define SUPPORT_50V	1
#define SUPPORT_33V	2
#define SUPPORT_18V	4
#define AUTO_VOLTAGE	0x80

#define FLAG_NO_PTS		1
#define FLAG_NO_SETPARAM	2
#define FLAG_AUTO_ACTIVATE	4
#define FLAG_AUTO_ATRPARSE	8

#define USB_CCID_DESCRIPTOR_LENGTH 54
struct usb_ccid_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdCCID;
	uint8_t bMaxSlotIndex;
	uint8_t bVoltageSupport;
	uint32_t dwProtocols;
	uint32_t dwDefaultClock;
	uint32_t dwMaximumClock;
	uint8_t bNumClockRatesSupported;
	uint32_t dwDataRate;
	uint32_t dwMaxDataRate;
	uint8_t bNumDataRatesSupported;
	uint32_t dwMaxIFSD;
	uint32_t dwSynchProtocols;
	uint32_t dwMechanical;
	uint32_t dwFeatures;
	uint32_t dwMaxCCIDMessageLength;
	uint8_t bClassGetResponse;
	uint8_t bClassEnvelope;
	uint16_t wLcdLayout;
	uint8_t bPINSupport;
	uint8_t bMaxCCIDBusySlots;
};

static int ccid_parse_descriptor(struct usb_ccid_descriptor *ret,
				 unsigned char *in, size_t inlen)
{
	if (inlen < USB_CCID_DESCRIPTOR_LENGTH)
		return 1;

	ret->bLength = in[0];
	if (ret->bLength < USB_CCID_DESCRIPTOR_LENGTH)
		return 1;
	ret->bDescriptorType = in[1];
	ret->bcdCCID = in[3] << 8 | in[2];
	ret->bMaxSlotIndex = in[4];
	ret->bVoltageSupport = in[5];
	ret->dwProtocols = in[9] << 24 | in[8] << 16 | in[7] << 8 | in[6];
	ret->dwDefaultClock =
	    in[13] << 24 | in[12] << 16 | in[11] << 8 | in[10];
	ret->dwMaximumClock =
	    in[17] << 24 | in[16] << 16 | in[15] << 8 | in[14];
	ret->bNumClockRatesSupported = in[18];
	ret->dwDataRate = in[22] << 24 | in[21] << 16 | in[20] << 8 | in[19];
	ret->dwMaxDataRate = in[26] << 24 | in[25] << 16 | in[24] << 8 | in[23];
	ret->bNumDataRatesSupported = in[27];
	ret->dwMaxIFSD = in[31] << 24 | in[30] << 16 | in[29] << 8 | in[28];
	ret->dwSynchProtocols =
	    in[35] << 24 | in[34] << 16 | in[33] << 8 | in[32];
	ret->dwMechanical = in[39] << 24 | in[38] << 16 | in[37] << 8 | in[36];
	ret->dwFeatures = in[43] << 24 | in[42] << 16 | in[41] << 8 | in[40];
	ret->dwMaxCCIDMessageLength = in[47] << 24 | in[46] << 16 |
	    in[45] << 8 | in[44];
	ret->bClassGetResponse = in[48];
	ret->bClassEnvelope = in[49];
	ret->wLcdLayout = in[51] << 8 | in[50];
	ret->bPINSupport = in[52];
	ret->bMaxCCIDBusySlots = in[53];

	return 0;
}

/*
 * CT status
 */
typedef struct ccid_status {
	int reader_type;
	int usb_interface;
	int proto_support;
	int voltage_support;
	int ifsd;
	int maxmsg;
	int flags;
	unsigned char icc_present[OPENCT_MAX_SLOTS];
	unsigned char icc_proto[OPENCT_MAX_SLOTS];
	unsigned char *sbuf[OPENCT_MAX_SLOTS];
	size_t slen[OPENCT_MAX_SLOTS];
	unsigned char seq;
	int support_events;
	int events_active;
	ifd_usb_capture_t *event_cap;
} ccid_status_t;

static int ccid_checkresponse(void *status, int r)
{
	unsigned char *p = (unsigned char *)status;

	if ((p[7] >> 6 & 3) == 0)
		return 0;

	/* XXX */
	if ((p[7] >> 6 & 3) == 2) {
		/*ct_error("card requests more time"); */
		return -300;
	}

	switch (p[8]) {
	case CCID_ERR_ICC_MUTE:
		return IFD_ERROR_NO_CARD;
	case CCID_ERR_XFR_PARITY:
	case CCID_ERR_OVERRUN:
		return IFD_ERROR_COMM_ERROR;
	case CCID_ERR_BAD_ATR_TS:
	case CCID_ERR_BAD_ATR_TCK:
		return IFD_ERROR_NO_ATR;
	case CCID_ERR_PROT_NOSUP:
	case CCID_ERR_CLASS_NOSUP:
		return IFD_ERROR_INCOMPATIBLE_DEVICE;
	case CCID_ERR_BAD_PROC_BYTE:
		return IFD_ERROR_INVALID_ARG;
	case CCID_ERR_BUSY_AUTO_SEQ:
	case CCID_ERR_SLOT_BUSY:
		return IFD_ERROR_TIMEOUT;
	case CCID_ERR_PIN_TIMEOUT:
		return IFD_ERROR_USER_TIMEOUT;
	case CCID_ERR_PIN_CANCELED:
		return IFD_ERROR_USER_ABORT;
	case CCID_OFFSET_MSGTYPE:
		return IFD_ERROR_NOT_SUPPORTED;
	case CCID_OFFSET_SLOT:
		return IFD_ERROR_INVALID_SLOT;
	}
	return IFD_ERROR_GENERIC;
}

static int ccid_prepare_cmd(ifd_reader_t * reader, unsigned char *out,
			    size_t outsz, int slot, unsigned char cmd,
			    const void *ctl, const void *snd, size_t sendlen)
{
	ccid_status_t *st = (ccid_status_t *) reader->driver_data;
	unsigned char *p = out;

	if (slot >= reader->nslots)
		return IFD_ERROR_INVALID_SLOT;
	if (sendlen + 10 > outsz)	/* this probably means the apdu is larger
					   than the supported MaxMessageSize - 10  */
		return IFD_ERROR_NOT_SUPPORTED;
	*p++ = cmd;
	*p++ = sendlen & 0xFF;
	*p++ = (sendlen >> 8) & 0xFF;
	*p++ = (sendlen >> 16) & 0xFF;
	*p++ = (sendlen >> 24) & 0xFF;
	*p++ = slot;
	*p++ = st->seq++;
	if (ctl)
		memcpy(p, (unsigned char *)ctl, 3);
	else
		memset(p, 0, 3);

	if (sendlen)
		memcpy(&p[3], (unsigned char *)snd, sendlen);
	return sendlen + 10;
}

static int ccid_extract_data(const void *in, size_t inlen, void *out,
			     size_t outlen)
{
	unsigned char dlen[4];
	size_t len;

	if (inlen < 5) {
		ct_error("short response from reader?!");
		return IFD_ERROR_BUFFER_TOO_SMALL;
	}

	memcpy(dlen, ((char *)in) + 1, 4);
	len = dlen[0] | dlen[1] << 8 | dlen[2] << 16 | dlen[3] << 24;
	if (len == 0)
		return 0;
	if (inlen < len + 10) {
		ct_error("truncated response from reader");
		return IFD_ERROR_BUFFER_TOO_SMALL;
	}
	if (outlen < len) {
		ct_error("user buffer too small (%d < %d)", outlen, len);
		return IFD_ERROR_BUFFER_TOO_SMALL;
	}
	memcpy(out, ((char *)in) + 10, len);
	return len;
}

static int ccid_command(ifd_reader_t * reader, const unsigned char *cmd,
			size_t cmd_len, unsigned char *res, size_t res_len)
{
	int rc;
	size_t req_len;

	if (!cmd_len || !res_len) {
		ct_error("missing parameters to ccid_command");
		return IFD_ERROR_INVALID_ARG;
	}
	req_len = res_len;
	if (ct_config.debug >= 3)
		ifd_debug(3, "sending:%s", ct_hexdump(cmd, cmd_len));

	rc = ifd_device_send(reader->device, cmd, cmd_len);
	if (rc < 0) {
		ifd_debug(1, "ifd_device_send failed %d", rc);
		return rc;
	}
	while (1) {
		rc = ifd_device_recv(reader->device, res, req_len, 10000);
		if (rc < 0)
			return rc;
		if (rc == 0) {
			ct_error("zero length response from reader?!");
			return IFD_ERROR_GENERIC;
		}
		if (ct_config.debug >= 3)
			ifd_debug(3, "received:%s", ct_hexdump(res, rc));

		if (rc < 9) {
			return IFD_ERROR_GENERIC;
		}
		if (cmd[CCID_OFFSET_SLOT] == res[CCID_OFFSET_SLOT] &&
		    cmd[CCID_OFFSET_SEQ] == res[CCID_OFFSET_SEQ]) {
			res_len = rc;
			rc = ccid_checkresponse(res, res_len);
			if (rc == -300) {
				continue;
			}
			if (rc < 0)
				return rc;
			break;
		}

	}
	return res_len;
}

static int ccid_simple_rcommand(ifd_reader_t * reader, int slot, int cmd,
				void *ctl, void *res, size_t res_len)
{
	ccid_status_t *st = reader->driver_data;
	unsigned char cmdbuf[10];
	unsigned char resbuf[CCID_MAX_MSG_LEN + 1];
	int r;

	r = ccid_prepare_cmd(reader, cmdbuf, 10, slot, cmd, ctl, NULL, 0);
	if (r < 0)
		return r;

	r = ccid_command(reader, cmdbuf, 10, resbuf, st->maxmsg);
	if (r < 0)
		return r;
	if (resbuf[0] != msg_expected[cmd - CCID_CMD_FIRST]) {
		ct_error("Received a message of type x%02x instead of x%02x",
			 resbuf[0], msg_expected[cmd - CCID_CMD_FIRST]);
		return -1;
	}

	if (res_len)
		r = ccid_extract_data(&resbuf, r, res, res_len);
	return r;
}

static int ccid_simple_wcommand(ifd_reader_t * reader, int slot, int cmd,
				void *ctl, void *data, size_t data_len)
{
	ccid_status_t *st = reader->driver_data;
	unsigned char cmdbuf[CCID_MAX_MSG_LEN + 1];
	unsigned char resbuf[CCID_MAX_MSG_LEN + 1];
	int r;

	r = ccid_prepare_cmd(reader, cmdbuf, st->maxmsg, slot, cmd, ctl, data,
			     data_len);
	if (r < 0)
		return r;

	r = ccid_command(reader, cmdbuf, r, resbuf, st->maxmsg);
	if (r < 0)
		return r;
	if (resbuf[0] != msg_expected[cmd - CCID_CMD_FIRST]) {
		ct_error("Received a message of type x%02x instead of x%02x",
			 resbuf[0], msg_expected[cmd - CCID_CMD_FIRST]);
		return -1;
	}

	return r;
}

#ifdef notyet
static int ccid_abort(ifd_reader_t * reader, int slot)
{
	ccid_status_t *st = (ccid_status_t *) reader->driver_data;

	int r;
	if (ifd_device_type(reader->device) == IFD_DEVICE_TYPE_USB) {
		r = ifd_usb_control(reader->device, 0x21
				    /*USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE */
				    ,
				    CCID_REQ_ABORT, st->seq << 8 | slot,
				    st->usb_interface, NULL, 0, 10000);
		if (r < 0)
			return r;
		r = ccid_simple_wcommand(reader, slot, CCID_CMD_ABORT, NULL,
					 NULL, 0);
		return r;
	} else
	    if (ifd_device_type(reader->device == IFD_DEVICE_TYPE_PCMCIA_BLOCK))
	{
		/* FIXME */
		return IFD_ERROR_NOT_SUPPORTED;
	}
	return IFD_ERROR_NOT_SUPPORTED;
}
#endif

static int ccid_exchange(ifd_reader_t * reader, int slot,
			 const void *sbuf, size_t slen, void *rbuf, size_t rlen)
{
	ccid_status_t *st = reader->driver_data;
	unsigned char sendbuf[CCID_MAX_MSG_LEN + 1];
	unsigned char recvbuf[CCID_MAX_MSG_LEN + 1];
	int r;
	unsigned char ctlbuf[3], *ctlptr=NULL;

	ctlptr=NULL;
	if (st->reader_type == TYPE_CHAR) {
		ctlbuf[0] = 0;
		ctlbuf[1] = rlen & 0xff;
		ctlbuf[2] = (rlen >> 8) & 0xff;
		ctlptr = ctlbuf;
	}

	r = ccid_prepare_cmd(reader, sendbuf, st->maxmsg,
			     slot, CCID_CMD_XFRBLOCK, ctlptr, sbuf, slen);
	if (r < 0)
		return r;

	r = ccid_command(reader, &sendbuf[0], r, recvbuf, st->maxmsg);
	if (r < 0)
		return r;
	return ccid_extract_data(&recvbuf, r, rbuf, rlen);
}

static int ccid_open_usb(ifd_device_t * dev, ifd_reader_t * reader)
{
	ccid_status_t *st;
	ifd_device_params_t params;
	int r, i, c, ifc, alt, num_alt;
	struct ifd_usb_device_descriptor de;
	struct ifd_usb_config_descriptor conf;
	struct ifd_usb_interface_descriptor *intf;
	struct usb_ccid_descriptor ccid = { 0 };
	int force_parse;
	struct force_parse_device_st *force_dev;
	unsigned char *_class;
	unsigned char *p;
	int support_events = 0;

#define PCSCLITE_FILE "/var/run/pcscd/pcscd.comm"
	struct stat buf;

	/* give priority to pcsc-lite for CCID devices */
	if (0 == stat(PCSCLITE_FILE, &buf))
		sleep(3);

	if (ifd_usb_get_device(dev, &de)) {
		ct_error("ccid: device descriptor not found");
		ifd_device_close(dev);
		return -1;
	}

	force_parse = 0;
	for (force_dev = force_parse_devices; force_dev->vendor; force_dev++) {
		if (de.idVendor == force_dev->vendor &&
		    de.idProduct == force_dev->product) {
			force_parse = 1;
			break;
		}
	}

	intf = NULL;
	p = NULL;
	r = i = 0;
	memset(&conf, 0, sizeof(conf));
	for (c = 0; c < de.bNumConfigurations; c++) {
		if (ifd_usb_get_config(dev, c, &conf)) {
			ct_error("ccid: config descriptor %d not found", c);
			continue;
		}
		if (!conf.interface)
			continue;

		for (ifc = 0; ifc < conf.bNumInterfaces; ifc++) {
			num_alt = conf.interface[ifc].num_altsetting;
			for (alt = 0; alt < num_alt; alt++) {
				int typeok = 0;
				int ok = 0;
				intf = &conf.interface[ifc].altsetting[alt];
				if (intf->bInterfaceClass == 0xb ||
				    intf->bInterfaceSubClass == 0 ||
				    intf->bInterfaceProtocol == 0)
					typeok = 1;
				/* accept class 0xFF if force_parse != 0 */
				if (force_parse
				    && intf->bInterfaceClass == 0xff)
					typeok = 1;
				if (intf->bNumEndpoints < 2
				    || intf->bNumEndpoints > 3)
					typeok = 0;
				if (typeok == 0) {
					intf = NULL;
					continue;
				}
				if (intf->bNumEndpoints == 2) {
					params.usb.ep_intr = 0;
					ok |= 4;
				}
				if (intf->bNumEndpoints == 3) {
					support_events = 1;
				}
				for (i = 0; i < intf->bNumEndpoints; i++) {
					if (((intf->endpoint[i].bmAttributes &
					      IFD_USB_ENDPOINT_TYPE_MASK) ==
					     IFD_USB_ENDPOINT_TYPE_BULK) &&
					    (intf->endpoint[i].
					     bEndpointAddress &
					     IFD_USB_ENDPOINT_DIR_MASK) ==
					    IFD_USB_ENDPOINT_OUT) {
						ok |= 1;
						params.usb.ep_o =
						    intf->endpoint[i].
						    bEndpointAddress;
					}
					if (((intf->endpoint[i].bmAttributes &
					      IFD_USB_ENDPOINT_TYPE_MASK) ==
					     IFD_USB_ENDPOINT_TYPE_BULK) &&
					    (intf->endpoint[i].
					     bEndpointAddress &
					     IFD_USB_ENDPOINT_DIR_MASK) ==
					    IFD_USB_ENDPOINT_IN) {
						ok |= 2;
						params.usb.ep_i =
						    intf->endpoint[i].
						    bEndpointAddress;
					}
					if (((intf->endpoint[i].bmAttributes &
					      IFD_USB_ENDPOINT_TYPE_MASK) ==
					     IFD_USB_ENDPOINT_TYPE_INTERRUPT) &&
					    (intf->endpoint[i].
					     bEndpointAddress &
					     IFD_USB_ENDPOINT_DIR_MASK) ==
					    IFD_USB_ENDPOINT_IN) {
						ok |= 4;
						params.usb.ep_intr =
						    intf->endpoint[i].
						    bEndpointAddress;
					}
				}
				if (ok == 7)
					break;
				intf = NULL;
			}
			if (!intf)
				continue;
			if (!intf->extralen) {
				int i;
				/* Buggy O2 Micro CCID SC Reader has zero extra len at interface level but not endpoint descriptor.
				 * Patch the interface level field and proceed.
				 * ProdID 7762 reader is in Dell Latitude D620 and 7772 is in D630.
				 */
        			if( de.idVendor == 0x0b97 && (de.idProduct == 0x7762 || de.idProduct == 0x7772) )  {
					ct_error("ccid: extra len is zero, patching O2 Micro support");
					for (i=0; i<intf->bNumEndpoints; i++)  {
						/* find the extra[] array */
						if( intf->endpoint[i].extralen == 54 )  {
							/* get the extra[] from the endpoint */
							intf->extralen = 54;
							/* avoid double free on close, allocate here */
							intf->extra = malloc(54);
							if( intf->extra )  {
								memcpy( intf->extra, intf->endpoint[i].extra, 54 );
								break;
							}
							else  {
								intf = NULL;
								continue;
							}
						}
					}
				}
				else  {
					intf = NULL;
					ct_error("ccid: extra len is zero, continuing");
					continue;
				}
			}

			r = intf->extralen;
			_class = intf->extra;
			i = 0;
			p = _class + i;
			/* 0x21 == USB_TYPE_CLASS | 0x1 */
			/* accept descriptor type 0xFF if force_parse != 0 */
			while (i < r && p[0] > 2 &&
			       (p[1] != 0x21 &&
				(force_parse == 0 || p[1] != 0xff))) {
				i += p[0];
				p = _class + i;
			}
			if (i >= r || p[0] < 2 ||
			    (p[1] != 0x21 &&
			     (force_parse == 0 || p[1] != 0xff))) {
				intf = NULL;
			}
			if (intf)
				break;
		}
		if (intf)
			break;
		ifd_usb_free_configuration(&conf);
	}

	if (!intf) {
		ct_error("ccid: class descriptor not found");
		ifd_device_close(dev);
		return -1;
	}

	/* Don't touch the device configuration if it's the one and only.
	 * The reason for this is that in multi purpose devices (eg keyboards
	 * with an integrated reader) some interfaces might already be in use.
	 * Trying to change the device configuration in such a case will produce
	 * this kernel message on Linux:
	 * usbfs: interface X claimed while 'ifdhandler' sets config #N
	 * and often the call will fail with EBUSY.
	 * Actually a bit better fix could be implemented in usb_set_params.
	 * The code there should check if the device is already in the requested
	 * configuration and thus if the change is needed after all. However 
	 * implementing this would need a bunch of new sysdep functions to
	 * determine the current device configuration.
	 * So IMHO we should postpone this hard work until some USB device
	 * surfaces that really needs such magic.
	 *
	 * Antti Andreimann <Antti.Andreimann@mail.ee> Thu Feb 2 2006
	 */
	if (de.bNumConfigurations > 1)
		params.usb.configuration = conf.bConfigurationValue;
	else
		params.usb.configuration = -1;

	params.usb.interface = intf->bInterfaceNumber;
	if (num_alt > 1 || intf->bAlternateSetting > 0)
		params.usb.altsetting = intf->bAlternateSetting;
	else
		params.usb.altsetting = -1;

	r = ccid_parse_descriptor(&ccid, p, r - i);
	ifd_usb_free_configuration(&conf);
	if (r) {
		ct_error("ccid: class descriptor is invalid");
		ifd_device_close(dev);
		return -1;
	}

	if (ccid.bcdCCID != 0x100 && ccid.bcdCCID != 0x110) {
		ct_error("ccid: unknown ccid version %02x.%02x supported only 1.00, 1.10",
			(ccid.bcdCCID >> 8) & 0xff,
			(ccid.bcdCCID >> 0) & 0xff
		);
		ifd_device_close(dev);
		return -1;
	}

	if ((st = (ccid_status_t *) calloc(1, sizeof(*st))) == NULL) {
		ct_error("out of memory");
		return IFD_ERROR_NO_MEMORY;
	}

	st->usb_interface = intf->bInterfaceNumber;
	memset(st->icc_present, -1, OPENCT_MAX_SLOTS);
	st->voltage_support = ccid.bVoltageSupport & 0x7;
	st->proto_support = ccid.dwProtocols;
	if ((st->proto_support & 3) == 0) {
		ct_error
		    ("ccid: device does not provide any supported protocols");
		free(st);
		ifd_device_close(dev);
		return -1;
	}

/* "When a CCID doesn't declare the values 00000010h and 00000020h, the
 * frequency or the baud rate must be made via manufacturer proprietary
 * PC_to_RDR_Escape command." - ccid class specification v1.00
 * 
 * "The value of the lower word (=0840) indicates that the host will only
 * send requests that are valid for the USB-ICC." - ISO/IEC 7816-12:2005
 * 7.2/Table 8
 */
	if ((ccid.dwFeatures & 0xFFFF) != 0x0840
	    && ~ccid.dwFeatures & (0x10 | 0x20)) {
		ct_error("ccid: required card initialization features missing");
		free(st);
		ifd_device_close(dev);
		return -1;
	}

	st->reader_type = TYPE_CHAR;

	if (ccid.dwFeatures & 0x10000) {
		st->reader_type = TYPE_TPDU;
	} else if (ccid.dwFeatures & 0x60000) {
		st->reader_type = TYPE_APDU;
	}
	if (ccid.dwFeatures & 0x2)
		st->flags |= FLAG_AUTO_ATRPARSE;
	if (ccid.dwFeatures & 0x4)
		st->flags |= FLAG_AUTO_ACTIVATE;
	if (ccid.dwFeatures & 0x8)
		st->voltage_support |= AUTO_VOLTAGE;
	if (ccid.dwFeatures & 0x40)
		st->flags |= FLAG_NO_PTS | FLAG_NO_SETPARAM;
	if (ccid.dwFeatures & 0x80)
		st->flags |= FLAG_NO_PTS;
	st->ifsd = ccid.dwMaxIFSD;

	/* must provide AUTO or at least one of 5/3.3/1.8 */
	if (st->voltage_support == 0) {
		ct_error
		    ("ccid: device does not provide any supported voltages");
		free(st);
		ifd_device_close(dev);
		return -1;
	}

	if (ccid.dwMaxCCIDMessageLength > CCID_MAX_MSG_LEN) {
		st->maxmsg = CCID_MAX_MSG_LEN;
	} else {
		st->maxmsg = ccid.dwMaxCCIDMessageLength;
	}

	reader->driver_data = st;
	reader->device = dev;
	reader->nslots = ccid.bMaxSlotIndex + 1;

	if (ifd_device_set_parameters(dev, &params) < 0) {
		ifd_device_close(dev);
		return -1;
	}
	if (de.idVendor == 0x08e6 && de.idProduct == 0x3437) {
		unsigned char settpdu[] = { 0xA0, 0x1 };
		unsigned char setiso[] = { 0x1F, 0x1 };
		r = ccid_simple_wcommand(reader, 0, CCID_CMD_ESCAPE, NULL,
					 settpdu, 2);
		if (r < 0) {
			ct_error("ccid: cannot set GemPlus TPDU mode");
			ifd_device_close(dev);
			return -1;
		}
		r = ccid_simple_wcommand(reader, 0, CCID_CMD_ESCAPE, NULL,
					 setiso, 2);
		if (r < 0) {
			ct_error("ccid: cannot set GemPlus ISO APDU mode");
			ifd_device_close(dev);
			return -1;
		}
		st->reader_type = TYPE_TPDU;
	}

	if (de.idVendor == 0x076b && de.idProduct == 0x5121) {
		/* special handling of RFID part of OmniKey 5121 */
		reader->nslots++;	/* one virtual slot for RFID escape */
		st->proto_support |= SUPPORT_ESCAPE;
	}

	st->support_events = support_events;

	ifd_debug(3, "Accepted %04x:%04x with features 0x%x and protocols 0x%x events=%d", de.idVendor, de.idProduct, ccid.dwFeatures, ccid.dwProtocols, st->support_events);
	return 0;
}

static int ccid_open_pcmcia_block(ifd_device_t * dev, ifd_reader_t * reader)
{
	ccid_status_t *st;
	/* unfortunately I know of no sanity checks that we could do with the
	 * hardware to confirm we're actually accessing a real pcmcia/ccid
	 * device -HW */

	if ((st = (ccid_status_t *) calloc(1, sizeof(*st))) == NULL)
		return IFD_ERROR_NO_MEMORY;

	/* setup fake ccid_status_t based on totally guessed values */
	memset(st->icc_present, -1, OPENCT_MAX_SLOTS);
	st->voltage_support = 0x7;
	st->proto_support = SUPPORT_T0 | SUPPORT_T1;
	st->reader_type = TYPE_APDU;
	st->voltage_support |= AUTO_VOLTAGE;
	st->ifsd = 1;		/* ? */
	st->maxmsg = CCID_MAX_MSG_LEN;
	st->flags = FLAG_AUTO_ATRPARSE | FLAG_NO_PTS;	/*|FLAG_NO_SETPARAM; */

	reader->driver_data = st;
	reader->device = dev;
	reader->nslots = 1;

	return 0;
}

/*
 * Initialize the device
 */
static int ccid_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	reader->name = "CCID Compatible";
	if (!(dev = ifd_device_open(device_name)))
		return -1;
	if (ifd_device_type(dev) == IFD_DEVICE_TYPE_USB)
		return ccid_open_usb(dev, reader);
	else if (ifd_device_type(dev) == IFD_DEVICE_TYPE_PCMCIA_BLOCK)
		return ccid_open_pcmcia_block(dev, reader);
	else {
		ct_error("ccid: device %s is not a supported device",
			 device_name);
		ifd_device_close(dev);
		return -1;
	}
}

/*
 * Close the device
 */
static int ccid_close(ifd_reader_t * reader)
{
	ccid_status_t *st = (ccid_status_t *) reader->driver_data;
	
	ifd_debug(1, "called.");

	if (st->event_cap != NULL) {
		ifd_usb_end_capture(reader->device, st->event_cap);
		st->event_cap = NULL;
	}

	return 0;
}

static int ccid_activate(ifd_reader_t * reader)
{
	ifd_debug(1, "called.");
	return 0;
}

static int ccid_deactivate(ifd_reader_t * reader)
{
	ifd_debug(1, "called.");
	return 0;
}

static int ccid_card_status(ifd_reader_t * reader, int slot, int *status)
{
	ccid_status_t *st = (ccid_status_t *) reader->driver_data;
	int r, stat;
	unsigned char ret[20];
	unsigned char cmdbuf[10];

	if (ifd_device_type(reader->device) == IFD_DEVICE_TYPE_USB &&
	    reader->device->settings.usb.ep_intr) {
		ifd_usb_capture_t *cap;
		int any = 0;
		int i, j, bits;

		if (st->proto_support & SUPPORT_ESCAPE
		    && slot == reader->nslots - 1) {
			ifd_debug(1,
				  "virtual escape slot, setting card present\n");
			*status = IFD_CARD_PRESENT;
			return 0;
		}

		i = 1 + (slot / 4);
		j = 2 * (slot % 4);
		stat = 0;

		r = ifd_usb_begin_capture(reader->device,
					  IFD_USB_URB_TYPE_INTERRUPT,
					  reader->device->settings.usb.ep_intr,
					  8, &cap);
		if (r < 0) {
			ct_error("ccid: begin capture: %d", r);
			return r;
		}
		/* read any bufferred interrupt pipe messages */
		while (1) {
			r = ifd_usb_capture(reader->device, cap, ret, 8, 100);
			if (r < 0)
				break;
			if (ret[0] != 0x50)
				continue;
			ifd_debug(3, "status received:%s", ct_hexdump(ret, r));
			bits = (ret[i] >> j) & 0x3;
			if (bits & 2)
				stat |= IFD_CARD_STATUS_CHANGED;
			if (bits & 1)
				stat |= IFD_CARD_PRESENT;
			else
				stat &= ~IFD_CARD_PRESENT;
			any = 1;
		}
		ifd_usb_end_capture(reader->device, cap);
		if (any) {
			ifd_debug(1, "polled result: %d", stat);
			st->icc_present[slot] = stat & IFD_CARD_PRESENT;
			*status = stat;
			return 0;
		}
		if (st->icc_present[slot] != 0xFF) {
			ifd_debug(1, "cached result: %d",
				  st->icc_present[slot]);
			*status = st->icc_present[slot];
			return 0;
		}
	}

	r = ccid_prepare_cmd(reader, cmdbuf, 10, 0, CCID_CMD_GETSLOTSTAT,
			     NULL, NULL, 0);
	if (r < 0)
		return r;
	r = ccid_command(reader, cmdbuf, 10, ret, 10);
	if (r == IFD_ERROR_NO_CARD) {
		stat = 0;
	}
	else if (r < 0) {
		return r;
	}
	else {
		switch (ret[7] & 3) {
		case 2:
			stat = 0;
			break;
		default:
			stat = IFD_CARD_PRESENT;
			break;
		}
	}

	ifd_debug(1, "probed result: %d", IFD_CARD_STATUS_CHANGED | stat);

	*status = IFD_CARD_STATUS_CHANGED | stat;
	st->icc_present[slot] = stat;
	return 0;
}

static int ccid_set_protocol(ifd_reader_t * reader, int s, int proto);

/*
 * Reset
 */
static int
ccid_card_reset(ifd_reader_t * reader, int slot, void *atr, size_t size)
{
	ccid_status_t *st = (ccid_status_t *) reader->driver_data;
	unsigned char buffer[IFD_MAX_ATR_LEN];
	char ctlbuf[3];
	int n, r, i;
	int status;

	r = ccid_card_status(reader, slot, &status);
	if (r < 0)
		return r;
	if (!(status & IFD_CARD_PRESENT))
		return IFD_ERROR_NO_CARD;

	if (st->proto_support & SUPPORT_ESCAPE && slot == reader->nslots - 1) {
		ifd_debug(1, "slot: %d, setting atr to 0xff", slot);
		*((char *)atr) = 0xff;
		ccid_set_protocol(reader, slot, IFD_PROTOCOL_ESCAPE);
		return 1;
	}
	memset(ctlbuf, 0, 3);

	n = -1;
	if (st->voltage_support & AUTO_VOLTAGE
	    || st->flags & FLAG_AUTO_ACTIVATE) {
		ifd_debug(1, "called. powering on with auto voltage selection");
		n = ccid_simple_rcommand(reader, slot, CCID_CMD_ICCPOWERON,
					 ctlbuf, buffer, IFD_MAX_ATR_LEN);
	}
	if (n < 0 && (st->voltage_support & AUTO_VOLTAGE) == 0) {
		ifd_debug(1,
			  "called. powering on with manual voltage selection");
		for (i = 1; i <= 3; i++) {
			if ((st->voltage_support & (1 << (i - 1))) == 0)
				continue;
			ifd_debug(3, "Trying voltage parameter %d", i);
			ctlbuf[0] = i;
			n = ccid_simple_rcommand(reader, slot,
						 CCID_CMD_ICCPOWERON, ctlbuf,
						 buffer, IFD_MAX_ATR_LEN);
			if (n > 0)
				break;
		}
	}
	if (n < 0)
		return n;
	if (n > size)
		return IFD_ERROR_BUFFER_TOO_SMALL;
	memcpy(atr, buffer, n);

	return n;
}

static int ccid_set_protocol(ifd_reader_t * reader, int s, int proto)
{
	ccid_status_t *st = (ccid_status_t *) reader->driver_data;
	unsigned char parambuf[17], ctl[3];
	ifd_slot_t *slot;
	ifd_protocol_t *p;
	ifd_atr_info_t atr_info;
	int r, paramlen;

	slot = &reader->slot[s];

	/* If we support RFID escaping, we only allow ESCAPE protocol
	 * at the last (== virtual) slot */
	if ((st->proto_support & SUPPORT_ESCAPE)
	    && (proto != IFD_PROTOCOL_ESCAPE)
	    && (s == reader->nslots - 1)) {
		ct_error("reader doesn't support this protocol at this slot\n");
		return IFD_ERROR_NOT_SUPPORTED;
	}

	switch (proto) {
	case IFD_PROTOCOL_T0:
		if (!(st->proto_support & SUPPORT_T0)) {
			ct_error("reader does not support this protocol");
			return IFD_ERROR_NOT_SUPPORTED;
		}
		break;
	case IFD_PROTOCOL_T1:
		if (!(st->proto_support & SUPPORT_T1)) {
			ct_error("reader does not support this protocol");
			return IFD_ERROR_NOT_SUPPORTED;
		}
		break;
	case IFD_PROTOCOL_ESCAPE:
		/* virtual "escape" fallthrough protocol for stacking RFID
		 * protocol stack on top of openct */
		if (!(st->proto_support & SUPPORT_ESCAPE)) {
			ct_error("reader does not support this protocol");
			return IFD_ERROR_NOT_SUPPORTED;
		}
		if (s != reader->nslots - 1) {
			ct_error
			    ("reader doesn't support this protocol at this slot");
			return IFD_ERROR_NOT_SUPPORTED;
		}
		p = ifd_protocol_new(IFD_PROTOCOL_ESCAPE, reader, slot->dad);
		if (!p) {
			ct_error("%s: internal error", reader->name);
			return -1;
		}
		if (slot->proto) {
			ifd_protocol_free(slot->proto);
			slot->proto = NULL;
		}
		slot->proto = p;
		st->icc_proto[s] = proto;
		ifd_debug(1, "set protocol to ESCAPE\n");
		return 0;
		break;
	default:
		ct_error("protocol unknown");
		return IFD_ERROR_NOT_SUPPORTED;
	}

	if (st->reader_type == TYPE_APDU) {
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
		slot->proto = p;
		st->icc_proto[s] = proto;
		return 0;
	}

	r = ifd_atr_parse(&atr_info, slot->atr, slot->atr_len);
	if (r < 0) {
		ct_error("%s: Bad ATR", reader->name);
		return r;
	}
	/* ccid doesn't have a parameter for this */
	if (atr_info.TC[0] == 255)
		atr_info.TC[0] = -1;

	/*
	 * guard time increase must precede PTS
	 * we don't need to do this separate step if
	 * a) the ccid does automatic parameter setting, or
	 * b) the ccid parses the atr itself, or
	 * c) the ccid does pts itself when we set parameters, or
	 * d) the ICC does not require extra guard time
	 * In all but the first case, we'll do parameter setting later, 
	 * so fetch the default parameters now.
	 */
	if ((st->flags & FLAG_NO_SETPARAM) == 0) {
		memset(parambuf, 0, sizeof(parambuf));
		memset(ctl, 0, 3);
		r = ccid_simple_rcommand(reader, s, CCID_CMD_GETPARAMS,
			ctl, parambuf, 7);
		if (r < 0)
			return r;
		if (proto == IFD_PROTOCOL_T0) {
			paramlen = 5;
			ctl[0] = 0;
		}
		else {
			paramlen = 7;
			ctl[0] = 1;
		}
		if ((st->flags & (FLAG_NO_PTS | FLAG_AUTO_ATRPARSE)) == 0 &&
			atr_info.TC[0] != -1) {

			parambuf[2] = atr_info.TC[0];
			r = ccid_simple_wcommand(reader, s, CCID_CMD_SETPARAMS,
				ctl, parambuf, paramlen);
			if (r < 0)
				return r;
		}
	}

	if ((st->flags & FLAG_NO_PTS) == 0 &&
		(proto == IFD_PROTOCOL_T1 || atr_info.TA[0] != -1)) {

		unsigned char pts[7], ptsret[7];
		int ptslen;

		ptslen = ifd_build_pts(&atr_info, proto, pts, sizeof(pts));
		if (ptslen < 0) {
			ct_error("%s: Could not perform PTS: %s", reader->name,
				 ct_strerror(r));
			return ptslen;
		}
		r = ccid_exchange(reader, s, pts, ptslen, ptsret,
				  ptslen);
		if (r < 0)
			return r;
		r = ifd_verify_pts(&atr_info, proto, ptsret, r);
		if (r) {
			ct_error("%s: Bad PTS response", reader->name);
			return r;
		}
	}

	if ((st->flags & FLAG_NO_SETPARAM) == 0 &&
		((st->flags & FLAG_AUTO_ATRPARSE) == 0 ||
		proto != IFD_PROTOCOL_T0)) {

		/* if FLAG_AUTO_ATRPARSE, only set the protocol. */
		if ((st->flags & FLAG_AUTO_ATRPARSE) == 0) {
			if (proto == IFD_PROTOCOL_T0) {
				/* TA1 -> Fi | Di */
				if (atr_info.TA[0] != -1)
					parambuf[0] = atr_info.TA[0];
				/* TC1 -> N */
				if (atr_info.TC[0] != -1)
					parambuf[2] = atr_info.TC[0];
				/* TC2 -> WI */
				if (atr_info.TC[1] != -1)
					parambuf[3] = atr_info.TC[1];
				/* TA3 -> clock stop parameter */
				/* XXX check for IFD clock stop support */
				if (atr_info.TA[2] != -1)
					parambuf[4] = atr_info.TA[2] >> 6;
			}
			else if (proto == IFD_PROTOCOL_T1) {
				if (atr_info.TA[0] != -1)
					parambuf[0] = atr_info.TA[0];
				parambuf[1] = 0x10;
				/* TC3 -> LRC/CRC selection */
				if (atr_info.TC[2] == 1)
					parambuf[1] |= 0x1;
				else
					parambuf[1] &= 0xfe;
				/* TC1 -> N */
				if (atr_info.TC[0] != -1)
					parambuf[2] = atr_info.TC[0];
				/* atr_info->TB3 -> BWI/CWI */
				if (atr_info.TB[2] != -1)
					parambuf[3] = atr_info.TB[2];
				/* TA3 -> IFSC */
				if (atr_info.TA[2] != -1)
					parambuf[5] = atr_info.TA[2];
				/*
				 * XXX CCID supports setting up clock stop for T=1, but the
				 * T=1 ATR does not define a clock-stop byte.
				 */
			}
		}
		r = ccid_simple_wcommand(reader, s, CCID_CMD_SETPARAMS, ctl,
			parambuf, paramlen);
		if (r < 0)
			return r;
	}

	memset(&parambuf[r], 0, sizeof(parambuf) - r);
	if (proto == IFD_PROTOCOL_T0) {
		if (st->reader_type == TYPE_CHAR) {
			p = ifd_protocol_new(proto,
					     reader, slot->dad);
		} else {
			p = ifd_protocol_new(IFD_PROTOCOL_TRANSPARENT,
					     reader, slot->dad);
		}
	} else {
		p = ifd_protocol_new(proto, reader, slot->dad);
		if (p) {
			/* guessing that ifsc is limited by ifsd */
			if (atr_info.TA[2] != -1)
				ifd_protocol_set_parameter(p,
							   IFD_PROTOCOL_T1_IFSC,
							   atr_info.TA[2] >
							   st->ifsd ? st->
							   ifsd : atr_info.
							   TA[2]);
			ifd_protocol_set_parameter(p, IFD_PROTOCOL_T1_IFSD,
						   st->ifsd);
			if (atr_info.TC[2] == 1)
				ifd_protocol_set_parameter(p,
							   IFD_PROTOCOL_T1_CHECKSUM_CRC,
							   0);
		}
	}
	if (p == NULL) {
		ct_error("%s: internal error", reader->name);
		return -1;
	}
	/* ccid_recv needs to know the exact expected data length */
	if (st->reader_type == TYPE_CHAR)
		ifd_protocol_set_parameter(p, IFD_PROTOCOL_BLOCK_ORIENTED, 0);
	if (slot->proto) {
		ifd_protocol_free(slot->proto);
		slot->proto = NULL;
	}
	slot->proto = p;
	st->icc_proto[s] = proto;
	return 0;
}

static int ccid_escape(ifd_reader_t * reader, int slot, const void *sbuf,
		       size_t slen, void *rbuf, size_t rlen)
{
	unsigned char sendbuf[CCID_MAX_MSG_LEN];
	unsigned char recvbuf[CCID_MAX_MSG_LEN];
	int r;

	ifd_debug(1, "slot: %d, slen %d, rlen %d", slot, slen, rlen);

	r = ccid_prepare_cmd(reader, sendbuf, sizeof(sendbuf), slot,
			     CCID_CMD_ESCAPE, NULL, sbuf, slen);
	if (r < 0)
		return r;

	r = ccid_command(reader, &sendbuf[0], r, recvbuf, sizeof(recvbuf));
	if (r < 0)
		return r;

	return ccid_extract_data(&recvbuf, r, rbuf, rlen);
}

static int
ccid_transparent(ifd_reader_t * reader, int slot,
		 const void *sbuf, size_t slen, void *rbuf, size_t rlen)
{
	ccid_status_t *st = (ccid_status_t *) reader->driver_data;

	ifd_debug(1, "called.");
	if (st->reader_type == TYPE_APDU ||
	    (st->reader_type == TYPE_TPDU &&
	     st->icc_proto[slot] == IFD_PROTOCOL_T0))
		return ccid_exchange(reader, slot, sbuf, slen, rbuf, rlen);
	return IFD_ERROR_NOT_SUPPORTED;
}

static int ccid_send(ifd_reader_t * reader, unsigned int dad,
		     const unsigned char *buffer, size_t len)
{
	ccid_status_t *st = (ccid_status_t *) reader->driver_data;
	unsigned char *apdu;

	ifd_debug(1, "called.");
	if (st->sbuf[dad]) {
		free(st->sbuf[dad]);
		st->sbuf[dad] = NULL;
		st->slen[dad] = 0;
	}

	apdu = (unsigned char *)calloc(1, len);
	if (!apdu) {
		ct_error("out of memory");
		return IFD_ERROR_NO_MEMORY;
	}
	memcpy(apdu, buffer, len);
	st->sbuf[dad] = apdu;
	st->slen[dad] = len;
	return 0;
}

static int ccid_recv(ifd_reader_t * reader, unsigned int dad,
		     unsigned char *buffer, size_t len, long timeout)
{
	ccid_status_t *st = (ccid_status_t *) reader->driver_data;
	int r;

	ifd_debug(1, "called.");

	r = ccid_exchange(reader, dad, st->sbuf[dad], st->slen[dad], buffer,
			  len);
	if (st->sbuf[dad])
		free(st->sbuf[dad]);
	st->sbuf[dad] = NULL;
	st->slen[dad] = 0;
	if (r < 0)
		ifd_debug(3, "failed: %d", r);
	return r;
}

static int ccid_before_command(ifd_reader_t * reader)
{
	ccid_status_t *st = (ccid_status_t *) reader->driver_data;
	int rc;

	ifd_debug(1, "called.");

	if (!st->events_active) {
		return 0;
	}

	if (st->event_cap == NULL) {
		return 0;
	}

	rc = ifd_usb_end_capture(reader->device, st->event_cap);
	st->event_cap = NULL;

	return rc;
}

static int ccid_after_command(ifd_reader_t * reader)
{
	ccid_status_t *st = (ccid_status_t *) reader->driver_data;

	ifd_debug(1, "called.");

	if (!st->events_active) {
		return 0;
	}

	if (st->event_cap != NULL) {
		return 0;
	}

	return ifd_usb_begin_capture(
		reader->device,
		IFD_USB_URB_TYPE_INTERRUPT,
		reader->device->settings.usb.ep_intr,
		8, &st->event_cap
	);
}

static int ccid_get_eventfd(ifd_reader_t * reader, short *events)
{
	ccid_status_t *st = (ccid_status_t *) reader->driver_data;
	int fd;

	ifd_debug(1, "called.");

	if (!st->support_events) {
		return -1;
	}

	fd = ifd_device_get_eventfd(reader->device, events);

	if (fd != -1) {
		st->events_active = 1;
	}

	return fd;
}

static int ccid_event(ifd_reader_t * reader, int *status, size_t status_size)
{
	ccid_status_t *st = (ccid_status_t *) reader->driver_data;
	unsigned char ret[20];
	int bytes;

	ifd_debug(1, "called.");

	if (status_size < reader->nslots) {
		return IFD_ERROR_BUFFER_TOO_SMALL;
	}

	bytes = ifd_usb_capture_event(reader->device, st->event_cap, ret, 8);
	if (bytes < 0) {
		return bytes;
	}

	if (bytes > 0 && ret[0] == 0x50) {
		int slot;
		ifd_debug(3, "status received:%s", ct_hexdump(ret, bytes));
		for (slot=0;slot<reader->nslots;slot++) {
			if (1 + (slot / 4) < bytes) {
				int bits = (ret[1 + (slot / 4)] >> (2 * (slot % 4))) & 0x3;
				if (bits & 2)
					status[slot] |= IFD_CARD_STATUS_CHANGED;
				if (bits & 1)
					status[slot] |= IFD_CARD_PRESENT;
				else
					status[slot] &= ~IFD_CARD_PRESENT;

				ifd_debug(1, "slot %d event result: %08x", slot, status[slot]);
				st->icc_present[slot] = status[slot] & IFD_CARD_PRESENT;
			}
		}
	}

	return 0;
}

static int ccid_error(ifd_reader_t * reader)
{
	(void)reader;

	ifd_debug(1, "called.");

	return IFD_ERROR_DEVICE_DISCONNECTED;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops ccid_driver;

/*
 * Initialize this module
 */
void ifd_ccid_register(void)
{
	ccid_driver.open = ccid_open;
	ccid_driver.close = ccid_close;
	ccid_driver.activate = ccid_activate;
	ccid_driver.deactivate = ccid_deactivate;
	ccid_driver.card_status = ccid_card_status;
	ccid_driver.card_reset = ccid_card_reset;
	ccid_driver.set_protocol = ccid_set_protocol;
	ccid_driver.transparent = ccid_transparent;
	ccid_driver.send = ccid_send;
	ccid_driver.recv = ccid_recv;
	ccid_driver.escape = ccid_escape;
	ccid_driver.before_command = ccid_before_command;
	ccid_driver.after_command = ccid_after_command;
	ccid_driver.get_eventfd = ccid_get_eventfd;
	ccid_driver.event = ccid_event;
	ccid_driver.error = ccid_error;

	ifd_driver_register("ccid", &ccid_driver);
}
