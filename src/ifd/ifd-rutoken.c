/*
 * driver for Rutoken devices
 *
 * Copyright (C) 2007, Pavel Mironchik <rutoken@rutoken.ru>
 * Copyright (C) 2007, Eugene Hermann <e_herman@rutoken.ru>
 */

#include "internal.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

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
	ifd_device_params_t params;

	ifd_debug(6, "rutoken_open - %s", device_name);

	reader->name = "Rutoken S driver";
	reader->nslots = 1;
	if (!(dev = ifd_device_open(device_name)))
		return -1;

	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ct_error("Rutoken: device %s is not a USB device", device_name);
		ifd_device_close(dev);
		return -1;
	}

	params = dev->settings;
	params.usb.interface = 0;
	if (ifd_device_set_parameters(dev, &params) < 0) {
		ct_error("Rutoken: setting parameters failed", device_name);
		ifd_device_close(dev);
		return -1;
	}

	reader->device = dev;
	dev->timeout = 1000;

	ifd_debug(6, "rutoken_open - %s - successful", device_name);
	return 0;
}

static int rutoken_activate(ifd_reader_t * reader)
{
	ifd_debug(6, "called.");
	return 0;
}

static int rutoken_deactivate(ifd_reader_t * reader)
{
	ifd_debug(6, "called.");
	return -1;
}

static int rutoken_getstatus(ifd_reader_t * reader, unsigned char *status)
{
	if(ifd_usb_control(reader->device, 0xc1, USB_ICC_GET_STATUS, 
				0, 0, status, 1, 1000) < 0 )
		return -1;
	if((*status & 0xF0) == ICC_STATUS_BUSY_COMMON){
		unsigned char prev_status;
		int i;
		for(i = 0; i < 200; i++) { // 2 s  (200 * 10 ms)
			do {
				usleep(10000); // 10 ms
				prev_status = *status;
				if(ifd_usb_control(reader->device, 0xc1, 
							USB_ICC_GET_STATUS, 0, 0, 
							status, 1, 1000) < 0
				)
				return -1;
			if((*status & 0xF0) != ICC_STATUS_BUSY_COMMON)
					return *status;
			} while((((prev_status & 0x0F) + 1) & 0x0F) == (*status & 0x0F));
		}
		return -1;
	}
	return *status;
}

static int rutoken_card_reset(ifd_reader_t * reader, int slot, void *atr,
		size_t atr_len)
{
	int nLen = 0, i;
	ifd_debug(6, "rutoken_card_reset, slot = %X", slot);
	if(ifd_usb_control(reader->device, 0x41, USB_ICC_POWER_OFF, 0, 0, 0, 0, -1) < 0)
	{
		ifd_debug(6, "error poweroff");
		return -1;
	}
	unsigned char status;
	if( rutoken_getstatus(reader, &status) < 0)
	{
		ifd_debug(6, "error get status");
		return -1;
	}
	if( status == ICC_STATUS_READY_DATA ) {
		char buf[OUR_ATR_LEN];
		memset(buf, 0, OUR_ATR_LEN);

		nLen = ifd_usb_control(reader->device, 0xc1, USB_ICC_POWER_ON, 0, 0, 
				buf, OUR_ATR_LEN, 1000);
		if( nLen < 0 )
		{
			ifd_debug(6, "error poewron");
			return -1;
		}

		ifd_debug(6, "returned len = %d", nLen);
		for(i = 0; i < OUR_ATR_LEN; i++) ifd_debug(6, "%c", buf[i]);
		memcpy(atr, buf, nLen);
		return nLen;
	}

	ifd_debug(6, "error bad status");
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
	ifd_slot_t *slot;
	ifd_protocol_t *p;

	ifd_debug(6, "proto=%d", proto);
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
	ifd_debug(6, "success");
	return 0;
}

static int rutoken_card_status(ifd_reader_t * reader, int slot,
		int *status)
{
	*status = IFD_CARD_PRESENT;
	return 0;
}

