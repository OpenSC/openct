/*
 * Driver for Towitoko readers
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>

static int twt_led(ifd_reader_t *, int);
static int twt_try_reset(ifd_reader_t *, const void *, size_t, void *, size_t);
static int twt_command(ifd_reader_t *, const void *, size_t, void *, size_t);
static int twt_recv_checksum(const unsigned char *, size_t);
static size_t twt_send_checksum(unsigned char *, size_t);

enum {
	TWT_LED_OFF = 0,
	TWT_LED_RED,
	TWT_LED_GREEN,
	TWT_LED_YELLOW
};

#define TWT_PAGESIZE	15

/*
 * Initialize the reader
 */
static int twt_open(ifd_reader_t * reader, const char *device_name)
{
	ifd_device_params_t params;
	ifd_device_t *dev;
	unsigned char buffer[256];

	ifd_debug(1, "called, device=%s", device_name);

	reader->name = "Towitoko Reader";
	reader->nslots = 1;

	if (!(dev = ifd_device_open(device_name)))
		return -1;
	reader->device = dev;

	if (dev->type == IFD_DEVICE_TYPE_SERIAL) {
		if (ifd_device_get_parameters(dev, &params) < 0)
			return -1;

		params.serial.speed = 9600;
		params.serial.bits = 8;
		params.serial.stopbits = 2;
		params.serial.parity = IFD_SERIAL_PARITY_EVEN;
		params.serial.dtr = 1;
		params.serial.rts = 1;

		if (ifd_device_set_parameters(dev, &params) < 0)
			return -1;
	}

	sleep(1);
	ifd_device_flush(dev);

	if (twt_command(reader, "\x00", 1, buffer, 2) < 0)
		goto failed;

	ifd_debug(1, "towitoko reader type 0x%02x", buffer[0]);

	/* Special handling for some towitoko readers
	 * (according to SCEZ) */
	switch (buffer[0]) {
	case 0x61:
		reader->name = "Towitoko Chipdrive Micro";
		break;
	case 0x80:		/* Kartenzwerg */
		reader->name = "Towitoko Kartenzwerg";
		params.serial.stopbits = 1;
		params.serial.parity = IFD_SERIAL_PARITY_NONE;
		/* XXX - Kartenzwerg is for synchronous cards
		 * only. Should we have a flag for this? */
		break;
	case 0x64:
		reader->name = "Towitoko Kartenzwerg II";
		params.serial.stopbits = 1;
		params.serial.parity = IFD_SERIAL_PARITY_NONE;
		break;
	case 0x84:
		reader->name = "Towitoko Chipdrive External";
		break;
	case 0x88:		/* Twin */
		reader->name = "Towitoko Chipdrive Twin";
		reader->nslots = 2;
		params.serial.rts = 0;
		break;
	case 0x90:
		reader->name = "Towitoko Chipdrive Internal";
		break;
	default:
		reader->name = "Towitoko";
	}

	if (ifd_device_set_parameters(dev, &params) < 0)
		return -1;

	return 0;

      failed:
	ct_error("towitoko: failed to initialize device");
	return -1;
}

/*
 * Activate the reader
 */
static int twt_activate(ifd_reader_t * reader)
{
	unsigned char cmd[2] = { 0x60, 0x0F };

	ifd_debug(1, "called.");
	if (twt_command(reader, cmd, sizeof(cmd), NULL, 0) < 0)
		return -1;

	return 0;
}

static int twt_deactivate(ifd_reader_t * reader)
{
	unsigned char cmd[2] = { 0x61, 0x0F };

	ifd_debug(1, "called.");
	if (twt_command(reader, cmd, sizeof(cmd), NULL, 0) < 0)
		return -1;
	return 0;
}

static int twt_close(ifd_reader_t * reader)
{
	twt_led(reader, TWT_LED_OFF);
	return 0;
}

/*
 * Check card status
 */
static int twt_card_status(ifd_reader_t * reader, int slot, int *status)
{
	unsigned char byte;
	int r;

	if (slot != 0) {
		ct_error("towitoko: bad slot index %u", slot);
		return IFD_ERROR_INVALID_SLOT;
	}

	if ((r = twt_command(reader, "\x03", 1, &byte, 1)) < 0)
		return r;

	*status = 0;
	if (byte & 0x40)
		*status |= IFD_CARD_PRESENT;
	if (byte & 0x80)
		*status |= IFD_CARD_STATUS_CHANGED;

	twt_led(reader, (byte & 0x40) ? TWT_LED_RED : TWT_LED_OFF);

	return 0;
}

