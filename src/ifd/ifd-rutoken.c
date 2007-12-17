/*
 * driver for Rutoken devices
 *
 * Copyright (C) 2007, Pavel Mironchik <rutoken@rutoken.ru>
 * Copyright (C) 2007, Eugene Hermann <e_herman@tut.by>
 */

#include "internal.h"
#include "stdio.h"
#include "unistd.h"
#include "string.h"

#define MAX_BUF_T0_LEN  256
#define T0_HDR_LEN      5


#define USB_ICC_POWER_ON	0x62
#define USB_ICC_POWER_OFF	0x63
#define USB_ICC_XFR_BLOCK	0x65
#define USB_ICC_DATA_BLOCK	0x6F
#define USB_ICC_GET_STATUS	0xA0

#define ICC_STATUS_IDLE			0x00
#define ICC_STATUS_READY_DATA	0x10
#define ICC_STATUS_READY_SW		0x20
#define ICC_STATUS_BUSY_COMMON	0x40
#define ICC_STATUS_MUTE			0x80

#define OUR_ATR_LEN	19

static int rutoken_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	ifd_debug(1, "rutoken_open - %s\n", device_name);
	ifd_debug(1, "%s:%d rutoken_open()", __FILE__, __LINE__);

	reader->name = "ruToken driver.\n";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;

	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("ruToken driver: device %s is not a USB device", device_name);
		ifd_device_close(dev);
		return -1;
	}

	reader->device = dev;
	dev->timeout = 1000;

	ifd_debug(1, "%s:%d Checkpoint", __FILE__, __LINE__);
	return 0;
}

static int rutoken_activate(ifd_reader_t * reader)
{
	ifd_debug(1, "%s:%d rutoken_activate()", __FILE__, __LINE__);
	return 0;
}

static int rutoken_deactivate(ifd_reader_t * reader)
{
	ifd_debug(1, "%s:%d rutoken_deactivate()", __FILE__, __LINE__);
	return -1;
}

// Get Status 0 - OK
//            	- ERROR
//
int rutoken_getstatus(ifd_reader_t * reader, char *status)
{
	// TODO Chech for timeout
	//ifd_debug(1, "");
	if(!ifd_usb_control(reader->device, 0xc1, USB_ICC_GET_STATUS, 0, 0, status, 1, 1000) < 0 )
		return -1;
	if((*status & 0xF0) == ICC_STATUS_BUSY_COMMON){
		int i;
		for(i = 0; i < 100000; i++) {
			if(!ifd_usb_control(reader->device, 0xc1, USB_ICC_GET_STATUS, 0, 0, status, 1, 1000) < 0 )
				return -1;
			if((*status & 0xF0) != ICC_STATUS_BUSY_COMMON)
				return 0;
			usleep(1000);
		}
		return -1;
	}
	return *status;
}

static int rutoken_card_reset(ifd_reader_t * reader, int slot, void *atr,
		size_t atr_len)
{
	ifd_debug(1, "%s:%d rutoken_card_reset()", __FILE__, __LINE__);

	int nLen = 0, i;
	ifd_debug(1, "rutoken_card_reset, slot = %X\n", slot);
	if(ifd_usb_control(reader->device, 0x41, USB_ICC_POWER_OFF, 0, 0, 0, 0, -1) < 0)
	{
		ifd_debug(1, "error poweroff\n");
		return -1;
	}
	char status;
	if( rutoken_getstatus(reader, &status) < 0)
	{
		ifd_debug(1, "error get status\n");
		return -1;
	}
	if( status == ICC_STATUS_READY_DATA ) {
		char buf[OUR_ATR_LEN];
		memset(buf, 0, OUR_ATR_LEN);

		nLen = ifd_usb_control(reader->device, 0xc1, USB_ICC_POWER_ON, 0, 0, buf, OUR_ATR_LEN, 1000);
		if( nLen < 0 )
		{
			ifd_debug(1, "error poewron\n");
			return -1;
		}

		ifd_debug(1, "returned len = %d", nLen);
		for(i = 0; i < OUR_ATR_LEN; i++) ifd_debug(1, "%c", buf[i]);
		memcpy(atr, buf, nLen);
		return nLen;
	}

	ifd_debug(1, "error bad status\n");
	return -1;
}

