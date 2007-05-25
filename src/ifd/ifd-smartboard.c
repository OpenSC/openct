/*
 * Driver for Cherry smartboard
 *
 * This was written just by looking at the Smartboard protocol
 * on the wire.
 *
 * Some notes on the Smartboard protocol.
 * The basic message format seems to be
 *
 *	00 [len] [code]
 *
 * 00	Seems to be some general "I'm okay, you're okay" byte.
 *	Never seen anything else, but maybe it can be used to
 *	convey some critical errors etc.
 * len	One byte; length of following data
 * code	One byte; message code.
 *      Followed by data
 *
 * I have no clue yet how to use the num block for PIN entry...
 *
 * This driver is alpha code - it works for me with a Cryptoflex card,
 * but that doesn't mean a thing :)
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <sys/ioctl.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ctbcs.h"

static int smartboard_reset_ct(ifd_reader_t * reader);
static int smartboard_command(ifd_reader_t *,
			      unsigned char, const unsigned char *, size_t,
			      unsigned char *, void *, size_t);
static int __smartboard_cmd(ifd_reader_t *, unsigned char, const void *,
			    size_t);
static int __smartboard_rsp(ifd_reader_t *, unsigned char *, void *, size_t);

/*
 * Initialize the device
 */
static int smartboard_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_params_t params;
	ifd_device_t *dev;
	int res;

	reader->name = "Cherry Smartboard";
	reader->nslots = 1;
	reader->slot[0].dad = 0;

	if (!(dev = ifd_device_open(device_name)))
		return -1;

	ifd_device_flush(dev);

	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_SERIAL) {
		ct_error("Smartboard: must be a serial device");
		return -1;
	}
	if ((res = ifd_device_get_parameters(dev, &params)) < 0) {
		ct_error("Smartboard: failed to get serial config");
		return res;
	}

	params.serial.bits = 8;
	params.serial.parity = IFD_SERIAL_PARITY_EVEN;
	params.serial.stopbits = 2;
	params.serial.speed = 115200;
	params.serial.check_parity = 1;
	if ((res = ifd_device_set_parameters(dev, &params)) < 0) {
		ct_error("Smartboard: failed to get serial line to 115200/8E2");
		return res;
	}

	/* Toggle RTS briefly */
	{
		int bits = 0x4000;

		usleep(230000);
		ioctl(dev->fd, TIOCMSET, &bits);
		usleep(230000);
		bits |= TIOCM_DTR;
		ioctl(dev->fd, TIOCMSET, &bits);
		usleep(230000);
		bits |= TIOCM_RTS;
		ioctl(dev->fd, TIOCMSET, &bits);
		usleep(100000);
	}

	ifd_serial_send_break(dev, 500000);
	ifd_device_flush(dev);

	reader->device = dev;

	/* Reset the CT */
	if ((res = smartboard_reset_ct(reader)) < 0)
		return res;

	return 0;
}

/*
 * Reset the card reader
 */
static int smartboard_reset_ct(ifd_reader_t * reader)
{
	unsigned char buffer[128], code;
	int rc;

#if 1
	/* shutdown reader - occasionally needed before we can init it */
	rc = smartboard_command(reader, 0x6a, NULL, 0, &code, NULL, 0);
	if (rc < 0)
		return rc;
#endif

	/* init reader */
	rc = smartboard_command(reader, 0x60, NULL, 0, &code, buffer,
				sizeof(buffer));
	if (rc < 0)
		return rc;
	if (code != 0x60) {
		ct_error("smartboard_reset_ct, expected status 0x60, got 0x%x",
			 code);
		return -1;
	}
	ifd_debug(1, "Detected %.*s", rc, buffer);
	return 0;
}

/*
 * Power up the reader
 */
static int smartboard_activate(ifd_reader_t * reader)
{
	ifd_debug(1, "called.");
	return 0;
}

static int smartboard_deactivate(ifd_reader_t * reader)
{
	ifd_debug(1, "called.");
	return 0;
}

/*
 * Get the card status
 */
static int smartboard_card_status(ifd_reader_t * reader, int idx, int *status)
{
	unsigned char code, buffer[16];
	int rc;

	ifd_debug(1, "slot=%d", idx);
	rc = smartboard_command(reader, 0x65, NULL, 0, &code, buffer,
				sizeof(buffer));
	if (rc < 0)
		return rc;

	*status = 0;
	switch (code) {
	case 0x61:
		/* card absent:  00 00 00 01
		 * card present: 08 00 00 02
		 * after reset:  19 01 00 04
		 */
		if (rc >= 4 && (buffer[0] & 0x08))
			*status = IFD_CARD_PRESENT;
		break;
	case 0x65:
		ifd_debug(1, "event: card inserted.");
		*status = IFD_CARD_PRESENT | IFD_CARD_STATUS_CHANGED;
		break;
	case 0x66:
		ifd_debug(1, "event: card removed.");
		*status = IFD_CARD_STATUS_CHANGED;
		break;
	default:
		ct_error("smartboard_card_status: unexpected status code 0x%x",
			 code);
		return -1;
	}

	return 0;
}

/*
 * Reset card and get ATR
 */
static int smartboard_card_reset(ifd_reader_t * reader, int slot, void *result,
				 size_t size)
{
	unsigned char code;
	int rc;

	rc = smartboard_command(reader, 0x65, NULL, 0, &code, result, size);
	if (rc < 0)
		return rc;

	rc = smartboard_command(reader, 0x62, NULL, 0, &code, result, size);
	if (rc < 0)
		return rc;

	if (code != 0x64) {
		ct_error
		    ("smartboard_card_reset: expected status code 0x62, got 0x%x",
		     code);
		return -1;
	}
	return rc;
}