/*
 * Reset the card and get the ATR
 */
static int twt_card_reset(ifd_reader_t * reader, int slot, void *atr,
			  size_t size)
{
	static unsigned char reset1[] = { 0x80, 0x6F, 0x00, 0x05, 0x76 };
	static unsigned char reset2[] = { 0xA0, 0x6F, 0x00, 0x05, 0x74 };
	int r, i, n = 0, status;

	ifd_debug(1, "called.");

	if (slot != 0) {
		ct_error("towitoko: bad slot index %u", slot);
		return IFD_ERROR_INVALID_SLOT;
	}

	/* Activate the reader */
	if ((r = twt_activate(reader)) < 0)
		return r;

	/* Get the card status */
	if ((r = twt_card_status(reader, slot, &status)) < 0)
		return r;

	if (!(status & IFD_CARD_PRESENT))
		return IFD_ERROR_NO_CARD;

	/* SCEZ does this three times - I have no clue why */
	for (i = 0; i < 1; i++) {
		n = twt_try_reset(reader, reset1, sizeof(reset1), atr, size);
		if (n != 0)
			return n;
		n = twt_try_reset(reader, reset2, sizeof(reset2), atr, size);
		if (n != 0)
			return n;
	}

	/* See if this is a synchronous card */
	return ifd_sync_detect_icc(reader, slot, atr, size);
}

static int twt_try_reset(ifd_reader_t * reader, const void *cmd, size_t cmd_len,
			 void *atr, size_t atr_len)
{
	ifd_device_t *dev = reader->device;
	int rc;

	ifd_debug(2, "sending %s", ct_hexdump(cmd, cmd_len));

	ct_config.suppress_errors++;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_SERIAL) {
		rc = ifd_device_transceive(dev, cmd, cmd_len,
					   atr, atr_len, 1000);
	} else {
		if (ifd_device_send(dev, (const unsigned char *)cmd, cmd_len) <
		    0)
			return -1;
		rc = ifd_device_recv(dev, (unsigned char *)atr, 1, 1000);
	}
	ct_config.suppress_errors--;

	if (rc == IFD_ERROR_TIMEOUT)
		return 0;

	if (rc == 1) {
		unsigned char c = *(unsigned char *)atr;

		ifd_debug(1, "received first ATR byte: 0x%02x", c);
		if (c != 0x3f && c != 0x3b && c != 0x03)
			return 0;
	}

	return rc;
}

/*
 * Change the parity
 */
static int twt_change_parity(ifd_reader_t * reader, int parity)
{
	unsigned char cmd[] = { 0x6F, 0x00, 0x6A, 0x0F };
	ifd_device_t *dev = reader->device;
	ifd_device_params_t params;
	int r;

	if (dev->type != IFD_DEVICE_TYPE_SERIAL)
		return IFD_ERROR_NOT_SUPPORTED;

	if (ifd_device_get_parameters(dev, &params) < 0)
		return -1;

	switch (parity) {
	case IFD_SERIAL_PARITY_EVEN:
		cmd[1] = 0x40;
		break;
	case IFD_SERIAL_PARITY_ODD:
		cmd[1] = 0x80;
		break;
	default:
		ct_error("towitoko: parity NONE not supported");
		return IFD_ERROR_NOT_SUPPORTED;
	}

	if ((r = twt_command(reader, cmd, 4, NULL, 0)) < 0) {
		ct_error("towitoko: failed to change parity");
		return r;
	}

	params.serial.parity = parity;
	return ifd_device_set_parameters(dev, &params);
}

/*
 * Change the serial speed
 */
static struct twt_speed {
	unsigned int value;
	unsigned char c1, c2;
} twt_speed[] = {
	{
	1200, 0x60, 0x07}, {
	2400, 0x2E, 0x03}, {
	4800, 0x17, 0x05}, {
	9600, 0x0B, 0x02}, {
	14400, 0x07, 0x01}, {
	19200, 0x05, 0x02}, {
	28800, 0x03, 0x00}, {
	38400, 0x02, 0x00}, {
	57600, 0x01, 0x00}, {
	115200, 0x80, 0x00}, {
	0}
};

