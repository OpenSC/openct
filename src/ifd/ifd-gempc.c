/*
 * Reader Drivers for GemPC devices.
 * Work in progress, entirely untested.
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPC_ISO_INPUT_MAX	248
#define GPC_ISO_EXCHANGE_MAX	254
#define GPC_MODE_ROS		0x08
#define GPC_MODE_TLP		0x01

typedef struct gpc_status {
	/* We need a GBP driver to talk to serial readers */
	ifd_protocol_t *p;
	int icc_proto;
	int card_state;
	unsigned char cmd_buf[260];
	size_t cmd_len;
} gpc_status_t;

static int gpc_transceive(ifd_reader_t *, unsigned int,
			  const unsigned char *, size_t,
			  unsigned char *, size_t, long);
static int gpc_transceive_t0(ifd_reader_t *, unsigned int,
			     const unsigned char *, size_t,
			     unsigned char *, size_t);
static int gpc_transceive_t1(ifd_reader_t *, unsigned int,
			     const unsigned char *, size_t,
			     unsigned char *, size_t);
static int gpc_iso_output(ifd_reader_t *,
			  const unsigned char *, size_t,
			  unsigned char *, size_t);
static int gpc_iso_input(ifd_reader_t *,
			 const unsigned char *, size_t,
			 unsigned char *, size_t);
static int gpc_iso_exchange_apdu(ifd_reader_t *,
				 const unsigned char *, size_t,
				 unsigned char *, size_t);
static int gpc_set_serial(ifd_reader_t *, unsigned int, int, int);
static int gpc_set_mode(ifd_reader_t *, int);
static int gpc_get_os_version(ifd_reader_t *, char *, size_t);
static int gpc_command(ifd_reader_t *, const void *, size_t, void *, size_t);
static int __gpc_command(ifd_reader_t *,
			 const void *, size_t, void *, size_t, int *);
static const char *gpc_strerror(int status);

/*
 * Initialize the reader
 */
static int gpc_open(ifd_reader_t * reader, const char *device_name)
{
	char buffer[256];
	gpc_status_t *st;
	ifd_device_t *dev;
	int r;

	ifd_debug(1, "called, device=%s", device_name);

	reader->name = "Gemplus Reader (pre-alpha, untested)";
	reader->nslots = 1;

	if (!(dev = ifd_device_open(device_name)))
		return IFD_ERROR_GENERIC;
	reader->device = dev;

	st = (gpc_status_t *) calloc(1, sizeof(*st));
	if (!st) {
		ct_error("out of memory");
		return IFD_ERROR_NO_MEMORY;
	}
	reader->driver_data = st;

	if (dev->type == IFD_DEVICE_TYPE_SERIAL) {
		ifd_device_params_t params;

		if (ifd_device_get_parameters(dev, &params) < 0)
			return IFD_ERROR_GENERIC;

		params.serial.speed = 9600;
		params.serial.bits = 8;
		params.serial.stopbits = 1;
		params.serial.parity = IFD_SERIAL_PARITY_NONE;

		if ((r = ifd_device_set_parameters(dev, &params)) < 0)
			return r;

		/* Instantiate a GBP driver for this reader */
		if (!(st->p = ifd_protocol_new(IFD_PROTOCOL_GBP, reader, 0))) {
			/* Something is badly hosed */
			ct_error("unable to get GBP protocol handler");
			return IFD_ERROR_GENERIC;
		}

		/* Tell the reader to switch to 38400 bps
		 * If the reader is already at 38400 bps, this
		 * command will fail, because we're currently
		 * driving the serial line at 9600. So don't even
		 * bother with checking the return value.
		 * Instead, pause for a moment.
		 */
		gpc_set_serial(reader, 38400, 8, IFD_SERIAL_PARITY_NONE);
		usleep(500000);
		ifd_device_flush(dev);
		params.serial.speed = 38400;
		if ((r = ifd_device_set_parameters(dev, &params)) < 0)
			return r;

		/* The line should now operate at 38400 */
		r = gpc_set_mode(reader, GPC_MODE_ROS);
		if (r < 0 && r != IFD_ERROR_NOT_SUPPORTED)
			return r;
	} else {
		ct_error("USB devices not yet supported for GemPC readers\n");
		return -1;
		/*
		   sleep(1);
		   ifd_device_flush(dev);
		 */
	}

	r = gpc_get_os_version(reader, buffer, sizeof(buffer));
	if (r >= 0) {
		if (!strcmp(buffer, "OROS-R2.24RM"))
			reader->name = "GCR 400";
		else if (!strcmp(buffer, "OROS-R2.99-R1.10"))
			reader->name = "GCR 410";
		else if (!strcmp(buffer, "OROS-R2.99-R1.11"))
			reader->name = "GCR 410P";
		else if (!strcmp(buffer, "OROS-R2.99-R1.21") ||
			 !strcmp(buffer, "GemCore-R1.21-GM"))
			reader->name = "GemPC 410";
		else if (!strcmp(buffer, "OROS-R2.99-R1.32"))
			reader->name = "GemPC 413";
		ifd_debug(1,
			  "OS version \"%s\", reader identified as \"%s\"\n",
			  buffer, reader->name);
	}

	return 0;
}