static int rutoken_send(ifd_reader_t * reader, unsigned int dad,
		const unsigned char *buffer, size_t len)
{
	int ret;
	unsigned char status;
	ifd_debug(3, "usb send %s len %d", ct_hexdump(buffer, len), len);
	ret = ifd_usb_control(reader->device, 0x41, USB_ICC_XFR_BLOCK, 0, 0, 
			(void *)buffer, len, -1);

	if (rutoken_getstatus(reader, &status) < 0)
	{
		ret = -1;
		ifd_debug(6, "error get status");
	}
	return ret;
}

static int rutoken_recv(ifd_reader_t * reader, unsigned int dad,
		unsigned char *buffer, size_t len, long timeout)
{
	unsigned char status;
	int ret = len;
	// USB_ICC_DATA_BLOCK
	if( (ret = ifd_usb_control(reader->device, 0xc1, USB_ICC_DATA_BLOCK, 0, 0, 
					buffer, len, timeout)) >= 0)
		if (rutoken_getstatus(reader, &status) < 0)
		{
			ret = -1;
			ifd_debug(6, "error get status, %0x", status);
		}
	ifd_debug(3, "usd recv %s len %d", ct_hexdump(buffer, ret), ret);
	return ret;
}

static int rutoken_recv_sw(ifd_reader_t * reader, int dad, unsigned char *sw)
{
	unsigned char status;
	if(rutoken_getstatus(reader, &status) == ICC_STATUS_MUTE)
	{  //If device not responsive
		ifd_debug(6, "status = ICC_STATUS_MUTE");
		return(rutoken_restart(reader));
	}
	if(status == ICC_STATUS_READY_SW)
	{
		ifd_debug(6, "status = ICC_STATUS_READY_SW;");
		if(rutoken_recv(reader, 0, sw, 2, 10000) < 0)
			return -5;
		ifd_debug(6, "Get SW %x %x", sw[0], sw[1]);
		return 2;
	}
	return -1;
}

// return how mach byte send
// sbuf - APDU bufer
// slen
static int rutoken_send_tpducomand(ifd_reader_t * reader, int dad, const void *sbuf, 
		size_t slen, void *rbuf, size_t rlen, int iscase4)
{
	ifd_debug(6, "send tpdu command %s, len: %d", ct_hexdump(sbuf, slen), slen);
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
			ifd_debug(6, "case 1");
			break;
		case    IFD_APDU_CASE_2S:
			// {cla, ins, p1, p2, le};
			// Rutoken Bug!!!
			ifd_debug(6, "case 2");
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
			ifd_debug(6, "case 3");
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
			ifd_debug(6, "Get Data %d", iso.le);
			if(rutoken_getstatus(reader, &status) == ICC_STATUS_READY_DATA)
			{
				rrecv = rutoken_recv(reader, 0, rbuf, iso.le, 10000);
				if (rrecv < 0)
					return -2;
				ifd_debug(6, "Get TPDU Anser %s", 
						ct_hexdump(rbuf, iso.le));
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
				return rutoken_send_tpducomand(reader, dad, sbuftmp, 
						slen, rbuf,  rlen, 0);
			}
			break;
		case    IFD_APDU_CASE_3S:
			// send data
			ifd_debug(6, "Send Data %d", iso.lc);
			if(rutoken_getstatus(reader, &status) == ICC_STATUS_READY_DATA)
			{
				ifd_debug(6, "Send TPDU Data %s", 
						ct_hexdump(iso.data, iso.lc));
				if (rutoken_send(reader, 0, iso.data, iso.lc) < 0)
					return -4;
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
					return rutoken_send_tpducomand(reader, dad, hdr, 
							T0_HDR_LEN, rbuf, rlen, 0);
				else {
					int recvtmp = rutoken_send_tpducomand(reader,dad,
							hdr, T0_HDR_LEN, rbuf, rlen, 0);
					rrecv = 0;
					memcpy(sw, (unsigned char*)rbuf+recvtmp-2, 2);
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
					return rutoken_send_tpducomand(reader, dad, hdr, 
							T0_HDR_LEN, rbuf, rlen, 0);
			}
			// NOT STANDART TPDU!!! END

			break;
		default:
			break;
	}
	// Add SW to respond
	memcpy(((char *)rbuf)+rrecv, sw, 2);
	rrecv+=2;
	ifd_debug(6, "Recv %d bytes", rrecv);
	return rrecv;
}