static int rutoken_restart(ifd_reader_t * reader)
{
	char atr[256];
	return rutoken_card_reset(reader, 0, atr, 256);
}

/*
 * Select a protocol.
 */
static int rutoken_set_protocol(ifd_reader_t * reader, int nslot, int proto)
{
	ifd_debug(1, "set protocol: {%d}", proto);

	ifd_slot_t *slot;
	ifd_protocol_t *p;

	ifd_debug(1, "proto=%d", proto);
	if (proto != IFD_PROTOCOL_T0 && proto != IFD_PROTOCOL_TRANSPARENT) {
		ct_error("%s: protocol %d not supported", reader->name, proto);
		return IFD_ERROR_NOT_SUPPORTED;
	}
	slot = &reader->slot[nslot];
	p = ifd_protocol_new(IFD_PROTOCOL_TRANSPARENT, reader, slot->dad);
	if (p == NULL) {
		ct_error("%s: internal error", reader->name);
		return IFD_ERROR_GENERIC;
	}
	if (slot->proto) {
		ifd_protocol_free(slot->proto);
		slot->proto = NULL;
	}
	slot->proto = p;
	ifd_debug(1, "success");
	return 0;
}

static int rutoken_card_status(ifd_reader_t * reader, int slot,
		int *status)
{
	//ifd_debug(1, "");
	*status = IFD_CARD_PRESENT;
	return 0;
}

static int rutoken_send(ifd_reader_t * reader, unsigned int dad,
		const unsigned char *buffer, size_t len)
{
	int ret;
	char status;
	ifd_debug(3, "usb send %s len %d", ct_hexdump(buffer, len), len);
	ret =  ifd_usb_control(reader->device, 0x41, USB_ICC_XFR_BLOCK, 0, 0, (void *)buffer, len, -1);

	if (rutoken_getstatus(reader, &status) < 0)
	{
		ret = -1;
		ifd_debug(1, "error get status");
	}
	return ret;
}

static int rutoken_recv(ifd_reader_t * reader, unsigned int dad,
		unsigned char *buffer, size_t len, long timeout)
{
	char status;
	int ret = len;
	// USB_ICC_DATA_BLOCK
	if( (ret = ifd_usb_control(reader->device, 0xc1, USB_ICC_DATA_BLOCK, 0, 0, buffer, len, timeout)) >= 0)
		if (rutoken_getstatus(reader, &status) < 0)
		{
			ret = -1;
			ifd_debug(1, "error get status, %0x", status);
		}
	ifd_debug(3, "usd recv %s len %d", ct_hexdump(buffer, ret), ret);
	return ret;
}

static int rutoken_recv_sw(ifd_reader_t * reader, int dad, unsigned char *sw)
{
	unsigned char status;
	if(rutoken_getstatus(reader, &status) == ICC_STATUS_MUTE)
	{  //If device not responsive
		ifd_debug(1, "status = ICC_STATUS_MUTE");
		return(rutoken_restart(reader));
	}
	if(status == ICC_STATUS_READY_SW)
	{
		ifd_debug(1, "status = ICC_STATUS_READY_SW;");
		if(rutoken_recv(reader, 0, sw, 2, 10000) < 0)
			return -5;
		ifd_debug(1, "Get SW %x %x", sw[0], sw[1]);
		return 2;
	}
	return -1;
}