/*
 * Activate the reader
 */
static int gpc_activate(ifd_reader_t * reader)
{
	ifd_debug(1, "called.");
	return 0;
}

static int gpc_deactivate(ifd_reader_t * reader)
{
	return 0;
}

static int gpc_close(ifd_reader_t * reader)
{
	return 0;
}

/*
 * Check card status
 */
static int gpc_card_status(ifd_reader_t * reader, int slot, int *status)
{
	gpc_status_t *st = (gpc_status_t *) reader->driver_data;
	unsigned char byte;
	int r;

	if (slot != 0) {
		ct_error("gempc: bad slot index %u", slot);
		return IFD_ERROR_INVALID_SLOT;
	}

	if ((r = gpc_command(reader, "\x17", 1, &byte, 1)) < 0)
		return r;

	ifd_debug(4, "card %spresent%s\n",
		  (byte & 0x04) ? "" : "not ",
		  (byte & 0x02) ? ", powered up" : "");

	*status = (byte & 0x04) ? IFD_CARD_PRESENT : 0;

	/* A power up/down transition can be used to detect
	 * a card change. */
	if ((st->card_state & 0x02) && !(byte & 0x02))
		*status |= IFD_CARD_STATUS_CHANGED;

	st->card_state = byte;
	return 0;
}

/*
 * Reset the card and get the ATR
 */
static int gpc_card_reset(ifd_reader_t * reader, int slot, void *atr,
			  size_t size)
{
	static unsigned char reset_auto_pps[] = { 0x12, 0x23 };
	static unsigned char reset_no_pps[] = { 0x12, 0x13 };
	static unsigned char reset_emv[] = { 0x12 };
	static unsigned char set_mode[] = { 0x17, 0x00, 0x47 };
	int r, status;

	ifd_debug(1, "called.");

	if (slot != 0) {
		ct_error("gempc: bad slot index %u", slot);
		return IFD_ERROR_INVALID_SLOT;
	}

	/* Get the card status */
	if ((r = gpc_card_status(reader, slot, &status)) < 0)
		return r;

	if (!(status & IFD_CARD_PRESENT))
		return IFD_ERROR_NO_CARD;

	r = __gpc_command(reader, reset_auto_pps, sizeof(reset_auto_pps),
			  atr, size, &status);
	if (r < 0 || status == 0x00)
		return r;

	/* Try again without PPS */
	r = __gpc_command(reader, reset_no_pps, sizeof(reset_no_pps),
			  atr, size, &status);
	if (r < 0 || status == 0x00)
		return r;

	/* Try EMV mode */
	r = __gpc_command(reader, reset_emv, sizeof(reset_emv),
			  atr, size, &status);
	if (r < 0 || status == 0x00)
		return r;

	/* Change Operation Mode, and retry EMV reset */
	(void)gpc_command(reader, set_mode, sizeof(set_mode), NULL, 0);
	r = __gpc_command(reader, reset_emv, sizeof(reset_emv),
			  atr, size, &status);
	if (r < 0 || status == 0x00)
		return r;

	return IFD_ERROR_NO_CARD;
}

/*
 * Select the card protocol
 */