static void swap_pair(unsigned char *buf, size_t len)
{
	size_t i;
	unsigned char tmp;

	for (i = 0; i + 1 < len; i += 2) {
		tmp = buf[i];
		buf[i] = buf[i + 1];
		buf[i + 1] = tmp;
	}
}

static void swap_four(unsigned char *buf, size_t len)
{
	size_t i;
	unsigned char tmp;

	for (i = 0; i + 3 < len; i += 4) {
		tmp = buf[i];
		buf[i] = buf[i + 3];
		buf[i + 3] = tmp;

		swap_pair(&buf[i + 1], 2);
	}
}

static int read_tag(unsigned char *buf, size_t buf_len,
		unsigned char tag_in, unsigned char *out, size_t out_len)
{
	unsigned char tag;
	size_t taglen, i = 0;

	while (i + 2 <= buf_len) {
		tag = buf[i];
		taglen = buf[i + 1];
		i += 2;
		if (taglen + i > buf_len)
			return -1;
		if (tag == tag_in) {
			if (taglen != out_len)
				return -1;
			memcpy(out, buf + i, out_len);
			return 0;
		}
		i += taglen;
	}
	return -1;
}

static int convert_doinfo_to_rtprot(void *data, size_t data_len)
{
	unsigned char dohdr[32] = { 0 };
	unsigned char secattr[40], data_a5[0xff];
	unsigned char *p = data;
	size_t i, data_a5_len;

	if (read_tag(p, data_len, 0x80, &dohdr[0], 2) == 0) {
		swap_pair(&dohdr[0], 2);
		ifd_debug(6, "tag 0x80 (file size) = %02x %02x", dohdr[0], dohdr[1]);
	}
	data_a5_len = dohdr[1] & 0xff;
	if (read_tag(p, data_len, 0xA5, data_a5, data_a5_len) == 0)
		ifd_debug(6, "tag 0xA5 = %s", ct_hexdump(data_a5, data_a5_len));
	else
		data_a5_len = 0;
	if (data_len < sizeof(dohdr) + data_a5_len) {
		ifd_debug(6, "data_len = %u", data_len);
		return -1;
	}
	if (read_tag(p, data_len, 0x83, &dohdr[2], 2) == 0)
		ifd_debug(6, "tag 0x83 (Type,ID) = %02x %02x", dohdr[2], dohdr[3]);
	if (read_tag(p, data_len, 0x85, &dohdr[4], 3) == 0)
		ifd_debug(6, "tag 0x85 (Opt,Flags,MaxTry) = %02x %02x %02x",
				dohdr[4], dohdr[5], dohdr[6]);
	if (read_tag(p, data_len, 0x86, secattr, sizeof(secattr)) == 0) {
		i = 17;
		memcpy(dohdr + i, secattr, 8);
		for (i += 8, p = &secattr[8]; i < sizeof(dohdr); ++i, p += 4)
			dohdr[i] = *p;
		ifd_debug(6, "tag 0x86 = %s", ct_hexdump(&dohdr[17], 15));
	}
	memcpy(data, dohdr, sizeof(dohdr));
	memcpy((unsigned char*)data + sizeof(dohdr), data_a5, data_a5_len);
	return sizeof(dohdr) + data_a5_len;
}