static int twt_change_speed(ifd_reader_t * reader, unsigned int speed)
{
	unsigned char cmd[] = { 0x6E, 0x00, 0x00, 0x00, 0x08 };
	ifd_device_t *dev = reader->device;
	struct twt_speed *spd;
	ifd_device_params_t params;
	int r;

	if (dev->type != IFD_DEVICE_TYPE_SERIAL)
		return IFD_ERROR_NOT_SUPPORTED;

	if ((r = ifd_device_get_parameters(dev, &params)) < 0)
		return r;

	for (spd = twt_speed; spd->value; spd++) {
		if (speed <= spd->value)
			break;
	}
	if (spd->value == 0)
		return IFD_ERROR_NOT_SUPPORTED;

	params.serial.speed = spd->value;
	cmd[1] = spd->c1;
	cmd[3] = spd->c2;

	if ((r = twt_command(reader, cmd, sizeof(cmd), NULL, 0)) < 0) {
		ct_error("towitoko: failed to change speed");
		return r;
	}

	return ifd_device_set_parameters(dev, &params);
}

/*
 * Send command to IFD
 */
static int twt_send(ifd_reader_t * reader, unsigned int dad,
		    const unsigned char *buffer, size_t len)
{
	unsigned char cmd[] = { 0x6F, 0x00, 0x05, 0x00 };
	unsigned int count;
	ifd_device_t *dev;

	if (!(dev = reader->device))
		return -1;

	ifd_debug(3, "data:%s", ct_hexdump(buffer, len));
	while (len) {
		if ((count = len) > 255)
			count = 255;

		cmd[1] = count;
		twt_send_checksum(cmd, 3);

		if (ifd_device_send(dev, cmd, 4) < 0
		    || ifd_device_send(dev, buffer, count) < 0)
			return -1;

		buffer += count;
		len -= count;
	}

	return 0;
}

/*
 * Receive data from IFD
 */
static int twt_recv(ifd_reader_t * reader, unsigned int dad,
		    unsigned char *buffer, size_t len, long timeout)
{
	int n;

	n = ifd_device_recv(reader->device, buffer, len, timeout);
	if (n < 0)
		return -1;
	ifd_debug(3, "data:%s", ct_hexdump(buffer, len));
	return n;
}

/*
 * Read synchronous card
 */
static int twt_sync_read_buffer(ifd_reader_t * reader, int slot, int proto,
				unsigned char *buffer, size_t len)
{
	size_t total = 0;
	int r;

	while (total < len) {
		unsigned char cmd;
		size_t cnt;

		if ((cnt = len - total) > TWT_PAGESIZE)
			cnt = TWT_PAGESIZE;
		cmd = (cnt - 1) | 0x10;

		r = twt_command(reader, &cmd, 1, buffer + total, cnt);
		if (r < 0) {
			if (total)
				return total;
			return r;
		}

		total += cnt;
	}

	return total;
}

static int twt_sync_set_read_address(ifd_reader_t * reader, int slot, int proto,
				     unsigned short addr)
{
	unsigned char cmd_i2c_short[] =
	    { 0x7C, 0x64, 0x41, 0x00, 0x00, 0x64, 0x40, 0x00, 0x0F };
	unsigned char cmd_i2c_long[] =
	    { 0x7C, 0x64, 0x42, 0xA0, 0x00, 0x00, 0x64, 0x40, 0xA1, 0x0F };
	unsigned char cmd_2wire[] =
	    { 0x70, 0x64, 0x42, 0x30, 0x00, 0x00, 0x65, 0x0F };
	unsigned char cmd_3wire[] =
	    { 0x70, 0xA0, 0x42, 0x00, 0x00, 0x00, 0x80, 0x50, 0x0F };
	unsigned char hi, lo, *cmd;
	size_t len;

	hi = addr >> 8;
	lo = addr & 0xFF;

	switch (proto) {
	case IFD_PROTOCOL_I2C_SHORT:
		cmd = cmd_i2c_short;
		len = sizeof(cmd_i2c_short);
		cmd[3] = (hi << 1) | 0xA0;
		cmd[4] = lo;
		cmd[7] = (hi << 1) | 0xA1;
		break;

	case IFD_PROTOCOL_I2C_LONG:
		cmd = cmd_i2c_long;
		len = sizeof(cmd_i2c_long);
		cmd[4] = hi;
		cmd[5] = lo;
		break;

	case IFD_PROTOCOL_2WIRE:
		cmd = cmd_2wire;
		len = sizeof(cmd_2wire);
		cmd[4] = lo;
		break;

	case IFD_PROTOCOL_3WIRE:
		cmd = cmd_3wire;
		len = sizeof(cmd_3wire);
		cmd[3] = (hi << 6) | 0x0e;
		cmd[4] = lo;
		break;

	default:
		return IFD_ERROR_NOT_SUPPORTED;
	}

	return twt_command(reader, cmd, len, NULL, 0);
}