static int gpc_set_protocol(ifd_reader_t * reader, int nslot, int proto)
{
	gpc_status_t *st = (gpc_status_t *) reader->driver_data;
	ifd_slot_t *slot;

	ifd_debug(1, "called, proto=%d", proto);

	if (proto != IFD_PROTOCOL_T0 && proto != IFD_PROTOCOL_T1) {
		/* XXX FIXME - we need to call DEFINE CARD TYPE here */
		return IFD_ERROR_NOT_SUPPORTED;
	}

	slot = &reader->slot[nslot];
	slot->proto = ifd_protocol_new(IFD_PROTOCOL_TRANSPARENT,
				       reader, slot->dad);
	if (slot->proto == NULL) {
		ct_error("%s: internal error", reader->name);
		return IFD_ERROR_GENERIC;
	}

	st->icc_proto = proto;
	return 0;
}

/*
 * Change the serial speed
 */
static int gpc_set_serial(ifd_reader_t * reader, unsigned int speed, int cs,
			  int parity)
{
	unsigned char cmd[] = { 0x0A, 0x00 };

	ifd_debug(1, "called, speed=%u, cs=%d, parity=%d\n", speed, cs, parity);

	if (reader->device->type != IFD_DEVICE_TYPE_SERIAL)
		return IFD_ERROR_NOT_SUPPORTED;

	switch (speed) {
	case 1200:
		cmd[1] = 0x07;
		break;
	case 2400:
		cmd[1] = 0x06;
		break;
	case 4800:
		cmd[1] = 0x05;
		break;
	case 9600:
		cmd[1] = 0x04;
		break;
	case 19200:
		cmd[1] = 0x03;
		break;
	case 38400:
		cmd[1] = 0x02;
		break;
	case 76800:
		cmd[1] = 0x01;
		break;
	default:
		return IFD_ERROR_NOT_SUPPORTED;
	}

	switch (cs) {
	case 7:
		cmd[1] |= 0x08;
		break;
	case 8:
		break;
	default:
		return IFD_ERROR_NOT_SUPPORTED;
	}

	switch (parity) {
	case IFD_SERIAL_PARITY_EVEN:
		cmd[1] |= 0x10;
		break;
	case IFD_SERIAL_PARITY_NONE:
		break;
	default:
		return IFD_ERROR_NOT_SUPPORTED;
	}

	return gpc_command(reader, cmd, sizeof(cmd), NULL, 0);
}

/*
 * Set Reader mode
 */
static int gpc_set_mode(ifd_reader_t * reader, int mode)
{
	unsigned char cmd[] = { 0x01, 0x00, 0x00 };

	cmd[2] = mode;
	return gpc_command(reader, cmd, sizeof(cmd), NULL, 0);
}

#if 0
/*
 * Send command to IFD
 *
 * FIXME: we really need a better mechanism at the reader driver
 * API to allow for a "transceive" operation.
 */
static int gpc_send(ifd_reader_t * reader, unsigned int dad,
		    const unsigned char *buffer, size_t len)
{
	gpc_status_t *st = (gpc_status_t *) reader->driver_data;

	ifd_debug(3, "data:%s", ct_hexdump(buffer, len));

	if (len > sizeof(st->cmd_buf))
		return IFD_ERROR_BUFFER_TOO_SMALL;
	memcpy(st->cmd_buf, buffer, len);
	st->cmd_len = len;
	return 0;
}

static int gpc_recv(ifd_reader_t * reader, unsigned int dad,
		    unsigned char *res_buf, size_t res_len, long timeout)
{
	gpc_status_t *st = (gpc_status_t *) reader->driver_data;
	int rc;

	rc = gpc_transceive(reader, dad,
			    st->cmd_buf, st->cmd_len,
			    res_buf, res_len, timeout);

	if (rc > 0)
		ifd_debug(3, "received:%s", ct_hexdump(res_buf, rc));
	return rc;
}
#endif

static int gpc_transparent(ifd_reader_t * reader, int nad, const void *cmd_buf,
			   size_t cmd_len, void *res_buf, size_t res_len)
{
	return gpc_transceive(reader, nad,
			      (const unsigned char *)cmd_buf, cmd_len,
			      (unsigned char *)res_buf, res_len, 0);
}

/*
 * Generic transceive function
 */