// return how mach byte send
// sbuf - APDU bufer
// slen
static int rutoken_send_tpducomand(ifd_reader_t * reader, int dad, const void *sbuf, size_t slen, void *rbuf, size_t rlen, int iscase4)
{
	ifd_debug(1, "send tpdu command %s, len: %d", ct_hexdump(sbuf, slen), slen);
	int rrecv = 0;
	unsigned char status;
	unsigned char sw[2];
	ifd_iso_apdu_t iso;
	if ( ifd_iso_apdu_parse(sbuf, slen, &iso) < 0)
		return -1;
	unsigned char hdr[T0_HDR_LEN]={iso.cla, iso.ins, iso.p1, iso.p2, 0};
	switch(iso.cse){
		case	IFD_APDU_CASE_1:
			// {cla, ins, p1, p2, 0};
			ifd_debug(1, "case 1");
			break;
		case    IFD_APDU_CASE_2S:
			// {cla, ins, p1, p2, le};
			// Rutoken Bug!!!
			ifd_debug(1, "case 2");
			if(iso.ins == 0xa4){
				hdr[4] = 0x20;
				iso.le = 0x20;
			}
			else{
				hdr[4] = iso.le;
			}
			break;
		case    IFD_APDU_CASE_3S:
			// {cla, ins, p1, p2, lc};
			ifd_debug(1, "case 3");
			hdr[4] = iso.lc;
			break;
		default:
			break;
	}
	//send TPDU header
	if (rutoken_send(reader, 0, hdr, T0_HDR_LEN) < 0)
		return -1;
	// send TPDU data or get answere and sw
	switch(iso.cse){
		case	IFD_APDU_CASE_1:
			// get sw
			if (rutoken_recv_sw(reader, 0, sw) < 0)
				return -2;
			break;
		case    IFD_APDU_CASE_2S:
			// get answere
			ifd_debug(1, "Get Data %d", iso.le);
			if(rutoken_getstatus(reader, &status) == ICC_STATUS_READY_DATA)
			{
				rrecv = rutoken_recv(reader, 0, rbuf, iso.le, 10000);
				if (rrecv < 0)
					return -2;
				ifd_debug(1, "Get TPDU Anser %s", ct_hexdump(rbuf, iso.le));
			}
			if (rutoken_recv_sw(reader, 0, sw) < 0)
				return -2;
			if ( sw[0] == 0x67) {
				// Le definitely not accepted
				break;
			}
			if ( (sw[0] == 0x6c) ) {
				unsigned char sbuftmp[slen];
				memcpy(sbuftmp, sbuf, slen);
				sbuftmp[4] = sw[1];
				return rutoken_send_tpducomand(reader, dad, sbuftmp, slen, rbuf,  rlen, 0);
			}

			break;
		case    IFD_APDU_CASE_3S:
			// send data
			ifd_debug(1, "Send Data %d", iso.lc);
			if(rutoken_getstatus(reader, &status) == ICC_STATUS_READY_DATA)
			{
				ifd_debug(1, "Send TPDU Data %s", ct_hexdump(iso.data, iso.lc));
				if (rutoken_send(reader, 0, iso.data, iso.lc) < 0) return -4;
			} else return -3;
			// get sw
			if (rutoken_recv_sw(reader, 0, sw) < 0)
				return -2;

			// NOT STANDART TPDU!!! BEGIN
			if ( sw[0]== 0x61){
				unsigned char lx = sw[1];
				hdr[0] = 0x00;  //  iso.cla; (ruTokens specific)
				hdr[1] = 0xc0; // ins get response
				hdr[2] = 0; // p1
				hdr[3] = 0; // p2
				hdr[4] = lx ; //lx (case 2)
				if(iscase4)
					return rutoken_send_tpducomand(reader, dad, hdr, T0_HDR_LEN, rbuf, rlen, 0);
				else {
					int recvtmp = rutoken_send_tpducomand(reader, dad, hdr, T0_HDR_LEN, rbuf, rlen, 0);
					rrecv = 0;
					memcpy(sw, rbuf+recvtmp-2, 2);
					break;
				}
			}

			if ( (sw[0] == 0x90) && (sw[1] == 0x00))
			{
				hdr[0] = 0x00; //iso.cla;
				hdr[1] = 0xc0; // ins get response
				hdr[2] = 0; // p1
				hdr[3] = 0; // p2
				hdr[4] = iso.le; // le (case 2)
				if(iscase4)
					return rutoken_send_tpducomand(reader, dad, hdr, T0_HDR_LEN, rbuf, rlen, 0);
			}
			// NOT STANDART TPDU!!! END

			break;
		default:
			break;
	}
	// Add SW to respond
	memcpy(((char *)rbuf)+rrecv, sw, 2);
	rrecv+=2;
	ifd_debug(1, "Recv %d bytes", rrecv);
	return rrecv;

}