static int convert_fcp_to_rtprot(void *data, size_t data_len)
{
	unsigned char rtprot[32] = { 0 };
	unsigned char secattr[40];
	unsigned char *p = data;
	size_t i;

	if (data_len < sizeof(rtprot)) {
		ifd_debug(6, "data_len = %u", data_len);
		return -1;
	}
	/* 0x62 - FCP */
	if (p[0] != 0x62  ||  (size_t)p[1] + 2 > data_len) {
		ifd_debug(6, "Tag = %02x  len = %u", p[0], p[1]);
		return -1;
	}
	p += 2;
	data_len -= 2;
	/* file type */
	if (read_tag(p, data_len, 0x82, &rtprot[4], 2) != 0)
		return -1;
	ifd_debug(6, "tag 0x82 (file type) = %02x %02x", rtprot[4], rtprot[5]);
	/* file id */
	if (read_tag(p, data_len, 0x83, &rtprot[6], 2) != 0)
		return -1;
	swap_pair(&rtprot[6], 2);
	ifd_debug(6, "tag 0x83 (file id) = %02x %02x", rtprot[6], rtprot[7]);
	/* file size */
	if (read_tag(p, data_len, 0x81, &rtprot[0], 2) == 0) {
		swap_pair(&rtprot[0], 2);
		ifd_debug(6, "tag 0x81 (complete file size) = %02x %02x",
				rtprot[0], rtprot[1]);
	}
	if (read_tag(p, data_len, 0x80, &rtprot[2], 2) == 0) {
		swap_pair(&rtprot[2], 2);
		ifd_debug(6, "tag 0x80 (file size) = %02x %02x", rtprot[2], rtprot[3]);
	}
	if (read_tag(p, data_len, 0x86, secattr, sizeof(secattr)) == 0) {
		i = 17;
		memcpy(rtprot + i, secattr, 8);
		for (i += 8, p = &secattr[8]; i < sizeof(rtprot); ++i, p += 4)
			rtprot[i] = *p;
		ifd_debug(6, "tag 0x86 = %s", ct_hexdump(&rtprot[17], 15));
	}
	memcpy(data, rtprot, sizeof(rtprot));
	return sizeof(rtprot);
}

static int convert_rtprot_to_doinfo(void *data, size_t data_len)
{
	unsigned char doinfo[0xff] = { 0 };
	unsigned char *pdata = data;
	size_t i, doinfo_len = 0;

	if (data_len < 32) {
		ifd_debug(6, "data_len = %u", data_len);
		return -1;
	}
	if (pdata[0] != 0 && pdata[0] < sizeof(doinfo) - 4 - 4 - 5 - 42 - 2) {
		/* Tag 0x80 */
		doinfo[doinfo_len++] = 0x80;
		doinfo[doinfo_len++] = 2;
		memcpy(doinfo + doinfo_len, pdata, 2);
		swap_pair(doinfo + doinfo_len, 2);
		doinfo_len += 2;
	}
	/* Tag 0x83 */
	doinfo[doinfo_len++] = 0x83;
	doinfo[doinfo_len++] = 2;
	doinfo[doinfo_len++] = pdata[2];
	doinfo[doinfo_len++] = pdata[3];

	/* Tag 0x85 */
	doinfo[doinfo_len++] = 0x85;
	doinfo[doinfo_len++] = 3;
	doinfo[doinfo_len++] = pdata[4];
	doinfo[doinfo_len++] = pdata[5];
	doinfo[doinfo_len++] = pdata[6];

	/* Tag 0x86 */
	doinfo[doinfo_len++] = 0x86;
	doinfo[doinfo_len++] = 40;
	memcpy(doinfo + doinfo_len, pdata + 17, 8);
	doinfo_len += 8;
	for (i = 0; i < 7 && doinfo_len + 3 < sizeof(doinfo); ++i, doinfo_len += 4)
		doinfo[doinfo_len] = pdata[17 + 8 + i];
	doinfo_len += 4; /* for reserved */
	if (pdata[0] != 0 && pdata[0] + doinfo_len + 2 < sizeof(doinfo)) {
		/* Tag 0xA5 */
		if (data_len - 32 < pdata[0]) {
			ifd_debug(6, "for tag 0xA5 incorrect data_len = %u", data_len);
			return -1;
		}
		doinfo[doinfo_len++] = 0xA5;
		doinfo[doinfo_len++] = pdata[0];
		memcpy(doinfo + doinfo_len, pdata + 32, pdata[0]);
		doinfo_len += pdata[0];
	}
	ifd_debug(6, "doinfo = %s", ct_hexdump(doinfo, doinfo_len));
	memcpy(data, doinfo, doinfo_len);
	return doinfo_len;
}