static int gpc_transceive(ifd_reader_t * reader, unsigned int dad,
			  const unsigned char *cmd_buf, size_t cmd_len,
			  unsigned char *res_buf, size_t res_len, long timeout)
{
	gpc_status_t *st = (gpc_status_t *) reader->driver_data;
	ifd_protocol_t *proto = reader->slot[0].proto;
	long orig_timeout = 0;
	int rc;

	if (timeout) {
		ifd_protocol_get_parameter(proto,
					   IFD_PROTOCOL_RECV_TIMEOUT,
					   &orig_timeout);
		ifd_protocol_set_parameter(proto,
					   IFD_PROTOCOL_RECV_TIMEOUT,
					   timeout * 1000);
	}

	switch (st->icc_proto) {
	case IFD_PROTOCOL_T0:
		rc = gpc_transceive_t0(reader, dad,
				       cmd_buf, cmd_len, res_buf, res_len);
		break;
	case IFD_PROTOCOL_T1:
		rc = gpc_transceive_t1(reader, dad,
				       cmd_buf, cmd_len, res_buf, res_len);
		break;
	default:
		ct_error("protocol not supported\n");
		rc = IFD_ERROR_NOT_SUPPORTED;
	}

	if (orig_timeout) {
		ifd_protocol_set_parameter(proto,
					   IFD_PROTOCOL_RECV_TIMEOUT,
					   orig_timeout);
	}

	return rc;
}

static int gpc_transceive_t0(ifd_reader_t * reader, unsigned int dad,
			     const unsigned char *cmd_buf, size_t cmd_len,
			     unsigned char *res_buf, size_t res_len)
{
	ifd_iso_apdu_t iso;
	int rc;

	if ((rc = ifd_iso_apdu_parse(cmd_buf, cmd_len, &iso)) < 0)
		return rc;

	switch (iso.cse) {
	case IFD_APDU_CASE_1:
	case IFD_APDU_CASE_3S:
		rc = gpc_iso_input(reader, cmd_buf, cmd_len, res_buf, res_len);
		break;
	case IFD_APDU_CASE_2S:
		rc = gpc_iso_output(reader, cmd_buf, cmd_len, res_buf, res_len);
		break;
	case IFD_APDU_CASE_4S:
		/* The PC/SC IFD driver does an ISO EXCHANGE APDU here,
		 * but the specs I have say you can do this command only
		 * for T=1 cards.
		 * However, we shouldn't get here anyway for T=1, as the
		 * T=0 protocol driver splits case 4 APDUs. */
		rc = gpc_iso_exchange_apdu(reader, cmd_buf, cmd_len,
					   res_buf, res_len);
		break;
	default:
		ifd_debug(1, "Bad APDU (case %d unknown or unsupported)\n",
			  iso.cse);
		return IFD_ERROR_INVALID_ARG;
	}

	return rc;
}

static int gpc_transceive_t1(ifd_reader_t * reader, unsigned int dad,
			     const unsigned char *cmd_buf, size_t cmd_len,
			     unsigned char *res_buf, size_t res_len)
{
	return gpc_iso_exchange_apdu(reader, cmd_buf, cmd_len,
				     res_buf, res_len);
}

/*
 * Send partial APDU to the card
 */
static int gpc_iso_send_frag(ifd_reader_t * reader, unsigned char cmd,
			     const unsigned char *cmd_buf, size_t cmd_len)
{
	unsigned char buffer[256];

	ifd_debug(4, "called, len=%u", cmd_len);
	if (cmd_len > sizeof(buffer) - 6)
		return IFD_ERROR_INVALID_ARG;

	buffer[0] = cmd;
	buffer[1] = 0xFF;
	buffer[2] = 0xFF;
	buffer[3] = 0xFF;
	buffer[4] = 0xFF;
	buffer[5] = cmd_len;
	memcpy(buffer + 6, cmd_buf, cmd_len);

	return gpc_command(reader, buffer, 6 + cmd_len, buffer, sizeof(buffer));
}

/*
 * Receive (potentially fragmented) response from the card
 */