static int rutoken_transparent( ifd_reader_t * reader, int dad,
		const void *sbuf, size_t slen,
		void *rbuf, size_t rlen)
{
	int rrecv = 0;
	ifd_iso_apdu_t iso;
	ifd_debug(1, "buffer %s rlen = %d", ct_hexdump(sbuf, slen), rlen);
	if ( ifd_iso_apdu_parse(sbuf, slen, &iso) < 0)
		return -1;
	ifd_debug(1, "iso.le = %d", iso.le);
	switch(iso.cse){
		case	IFD_APDU_CASE_1:
		case    IFD_APDU_CASE_2S:
		case    IFD_APDU_CASE_3S:
			return rutoken_send_tpducomand(reader, dad, sbuf, slen, rbuf, rlen, 0);
			break;
		case	IFD_APDU_CASE_4S:
			// make send case 4 command
			rrecv = rutoken_send_tpducomand(reader, dad, sbuf, slen-1, rbuf, rlen, 1);
			return rrecv;
			break;
		default:
			break;
	}
	return -1;
}

static struct ifd_driver_ops rutoken_driver;

void ifd_rutoken_register(void)
{
	ifd_debug(1, "ifd_rutoken_register()\n");
	rutoken_driver.open = rutoken_open;
	rutoken_driver.activate = rutoken_activate;
	rutoken_driver.deactivate = rutoken_deactivate;
	rutoken_driver.card_reset = rutoken_card_reset;
	rutoken_driver.card_status = rutoken_card_status;
	rutoken_driver.set_protocol = rutoken_set_protocol;
	rutoken_driver.transparent = rutoken_transparent;

	ifd_driver_register("rutoken", &rutoken_driver);
}
/*
 * driver for Rutoken devices
 *
 * Copyright (C) 2007, Pavel Mironchik <rutoken@rutoken.ru>
 * Copyright (C) 2007, Eugene Hermann <e_herman@tut.by>
 */

#include "internal.h"
#include "stdio.h"
#include "unistd.h"
#include "string.h"

#define MAX_BUF_T0_LEN  256
#define T0_HDR_LEN      5


#define USB_ICC_POWER_ON	0x62
#define USB_ICC_POWER_OFF	0x63
#define USB_ICC_XFR_BLOCK	0x65
#define USB_ICC_DATA_BLOCK	0x6F
#define USB_ICC_GET_STATUS	0xA0

#define ICC_STATUS_IDLE			0x00
#define ICC_STATUS_READY_DATA	0x10
#define ICC_STATUS_READY_SW		0x20
#define ICC_STATUS_BUSY_COMMON	0x40
#define ICC_STATUS_MUTE			0x80

#define OUR_ATR_LEN	19

static int rutoken_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_t *dev;
	ifd_debug(1, "rutoken_open - %s\n", device_name);
	ifd_debug(1, "%s:%d rutoken_open()", __FILE__, __LINE__);

	reader->name = "ruToken driver.\n";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;

	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("ruToken driver: device %s is not a USB device", device_name);
		ifd_device_close(dev);
		return -1;
	}

	reader->device = dev;
	dev->timeout = 1000;

	ifd_debug(1, "%s:%d Checkpoint", __FILE__, __LINE__);
	return 0;
}

static int rutoken_activate(ifd_reader_t * reader)
{
	ifd_debug(1, "%s:%d rutoken_activate()", __FILE__, __LINE__);
	return 0;
}

static int rutoken_deactivate(ifd_reader_t * reader)
{
	ifd_debug(1, "%s:%d rutoken_deactivate()", __FILE__, __LINE__);
	return -1;
}