/*
 * Select a protocol for communication with the ICC.
 * We cannot use the T=0 driver directly, because it thinks it can
 * talk over the wire.
 */
static int smartboard_set_protocol(ifd_reader_t * reader, int nslot, int proto)
{
	unsigned char cmd_t0[5] = { 0x00, 0x00, 0x0a, 0x00, 0x10 };
	unsigned char cmd_t1[5] = { 0x10, 0x00, 0x00, 0x75, 0x10 };
	unsigned char *args, code;
	ifd_slot_t *slot;
	int rc;

	slot = &reader->slot[nslot];
	if (proto == IFD_PROTOCOL_T0) {
		args = cmd_t0;
	} else if (proto == IFD_PROTOCOL_T1) {
		args = cmd_t1;
	} else {
		ct_error("%s: protocol not supported", reader->name);
		return -1;
	}

	if ((rc =
	     smartboard_command(reader, 0x61, args, 5, &code, NULL, 0)) < 0)
		return rc;
	if (code != 0x62) {
		ct_error("smartboard: unexpected status code 0x%x", code);
		return -1;
	}

	slot->proto = ifd_protocol_new(proto, reader, slot->dad);
	if (slot->proto == NULL) {
		ct_error("%s: internal error", reader->name);
		return -1;
	}

	/* Tell the protocol handler that we will do the framing */
	ifd_protocol_set_parameter(slot->proto, IFD_PROTOCOL_BLOCK_ORIENTED, 1);

	return 0;
}

#if 0
/*
 * Perform a PIN verification
 */
static int smartboard_perform_verify(ifd_reader_t * reader, int nslot,
				     unsigned int timeout, const char *prompt,
				     const unsigned char *data, size_t data_len,
				     unsigned char *resp, size_t resp_len)
{
...}
#endif

/*
 * Simple command
 */
static int __smartboard_cmd(ifd_reader_t * reader, unsigned char cmd,
			    const void *arg, size_t arg_len)
{
	unsigned char buffer[257];

	if (arg_len > sizeof(buffer) - 3)
		return -1;

	buffer[0] = 0x00;	/* never seen anything other than this */
	buffer[1] = 1 + arg_len;
	buffer[2] = cmd;
	memcpy(buffer + 3, arg, arg_len);

	if (ct_config.debug > 1)
		ifd_debug(3, "sending:%s", ct_hexdump(buffer, 3 + arg_len));

	return ifd_device_send(reader->device, buffer, 3 + arg_len);
}

static int __smartboard_rsp(ifd_reader_t * reader, unsigned char *code,
			    void *res, size_t res_len)
{
	unsigned char buffer[257];
	unsigned int rsp_len = 0, total = 2;
	int rc;

	while (rsp_len < total) {
		rc = ifd_device_recv(reader->device, buffer + rsp_len,
				     total - rsp_len, -1);
		if (rc < 0)
			return rc;
		if (buffer[0] != 0x00)
			goto bad_reply;
		rsp_len += rc;
		if (rsp_len == 2) {
			if (buffer[1] == 0)
				goto bad_reply;
			total += buffer[1];
		}
	}

	if (total < 3)
		goto bad_reply;
	*code = buffer[2];

	if (ct_config.debug > 1)
		ifd_debug(3, "received:%s", ct_hexdump(buffer, total));

	rsp_len = total - 3;
	if (res_len > rsp_len)
		res_len = rsp_len;
	if (res && res_len)
		memcpy(res, buffer + 3, res_len);

	return res_len;

      bad_reply:
	ct_error("smartboard: bad reply from device");
	return -1;
}

static int smartboard_command(ifd_reader_t * reader, unsigned char cmd,
			      const unsigned char *arg, size_t arg_len,
			      unsigned char *code, void *res, size_t res_len)
{
	int n = 0, rc;

	do {
		if ((rc = __smartboard_cmd(reader, cmd, arg, arg_len)) < 0
		    || (rc = __smartboard_rsp(reader, code, res, res_len)) < 0)
			ct_error("smartboard: transceive error");
	} while (rc >= 0 && *code == 0x67 && n++ < 3);

	return rc;
}

/*
 * Send/receive APDU
 */
static int smartboard_send(ifd_reader_t * reader, unsigned int dad,
			   const unsigned char *buffer, size_t len)
{
	ifd_debug(3, "data:%s", ct_hexdump(buffer, len));
	return __smartboard_cmd(reader, 0x67, buffer, len);
}

static int smartboard_recv(ifd_reader_t * reader, unsigned int dad,
			   unsigned char *buffer, size_t len, long timeout)
{
	unsigned char code;
	int rc;

	ifd_debug(4, "called.");

	/* Status code 63 seems to be some sort of wait time extension */
	while (1) {
		if ((rc = __smartboard_rsp(reader, &code, buffer, len)) < 0)
			return rc;
		if (code != 0x63)
			break;
	}

	if (code != 0x64) {
		ct_error("smartboard: unexpected status code 0x%x", code);
		return -1;
	}

	ifd_debug(3, "resp:%s", ct_hexdump(buffer, rc));
	return rc;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops smartboard_driver;

/*
 * Initialize this module
 */
void ifd_smartboard_register(void)
{
	smartboard_driver.open = smartboard_open,
	    smartboard_driver.activate = smartboard_activate,
	    smartboard_driver.deactivate = smartboard_deactivate,
	    smartboard_driver.card_status = smartboard_card_status,
	    smartboard_driver.card_reset = smartboard_card_reset,
#ifdef notyet
	    smartboard_driver.perform_verify = smartboard_perform_verify,
#endif
	    smartboard_driver.send = smartboard_send,
	    smartboard_driver.recv = smartboard_recv,
	    smartboard_driver.set_protocol = smartboard_set_protocol,
	    ifd_driver_register("smartboard", &smartboard_driver);
}