static int twt_sync_read(ifd_reader_t * reader, int slot, int proto,
			 unsigned short addr, unsigned char *buffer, size_t len)
{
	int r;

	if ((r = twt_sync_set_read_address(reader, slot, proto, addr)) < 0)
		return r;

	return twt_sync_read_buffer(reader, slot, proto, buffer, len);
}

/*
 * Write synchronous card
 */
static int twt_sync_write_buffer(ifd_reader_t * reader, int slot, int proto,
				 const unsigned char *buffer, size_t len)
{
	size_t total = 0;
	int r;

	while (total < len) {
		unsigned char cmd[TWT_PAGESIZE + 2];
		size_t cnt;

		if ((cnt = len - total) > TWT_PAGESIZE)
			cnt = TWT_PAGESIZE;
		cmd[0] = (cnt - 1) | 0x40;
		memcpy(cmd + 1, buffer + total, cnt);
		cmd[cnt + 1] = 0x0F;

		r = twt_command(reader, cmd, cnt + 2, NULL, 0);
		if (r < 0) {
			if (total)
				return total;
			return r;
		}

		total += cnt;
	}

	return total;
}

static int twt_sync_set_write_address(ifd_reader_t * reader, int slot,
				      int proto, unsigned short addr)
{
	unsigned char cmd_i2c_short1[] =
	    { 0x7C, 0x64, 0x41, 0xA0, 0x00, 0x64, 0x40, 0xA1, 0x0F };
	unsigned char cmd_i2c_short2[] = { 0x7E, 0x10 };
	unsigned char cmd_i2c_short3[] =
	    { 0x7E, 0x66, 0x6E, 0x00, 0x00, 0x10, 0x0F };
	unsigned char cmd_i2c_long1[] =
	    { 0x7C, 0x64, 0x42, 0xA0, 0x00, 0x00, 0x64, 0x40, 0xA1, 0x0F };
	unsigned char cmd_i2c_long2[] = { 0x7E, 0x10 };
	unsigned char cmd_i2c_long3[] =
	    { 0x7F, 0x66, 0x6E, 0x00, 0x00, 0xA0, 0x0F };
	unsigned char cmd_2wire[] = { 0x72, 0x6E, 0x00, 0x38, 0x03, 0x0F };
	unsigned char cmd_3wire[] =
	    { 0x73, 0x67, 0x6E, 0x00, 0x00, 0x02, 0x0F };
	unsigned char hi, lo, *cmd, status;
	size_t len;
	int r;

	hi = addr >> 8;
	lo = addr & 0xFF;

	switch (proto) {
	case IFD_PROTOCOL_I2C_SHORT:
		if ((r =
		     twt_command(reader, cmd_i2c_short1, sizeof(cmd_i2c_short1),
				 NULL, 0)) < 0)
			return r;
		if ((r =
		     twt_command(reader, cmd_i2c_short2, sizeof(cmd_i2c_short2),
				 &status, 1)) < 0)
			return r;

		cmd = cmd_i2c_short3;
		len = sizeof(cmd_i2c_short3);
		cmd[3] = lo;
		cmd[4] = (hi << 1) | 0xA0;
		cmd[5] = 0x00 /* pagemode */ ;
		break;

	case IFD_PROTOCOL_I2C_LONG:
		if ((r =
		     twt_command(reader, cmd_i2c_long1, sizeof(cmd_i2c_long1),
				 NULL, 0)) < 0)
			return r;
		if ((r =
		     twt_command(reader, cmd_i2c_long2, sizeof(cmd_i2c_long2),
				 &status, 1)) < 0)
			return r;

		cmd = cmd_i2c_long3;
		len = sizeof(cmd_i2c_long3);
		cmd[3] = lo;
		cmd[4] = hi;
		break;

	case IFD_PROTOCOL_2WIRE:
		cmd = cmd_2wire;
		len = sizeof(cmd_2wire);
		cmd[2] = lo;
		break;

	case IFD_PROTOCOL_3WIRE:
		cmd = cmd_3wire;
		len = sizeof(cmd_3wire);
		cmd[3] = lo;
		cmd[4] = (hi << 6) | 0x33;
		break;

	default:
		return IFD_ERROR_NOT_SUPPORTED;
	}

	return twt_command(reader, cmd, len, NULL, 0);
}