// Get Status 0 - OK
//            	- ERROR
//
int rutoken_getstatus(ifd_reader_t * reader, char *status)
{
	// TODO Chech for timeout
	//ifd_debug(1, "");
	if(!ifd_usb_control(reader->device, 0xc1, USB_ICC_GET_STATUS, 0, 0, status, 1, 1000) < 0 )
		return -1;
	if((*status & 0xF0) == ICC_STATUS_BUSY_COMMON){
		int i;
		for(i = 0; i < 100000; i++) {
			if(!ifd_usb_control(reader->device, 0xc1, USB_ICC_GET_STATUS, 0, 0, status, 1, 1000) < 0 )
				return -1;
			if((*status & 0xF0) != ICC_STATUS_BUSY_COMMON)
				return 0;
			usleep(1000);
		}
		return -1;
	}
	return *status;
}

static int rutoken_card_reset(ifd_reader_t * reader, int slot, void *atr,
		size_t atr_len)
{
	ifd_debug(1, "%s:%d rutoken_card_reset()", __FILE__, __LINE__);

	int nLen = 0, i;
	ifd_debug(1, "rutoken_card_reset, slot = %X\n", slot);
	if(ifd_usb_control(reader->device, 0x41, USB_ICC_POWER_OFF, 0, 0, 0, 0, -1) < 0)
	{
		ifd_debug(1, "error poweroff\n");
		return -1;
	}
	char status;
	if( rutoken_getstatus(reader, &status) < 0)
	{
		ifd_debug(1, "error get status\n");
		return -1;
	}
	if( status == ICC_STATUS_READY_DATA ) {
		char buf[OUR_ATR_LEN];
		memset(buf, 0, OUR_ATR_LEN);

		nLen = ifd_usb_control(reader->device, 0xc1, USB_ICC_POWER_ON, 0, 0, buf, OUR_ATR_LEN, 1000);
		if( nLen < 0 )
		{
			ifd_debug(1, "error poewron\n");
			return -1;
		}

		ifd_debug(1, "returned len = %d", nLen);
		for(i = 0; i < OUR_ATR_LEN; i++) ifd_debug(1, "%c", buf[i]);
		memcpy(atr, buf, nLen);
		return nLen;
	}

	ifd_debug(1, "error bad status\n");
	return -1;
}

static int rutoken_restart(ifd_reader_t * reader)
{
	char atr[256];
	return rutoken_card_reset(reader, 0, atr, 256);
}

/*
 * Select a protocol.
 */
static int rutoken_set_protocol(ifd_reader_t * reader, int nslot, int proto)
{
	ifd_debug(1, "set protocol: {%d}", proto);

	ifd_slot_t *slot;
	ifd_protocol_t *p;

	ifd_debug(1, "proto=%d", proto);
	if (proto != IFD_PROTOCOL_T0 && proto != IFD_PROTOCOL_TRANSPARENT) {
		ct_error("%s: protocol %d not supported", reader->name, proto);
		return IFD_ERROR_NOT_SUPPORTED;
	}
	slot = &reader->slot[nslot];
	p = ifd_protocol_new(IFD_PROTOCOL_TRANSPARENT, reader, slot->dad);
	if (p == NULL) {
		ct_error("%s: internal error", reader->name);
		return IFD_ERROR_GENERIC;
	}
	if (slot->proto) {
		ifd_protocol_free(slot->proto);
		slot->proto = NULL;
	}
	slot->proto = p;
	ifd_debug(1, "success");
	return 0;
}

static int rutoken_card_status(ifd_reader_t * reader, int slot,
		int *status)
{
	//ifd_debug(1, "");
	*status = IFD_CARD_PRESENT;
	return 0;
}

static int rutoken_send(ifd_reader_t * reader, unsigned int dad,
		const unsigned char *buffer, size_t len)
{
	int ret;
	char status;
	ifd_debug(3, "usb send %s len %d", ct_hexdump(buffer, len), len);
	ret =  ifd_usb_control(reader->device, 0x41, USB_ICC_XFR_BLOCK, 0, 0, (void *)buffer, len, -1);

	if (rutoken_getstatus(reader, &status) < 0)
	{
		ret = -1;
		ifd_debug(1, "error get status");
	}
	return ret;
}