static int convert_rtprot_to_fcp(void *data, size_t data_len)
{
	unsigned char fcp[63] = {
		0x62, sizeof(fcp) - 2,
		0x81, 2, 0, 0,
		0x80, 2, 0, 0,
		0x82, 2, 0, 0,
		0x83, 2, 0, 0,
		0x8A, 1, 0,
		0x86, 40
	};
	unsigned char *p = data;
	size_t i;

	if (data_len < sizeof(fcp)) {
		ifd_debug(6, "data_len = %u", data_len);
		return -1;
	}
	/* Tag 0x81 */
	memcpy(fcp + 4, p, 2);
	swap_pair(fcp + 4, 2);
	/* Tag 0x80 */
	memcpy(fcp + 8, p + 2, 2);
	swap_pair(fcp + 8, 2);
	/* Tag 0x82 */
	memcpy(fcp + 12, p + 4, 2);
	/* Tag 0x83 */
	memcpy(fcp + 16, p + 6, 2);
	swap_pair(fcp + 16, 2);
	/* Tag 0x8A */
	fcp[20] = p[8];

	/* Tag 0x86 */
	memcpy(fcp + 23, p + 17, 8);
	for (i = 0; i < 7 && sizeof(fcp) > 23 + 8 + i * 4; ++i)
		fcp[23 + 8 + i * 4] = p[17 + 8 + i];
	ifd_debug(6, "fcp = %s", ct_hexdump(fcp, sizeof(fcp)));
	memcpy(data, fcp, sizeof(fcp));
	return sizeof(fcp);
}