static int gpc_iso_recv_frag(ifd_reader_t * reader, unsigned char cmd,
			     const unsigned char *data, size_t data_len,
			     ct_buf_t * bp)
{
	static unsigned char more_data[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	unsigned char buffer[256];
	int rc, status;

	if (!data) {
		data = more_data;
		data_len = sizeof(more_data);
	} else if (data_len > sizeof(buffer) - 1) {
		return IFD_ERROR_BUFFER_TOO_SMALL;
	}

	buffer[0] = cmd;
	memcpy(buffer + 1, data, data_len);

	rc = __gpc_command(reader, buffer, 1 + data_len,
			   buffer, sizeof(buffer), &status);
	if (rc < 0)
		return rc;

	ct_buf_put(bp, buffer, rc);
	if (status != 0x00	/* success */
	    && status != 0xE7	/* not 9000 */
	    && status != 0xE5	/* not 9000 */
	    && status != 0x1B) {	/* more data */
		ct_error("error 0x%02x in ISO OUPUT/EXCHANGE APDU (%s)",
			 status, gpc_strerror(status));
		return IFD_ERROR_COMM_ERROR;
	}

	/* so exchange/send stop looping. is 1b right here? */
	if (status != 0x00 && status != 0x1B)
		return 0;

	return rc;
}

/*
 * Send case 2 APDU to card, receive lots of data in response.
 * Due to the limitations imposed by GBP, the reader may have
 * to send the response fragmented into several parts.
 */
static int gpc_iso_output(ifd_reader_t * reader, const unsigned char *cmd_buf,
			  size_t cmd_len, unsigned char *res_buf,
			  size_t res_len)
{
	ct_buf_t res;
	size_t expect = 0;
	int rc;

	ct_buf_init(&res, res_buf, res_len);
	if (cmd_len > 4) {
		expect = cmd_buf[4];
		if (expect == 0)
			expect = 256;
		expect += 2;	/* for status word */
		if (expect > res_len)
			expect = res_len;
	}

	while (1) {
		rc = gpc_iso_recv_frag(reader, 0x13, cmd_buf, cmd_len, &res);
		if (rc <= 0 || ct_buf_avail(&res) >= expect)
			break;

		cmd_buf = NULL;
		cmd_len = 0;
	}

	if (rc < 0)
		return rc;
	return ct_buf_avail(&res);
}

/*
 * Send APDU+data to card
 */
static int gpc_iso_input(ifd_reader_t * reader, const unsigned char *cmd_buf,
			 size_t cmd_len, unsigned char *res_buf, size_t res_len)
{
	unsigned char buffer[GPC_ISO_INPUT_MAX + 1];
	int rc;

	if (cmd_len > GPC_ISO_INPUT_MAX) {
		size_t chunk = cmd_len - GPC_ISO_INPUT_MAX;

		rc = gpc_iso_send_frag(reader, 0x14, cmd_buf + chunk, chunk);
		if (rc < 0)
			return rc;
		cmd_len = GPC_ISO_INPUT_MAX;
	}

	buffer[0] = 0x14;
	memcpy(buffer + 1, cmd_buf, cmd_len);

	if (cmd_len == 4)
		buffer[++cmd_len] = 0x00;

	return gpc_command(reader, buffer, cmd_len + 1, res_buf, 2);
}

static int gpc_iso_exchange_apdu(ifd_reader_t * reader,
				 const unsigned char *cmd_buf, size_t cmd_len,
				 unsigned char *res_buf, size_t res_len)
{
	ct_buf_t res;
	size_t expect = 0;
	int rc;

	ct_buf_init(&res, res_buf, res_len);
	if (cmd_len > 4) {
		expect = cmd_buf[4];
		if (expect == 0)
			expect = 256;
		expect += 2;	/* for status word */
		if (expect > res_len)
			expect = res_len;
	}

	if (cmd_len > GPC_ISO_EXCHANGE_MAX) {
		size_t chunk = cmd_len - GPC_ISO_EXCHANGE_MAX;

		rc = gpc_iso_send_frag(reader, 0x15, cmd_buf + chunk, chunk);
		if (rc < 0)
			return rc;
		cmd_len = GPC_ISO_EXCHANGE_MAX;
	}

	while (1) {
		rc = gpc_iso_recv_frag(reader, 0x15, cmd_buf, cmd_len, &res);
		if (rc <= 0 || ct_buf_avail(&res) >= expect)
			break;

		/* Added by Chaskiel - I don't understand why */
		if (ct_buf_avail(&res) == 2 && expect == 258)
			break;

		cmd_buf = NULL;
		cmd_len = 0;
	}

	if (rc < 0)
		return rc;
	return ct_buf_avail(&res);
}

/*
 * Get the OS version
 */
static int gpc_get_os_version(ifd_reader_t * reader, char *buf, size_t len)
{
	static unsigned char cmd[] = { 0x22, 0x05, 0x3F, 0xE0, 0x10 };

	/* Ensure NUL termination */
	memset(buf, 0, len);
	return gpc_command(reader, cmd, sizeof(cmd), buf, len - 1);
}

/*
 * Helper functions
 */
static int __gpc_command(ifd_reader_t * reader, const void *cmd,
			 size_t cmd_len, void *res, size_t res_len,
			 int *gpc_status)
{
	gpc_status_t *st = (gpc_status_t *) reader->driver_data;
	unsigned char buffer[257];
	size_t len;
	int rc;

	if (res_len > sizeof(buffer) - 1)
		return IFD_ERROR_GENERIC;

	if (st->p == NULL) {
		ct_error("No host-reader comm protocol selected\n");
		return IFD_ERROR_GENERIC;
	}

	if (ct_config.debug >= 3)
		ifd_debug(3, "sending:%s", ct_hexdump(cmd, cmd_len));

	rc = ifd_protocol_transceive(st->p, 0,
				     cmd, cmd_len, buffer, sizeof(buffer));
	if (rc < 0)
		return rc;
	if (rc == 0) {
		ct_error("zero length response from reader?!\n");
		return IFD_ERROR_GENERIC;
	}

	if (ct_config.debug >= 3)
		ifd_debug(3, "received:%s", ct_hexdump(buffer, rc));

	len = rc - 1;
	if (buffer[0] != 0x00) {
		ifd_debug(2, "reader reports status 0x%02x (%s)\n",
			  buffer[0], gpc_strerror(buffer[0]));
	}
	if (gpc_status)
		*gpc_status = buffer[0];

	if (len > res_len)
		len = res_len;
	if (len && res)
		memcpy(res, buffer + 1, len);
	return len;
}

static int gpc_command(ifd_reader_t * reader, const void *cmd, size_t cmd_len,
		       void *res, size_t res_len)
{
	int rc, status;

	rc = __gpc_command(reader, cmd, cmd_len, res, res_len, &status);
	if (rc >= 0 && status == 0x01)
		rc = IFD_ERROR_NOT_SUPPORTED;
	if (rc >= 0 && status != 0x00 && status != 0xE5 && status != 0xE7)
		rc = IFD_ERROR_COMM_ERROR;
	return rc;
}

/*
 * GPC error handling
 */
static const char *gpc_strerror(int status)
{
	switch (status) {
	case 0x00:
		return "Success";
	case 0x01:
		return "Unknown GemCore command";
	case 0x02:
		return "Operation impossible with this driver";
	case 0x03:
		return "Incorrect number of arguments";
	case 0x10:
		return "The first byte of the response (TS) is not valid";
	case 0x1b:
		return "More data available";
	case 0x1d:
		return "Wrong ATR TCK";
	case 0xa0:
		return "Error in card reset response";
	case 0xa1:
		return "Card protocol error";
	case 0xa2:
		return "Card is mute";
	case 0xa3:
		return "Parity error during exchange";
	case 0xa4:
		return "Card has aborted chaining (T=1)";
	case 0xa5:
		return "Reader has aborted chaining (T=1)";
	case 0xa6:
		return "RESYNCH successfully performed by GemCore";
	case 0xa7:
		return "Protocol Type Selection (PTS) error";
	case 0xa8:
		return "Card and reader in EMV mode";
	case 0xe5:
		return "Card interrupted the exchange after SW1";
	case 0xe7:
		return "\"Error\" returned by the card (SW is not 9000)";
	case 0xf7:
		return "Card removed during execution of a command";
	case 0xfb:
		return "Card missing";
	}
	return "Unknown error";
}

/*
 * Driver operations
 */
static struct ifd_driver_ops gempc_driver;

void ifd_gempc_register(void)
{
	gempc_driver.open = gpc_open;
	gempc_driver.close = gpc_close;
	gempc_driver.activate = gpc_activate;
	gempc_driver.deactivate = gpc_deactivate;
	gempc_driver.set_protocol = gpc_set_protocol;
	gempc_driver.card_status = gpc_card_status;
	gempc_driver.card_reset = gpc_card_reset;
	gempc_driver.transparent = gpc_transparent;
	ifd_driver_register("gempc", &gempc_driver);
}