static int rutoken_recv(ifd_reader_t * reader, unsigned int dad,
		unsigned char *buffer, size_t len, long timeout)
{
	char status;
	int ret = len;
	// USB_ICC_DATA_BLOCK
	if( (ret = ifd_usb_control(reader->device, 0xc1, USB_ICC_DATA_BLOCK, 0, 0, buffer, len, timeout)) >= 0)
		if (rutoken_getstatus(reader, &status) < 0)
		{
			ret = -1;
			ifd_debug(1, "error get status, %0x", status);
		}
	ifd_debug(3, "usd recv %s len %d", ct_hexdump(buffer, ret), ret);
	return ret;
}

static int rutoken_recv_sw(ifd_reader_t * reader, int dad, unsigned char *sw)
{
	unsigned char status;
	if(rutoken_getstatus(reader, &status) == ICC_STATUS_MUTE)
	{  //If device not responsive
		ifd_debug(1, "status = ICC_STATUS_MUTE");
		return(rutoken_restart(reader));
	}
	if(status == ICC_STATUS_READY_SW)
	{
		ifd_debug(1, "status = ICC_STATUS_READY_SW;");
		if(rutoken_recv(reader, 0, sw, 2, 10000) < 0)
			return -5;
		ifd_debug(1, "Get SW %x %x", sw[0], sw[1]);
		return 2;
	}
	return -1;
}

// return how mach byte send
// sbuf - APDU bufer
// slen
static int rutoken_send_tpducomand(ifd_reader_t * reader, int dad, const void *sbuf, size_t slen, void *rbuf, size_t rlen, int iscase4)
{
	ifd_debug(1, "send tpdu command %s, len: %d", ct_hexdump(sbuf, slen), slen);
	int rrecv = 0;
	unsigned char status;
	unsigned char sw[2];
	ifd_iso_apdu_t iso;
	if ( ifd_iso_apdu_parse(sbuf, slen, &iso) < 0)
		return -1;
	unsigned char hdr[T0_HDR_LEN]={iso.cla, iso.ins, iso.p1, iso.p2, 0};
	switch(iso.cse){
		case	IFD_APDU_CASE_1:
			// {cla, ins, p1, p2, 0};
			ifd_debug(1, "case 1");
			break;
		case    IFD_APDU_CASE_2S:
			// {cla, ins, p1, p2, le};
			// Rutoken Bug!!!
			ifd_debug(1, "case 2");
			if(iso.ins == 0xa4){
				hdr[4] = 0x20;
				iso.le = 0x20;
			}
			else{
				hdr[4] = iso.le;
			}
			break;
		case    IFD_APDU_CASE_3S:
			// {cla, ins, p1, p2, lc};
			ifd_debug(1, "case 3");
			hdr[4] = iso.lc;
			break;
		default:
			break;
	}
	//send TPDU header
	if (rutoken_send(reader, 0, hdr, T0_HDR_LEN) < 0)
		return -1;
	// send TPDU data or get answere and sw
	switch(iso.cse){
		case	IFD_APDU_CASE_1:
			// get sw
			if (rutoken_recv_sw(reader, 0, sw) < 0)
				return -2;
			break;
		case    IFD_APDU_CASE_2S:
			// get answere
			ifd_debug(1, "Get Data %d", iso.le);
			if(rutoken_getstatus(reader, &status) == ICC_STATUS_READY_DATA)
			{
				rrecv = rutoken_recv(reader, 0, rbuf, iso.le, 10000);
				if (rrecv < 0)
					return -2;
				ifd_debug(1, "Get TPDU Anser %s", ct_hexdump(rbuf, iso.le));
			}
			if (rutoken_recv_sw(reader, 0, sw) < 0)
				return -2;
			if ( sw[0] == 0x67) {
				// Le definitely not accepted
				break;
			}
			if ( (sw[0] == 0x6c) ) {
				unsigned char sbuftmp[slen];
				memcpy(sbuftmp, sbuf, slen);
				sbuftmp[4] = sw[1];
				return rutoken_send_tpducomand(reader, dad, sbuftmp, slen, rbuf,  rlen, 0);
			}

			break;
		case    IFD_APDU_CASE_3S:
			// send data
			ifd_debug(1, "Send Data %d", iso.lc);
			if(rutoken_getstatus(reader, &status) == ICC_STATUS_READY_DATA)
			{
				ifd_debug(1, "Send TPDU Data %s", ct_hexdump(iso.data, iso.lc));
				if (rutoken_send(reader, 0, iso.data, iso.lc) < 0) return -4;
			} else return -3;
			// get sw
			if (rutoken_recv_sw(reader, 0, sw) < 0)
				return -2;

			// NOT STANDART TPDU!!! BEGIN
			if ( sw[0]== 0x61){
				unsigned char lx = sw[1];
				hdr[0] = 0x00;  //  iso.cla; (ruTokens specific)
				hdr[1] = 0xc0; // ins get response
				hdr[2] = 0; // p1
				hdr[3] = 0; // p2
				hdr[4] = lx ; //lx (case 2)
				if(iscase4)
					return rutoken_send_tpducomand(reader, dad, hdr, T0_HDR_LEN, rbuf, rlen, 0);
				else {
					int recvtmp = rutoken_send_tpducomand(reader, dad, hdr, T0_HDR_LEN, rbuf, rlen, 0);
					rrecv = 0;
					memcpy(sw, rbuf+recvtmp-2, 2);
					break;
				}
			}

			if ( (sw[0] == 0x90) && (sw[1] == 0x00))
			{
				hdr[0] = 0x00; //iso.cla;
				hdr[1] = 0xc0; // ins get response
				hdr[2] = 0; // p1
				hdr[3] = 0; // p2
				hdr[4] = iso.le; // le (case 2)
				if(iscase4)
					return rutoken_send_tpducomand(reader, dad, hdr, T0_HDR_LEN, rbuf, rlen, 0);
			}
			// NOT STANDART TPDU!!! END

			break;
		default:
			break;
	}
	// Add SW to respond
	memcpy(((char *)rbuf)+rrecv, sw, 2);
	rrecv+=2;
	ifd_debug(1, "Recv %d bytes", rrecv);
	return rrecv;
}