static int rutoken_transparent( ifd_reader_t * reader, int dad,
		const void *sbuf, size_t slen,
		void *rbuf, size_t rlen)
{
	unsigned char sw[2], *send_buf_trn = NULL;
	const void *send_buf = sbuf;
	int len, rrecv = -1, iscase4 = 0;
	ifd_iso_apdu_t iso;

	ifd_debug(6, "buffer %s rlen = %d", ct_hexdump(sbuf, slen), rlen);
	if ( ifd_iso_apdu_parse(sbuf, slen, &iso) < 0)
		return -1;
	ifd_debug(6, "iso.le = %d", iso.le);

	if (iso.cla == 0 && slen > 5) {
		send_buf_trn = malloc(slen);
		if (!send_buf_trn) {
			ifd_debug(5, "out of memory (slen = %u)", slen);
			return IFD_ERROR_NO_MEMORY;
		}
		memcpy(send_buf_trn, sbuf, slen);
		/* select file, delete file */
		if (iso.ins == 0xa4 || iso.ins == 0xe4)
			swap_pair(send_buf_trn + 5, slen - 5);
		/* create file */
		else if (iso.ins == 0xe0) {
			len = convert_fcp_to_rtprot(send_buf_trn + 5, slen - 5);
			ifd_debug(6, "convert_fcp_to_rtprot = %i", len);
			if (len > 0) {
				slen = len + 5;
				send_buf_trn[4] = len; /* replace le */
			}
		}
		/* create_do, key_gen */
		else if (iso.ins == 0xda && iso.p1 == 1
				&& (iso.p2 == 0x65 || iso.p2 == 0x62)) {
			len = convert_doinfo_to_rtprot(send_buf_trn + 5, slen - 5);
			ifd_debug(6, "convert_doinfo_to_rtprot = %i", len);
			if (len > 0) {
				slen = len + 5;
				send_buf_trn[4] = len; /* replace le */
			}
		}
		ifd_debug(6, "le = %u", send_buf_trn[4]);
		send_buf = send_buf_trn;
	}
	switch(iso.cse){
		case	IFD_APDU_CASE_2S:
		case	IFD_APDU_CASE_3S:
			if (iso.cla == 0 && iso.ins == 0xa4)
				iscase4 = 1; /* FIXME: */
		case	IFD_APDU_CASE_1:
			rrecv = rutoken_send_tpducomand(reader, dad, send_buf, slen,
					rbuf, rlen, iscase4);
			break;
		case	IFD_APDU_CASE_4S:
			// make send case 4 command
			rrecv = rutoken_send_tpducomand(reader, dad, send_buf, slen-1,
					rbuf, rlen, 1);
			break;
		default:
			break;
	}
	if (send_buf_trn)
		free(send_buf_trn);

	if (rrecv > 0 && (size_t)rrecv >= sizeof(sw)) {
		memcpy(sw, (unsigned char*)rbuf + rrecv - sizeof(sw), sizeof(sw));
		if (sw[0] != 0x90 || sw[1] != 0)
			/* do nothing */;
		/* select file */
		else if (iso.cla == 0 && iso.ins == 0xa4
				&& rrecv == sizeof(sw) + 32 /* size rtprot */) {
			len = convert_rtprot_to_fcp(rbuf, rlen);
			ifd_debug(6, "convert_rtprot_to_fcp = %i", len);
			if (len > 0) {
				rrecv = -1;
				if (rlen >= len + sizeof(sw)) {
					memcpy((unsigned char*)rbuf+len, sw, sizeof(sw));
					rrecv = len + sizeof(sw);
				}
			}
		}
		/* get_do_info */
		else if (iso.cla == 0x80 && iso.ins == 0x30
				&& (size_t)rrecv >= sizeof(sw) + 32 /* size rtprot */) {
			len = convert_rtprot_to_doinfo(rbuf, rlen);
			ifd_debug(6, "convert_rtprot_to_doinfo = %i", len);
			if (len > 0) {
				rrecv = -1;
				if (rlen >= len + sizeof(sw)) {
					memcpy((unsigned char*)rbuf+len, sw, sizeof(sw));
					rrecv = len + sizeof(sw);
				}
			}
		}
		else if (iso.cla == 0 && iso.ins == 0xca && iso.p1 == 1) {
			/* get_serial, get_free_mem */
			if (iso.p2 == 0x81 || iso.p2 == 0x8a)
				swap_four(rbuf, rrecv - sizeof(sw));
			/* get_current_ef */
			else if (iso.p2 == 0x11)
				swap_pair(rbuf, rrecv - sizeof(sw));
		}
	}
	return rrecv;
}

static int rutoken_get_eventfd(ifd_reader_t * reader, short *events)
{
	ifd_debug(6, "called.");

	return ifd_device_get_eventfd(reader->device, events);
}

static int rutoken_event(ifd_reader_t * reader, int *status, size_t status_size)
{
	(void)reader;
	(void)status;
	(void)status_size;

	ifd_debug(6, "called.");

	return 0;
}

static int rutoken_error(ifd_reader_t * reader)
{
	(void)reader;

	ifd_debug(6, "called.");

	return IFD_ERROR_DEVICE_DISCONNECTED;
}

static struct ifd_driver_ops rutoken_driver;

void ifd_rutoken_register(void)
{
	rutoken_driver.open = rutoken_open;
	rutoken_driver.activate = rutoken_activate;
	rutoken_driver.deactivate = rutoken_deactivate;
	rutoken_driver.card_reset = rutoken_card_reset;
	rutoken_driver.card_status = rutoken_card_status;
	rutoken_driver.set_protocol = rutoken_set_protocol;
	rutoken_driver.transparent = rutoken_transparent;
	rutoken_driver.get_eventfd = rutoken_get_eventfd;
	rutoken_driver.event = rutoken_event;
	rutoken_driver.error = rutoken_error;

	ifd_driver_register("rutoken", &rutoken_driver);
}