static int twt_sync_write(ifd_reader_t * reader, int slot, int proto,
			  unsigned short addr, const unsigned char *buffer,
			  size_t len)
{
	int r;

	if ((r = twt_sync_set_write_address(reader, slot, proto, addr)) < 0)
		return r;

	return twt_sync_write_buffer(reader, slot, proto, buffer, len);
}

/*
 * Turn LED on/off
 */
static int twt_led(ifd_reader_t * reader, int what)
{
	unsigned char cmd[] = { 0x6F, 0x00, 0x6A, 0x0F };

	cmd[1] = what;
	return twt_command(reader, cmd, sizeof(cmd), NULL, 0);
}

/*
 * Helper functions
 */
static int twt_command(ifd_reader_t * reader, const void *cmd, size_t cmd_len,
		       void *res, size_t res_len)
{
	unsigned char buffer[254];
	int rc;

	if (res_len > sizeof(buffer) - 1 || cmd_len > sizeof(buffer) - 1)
		return IFD_ERROR_BUFFER_TOO_SMALL;

	memcpy(buffer, cmd, cmd_len);
	cmd_len = twt_send_checksum(buffer, cmd_len);

	if (ct_config.debug > 1)
		ifd_debug(3, "sending:%s", ct_hexdump(buffer, cmd_len));

	rc = ifd_device_transceive(reader->device,
				   buffer, cmd_len, buffer, res_len + 1, -1);
	if (rc < 0) {
		ct_error("towitoko: transceive error: %s", ct_strerror(rc));
		return rc;
	}

	if (ct_config.debug > 1)
		ifd_debug(3, "received:%s", ct_hexdump(buffer, res_len + 1));

	if (!twt_recv_checksum(buffer, res_len + 1)) {
		ct_error("towitoko: command failed (bad checksum)");
		return -1;
	}

	if (res && res_len)
		memcpy(res, buffer, res_len);

	return 0;
}

static unsigned char twt_checksum(unsigned char cs, const unsigned char *data,
				  size_t len)
{
	unsigned char b;

	while (len--) {
		b = cs ^ *data++;
		/* rotate left one bit and toggle LSB */
		cs = ((b << 1) | (b >> 7)) ^ 0x01;
	}
	return cs;
}

static int twt_recv_checksum(const unsigned char *data, size_t len)
{
	if (len == 0)
		return 0;

	return data[len - 1] == twt_checksum(0x01, data, len - 1);
}

static size_t twt_send_checksum(unsigned char *data, size_t len)
{
	data[len] = twt_checksum(0x00, data, len);
	return len + 1;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops towitoko_driver;

void ifd_towitoko_register(void)
{
	towitoko_driver.open = twt_open;
	towitoko_driver.close = twt_close;
	towitoko_driver.change_parity = twt_change_parity;
	towitoko_driver.change_speed = twt_change_speed;
	towitoko_driver.activate = twt_activate;
	towitoko_driver.deactivate = twt_deactivate;
	towitoko_driver.card_status = twt_card_status;
	towitoko_driver.card_reset = twt_card_reset;
	towitoko_driver.send = twt_send;
	towitoko_driver.recv = twt_recv;
	towitoko_driver.sync_read = twt_sync_read;
	towitoko_driver.sync_write = twt_sync_write;

	ifd_driver_register("towitoko", &towitoko_driver);
}