static int rutoken_transparent( ifd_reader_t * reader, int dad,
		const void *sbuf, size_t slen,
		void *rbuf, size_t rlen)
{
	int rrecv = 0;
	ifd_iso_apdu_t iso;
	ifd_debug(1, "buffer %s rlen = %d", ct_hexdump(sbuf, slen), rlen);
	if ( ifd_iso_apdu_parse(sbuf, slen, &iso) < 0)
		return -1;
	ifd_debug(1, "iso.le = %d", iso.le);
	switch(iso.cse){
		case	IFD_APDU_CASE_1:
		case    IFD_APDU_CASE_2S:
		case    IFD_APDU_CASE_3S:
			return rutoken_send_tpducomand(reader, dad, sbuf, slen, rbuf, rlen, 0);
			break;
		case	IFD_APDU_CASE_4S:
			// make send case 4 command
			rrecv = rutoken_send_tpducomand(reader, dad, sbuf, slen-1, rbuf, rlen, 1);
			return rrecv;
			break;
		default:
			break;
	}
	return -1;
}

static struct ifd_driver_ops rutoken_driver;

void ifd_rutoken_register(void)
{
	ifd_debug(1, "ifd_rutoken_register()\n");
	rutoken_driver.open = rutoken_open;
	rutoken_driver.activate = rutoken_activate;
	rutoken_driver.deactivate = rutoken_deactivate;
	rutoken_driver.card_reset = rutoken_card_reset;
	rutoken_driver.card_status = rutoken_card_status;
	rutoken_driver.set_protocol = rutoken_set_protocol;
	rutoken_driver.transparent = rutoken_transparent;

	ifd_driver_register("rutoken", &rutoken_driver);
}
