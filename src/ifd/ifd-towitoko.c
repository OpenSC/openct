/*
 * Driver for Towitoko readers
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <openct/ifd.h>
#include <openct/driver.h>
#include <openct/device.h>
#include <openct/logging.h>
#include <openct/conf.h>
#include <openct/error.h>

#define DEBUG(fmt, args...) \
	do { ct_debug("%s: "  fmt, __FUNCTION__ , ##args); } while (0)
		

static int		twt_led(ifd_reader_t *, int);
static int		twt_try_reset(ifd_reader_t *,
				const void *, size_t,
				void *, size_t);
static int		twt_command(ifd_reader_t *,
				const char *, size_t,
				void *, size_t);
static int		twt_recv_checksum(const unsigned char *, size_t);
static unsigned int	twt_send_checksum(unsigned char *, size_t);

enum {
	TWT_LED_OFF = 0,
	TWT_LED_RED,
	TWT_LED_GREEN,
	TWT_LED_YELLOW,
};

/*
 * Initialize the reader
 */
static int
twt_open(ifd_reader_t *reader, const char *device_name)
{
	ifd_device_params_t params;
	ifd_device_t	*dev;
	unsigned char	buffer[256];

	DEBUG("called, device=%s", device_name);

	reader->name = "Towitoko Reader";
	reader->nslots = 1;

	if (!(dev = ifd_device_open(device_name)))
		return -1;
	reader->device = dev;

	if (ifd_device_get_parameters(dev, &params) < 0)
		return -1;

	params.serial.speed = 9600;
	params.serial.bits  = 8;
	params.serial.stopbits = 2;
	params.serial.parity = IFD_SERIAL_PARITY_EVEN;

	if (ifd_device_set_parameters(dev, &params) < 0)
		return -1;

	sleep(1);
	ifd_device_flush(dev);

	if (ifd_device_transceive(dev, "\x00\x01", 2, buffer, 3, -1) < 0
	 || !twt_recv_checksum(buffer, 3))
		goto failed;

	DEBUG("init command returns 0x%02x 0x%02x", buffer[0], buffer[1]);

	/* Special handling for some towitoko readers
	 * (according to SCEZ) */
	switch (buffer[0]) {
	case 0x61:
	case 0x64:
		reader->name = "Towitoko Chipdrive Micro";
		break;
	case 0x80: /* Kartenzwerg */
		reader->name = "Towitoko Kartenzwerg";
		params.serial.stopbits = 1;
		params.serial.parity = IFD_SERIAL_PARITY_NONE;
		/* XXX - Kartenzwerg is for synchronous cards
		 * only. Should we have a flag for this? */
		break;
	case 0x84:
		reader->name = "Towitoko Chipdrive External";
		break;
	case 0x88: /* Twin */
		reader->name = "Towitoko Chipdrive Twin";
		params.serial.rts = 0;
		break;
	case 0x90:
		reader->name = "Towitoko Chipdrive Internal";
		break;
	}

	if (ifd_device_set_parameters(dev, &params) < 0)
		return -1;

	return 0;

failed: ct_error("towitoko: failed to initialize device");
	return -1;
}

/*
 * Activate the reader
 */
static int
twt_activate(ifd_reader_t *reader)
{
	DEBUG("called.");

	if (twt_command(reader, "\x60\x0F", 2, NULL, 0) < 0)
		return -1;

	return 0;
}

static int
twt_deactivate(ifd_reader_t *reader)
{
	DEBUG("called.");

	if (twt_command(reader, "\x61\x0F", 2, NULL, 0) < 0)
		return -1;
	return 0;
}

static int
twt_close(ifd_reader_t *reader)
{
	twt_led(reader, TWT_LED_OFF);
	return 0;
}

/*
 * Check card status
 */
static int
twt_card_status(ifd_reader_t *reader, int slot, int *status)
{
	unsigned char	byte;

	if (slot != 0) {
		ct_error("towitoko: bad slot index %u", slot);
		return -1;
	}

	if (twt_command(reader, "\x03", 1, &byte, 1) < 0)
		return -1;

	*status = 0;
	if (byte & 0x40)
		*status |= IFD_CARD_PRESENT;
	if (byte & 0x80)
		*status |= IFD_CARD_STATUS_CHANGED;

	twt_led(reader, (byte & 0x40)? TWT_LED_GREEN : TWT_LED_OFF);

	return 0;
}

/*
 * Reset the card and get the ATR
 */
static int
twt_card_reset(ifd_reader_t *reader, int slot, void *atr, size_t size)
{
	static unsigned char reset1[] = { 0x80, 0x6F, 0x00, 0x05, 0x76 };
	static unsigned char reset2[] = { 0xA0, 0x6F, 0x00, 0x05, 0x74 };
	int	i, n;

	DEBUG("called.");

	if (slot != 0) {
		ct_error("towitoko: bad slot index %u", slot);
		return -1;
	}

	/* SCEZ does this tree times - I have no clue why */
	for (i = 0; i < 3; i++) {
		n = twt_try_reset(reader, reset1, sizeof(reset1), atr, size);
		if (n != 0)
			break;
		n = twt_try_reset(reader, reset2, sizeof(reset2), atr, size);
		if (n != 0)
			break;
	}

	return n;
}

int
twt_try_reset(ifd_reader_t *reader,
		const void *cmd, size_t cmd_len,
		void *atr, size_t atr_len)
{
	ifd_device_t *dev = reader->device;
	int rc;

	if (ct_config.debug > 1)
		DEBUG("sending %s", ct_hexdump(cmd, cmd_len));

	ct_config.hush_errors++;
	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_SERIAL) {
		rc = ifd_device_transceive(dev, cmd, cmd_len,
						atr, atr_len, 1000);
	} else {
		if (ifd_device_send(dev, cmd, cmd_len) < 0)
			return -1;
		rc = ifd_device_recv(dev, atr, 1, 1000);
	}
	ct_config.hush_errors--;

	if (rc == IFD_ERROR_TIMEOUT)
		return 0;

	if (rc == 1) {
		unsigned char c = *(unsigned char *) atr;

		DEBUG("got first byte of ATR %02x", c);
		if (c != 0x3f && c != 0x3b && c != 0x03)
			return 0;
	}

	return rc;
}

/*
 * Change the parity
 */
static int
twt_change_parity(ifd_reader_t *reader, int parity)
{
	unsigned char	cmd[] = { 0x6F, 0x00, 0x6A, 0x0F };
	ifd_device_t	*dev = reader->device;
	ifd_device_params_t params;

	if (ifd_device_get_parameters(dev, &params) < 0)
		return -1;
	if (params.serial.parity == parity)
		return 0;

	switch (parity) {
	case IFD_SERIAL_PARITY_EVEN:
		cmd[1] = 0x40;
		break;
	case IFD_SERIAL_PARITY_ODD:
		cmd[1] = 0x80;
		break;
	default:
		ct_error("towitoko: parity NONE not supported");
		return -1;
	}

	if (twt_command(reader, cmd, 4, NULL, 0) < 0) {
		ct_error("towitoko: failed to change parity");
		return -1;
	}

	params.serial.parity = parity;
	return ifd_device_set_parameters(dev, &params);
}

/*
 * Send command to IFD
 */
static int
twt_send(ifd_reader_t *reader, unsigned int dad,
		const void *buffer, size_t len)
{
	unsigned char	cmd[] = { 0x6F, 0x00, 0x05, 0x00 };
	unsigned int	count;
	ifd_device_t	*dev;

	if (!(dev = reader->device))
		return -1;

	DEBUG("data:%s", ct_hexdump(buffer, len));
	while (len) {
		if ((count = len) > 255)
			count = 255;

		cmd[1] = count;
		twt_send_checksum(cmd, 3);

		if (ifd_device_send(dev, cmd, 4) < 0
		 || ifd_device_send(dev, buffer, count) < 0)
			return -1;

		(caddr_t) buffer += count;
		len -= count;
	}

	return 0;
}

/*
 * Receive data from IFD
 */
static int
twt_recv(ifd_reader_t *reader, unsigned int dad,
		void *buffer, size_t len, long timeout)
{
	int	n;

	n = ifd_device_recv(reader->device, buffer, len, timeout);
	if (n < 0)
		return -1;
	DEBUG("data:%s", ct_hexdump(buffer, len));
	return n;
}

/*
 * Turn LED on/off
 */
int
twt_led(ifd_reader_t *reader, int what)
{
	unsigned char	cmd[] = { 0x6F, 0x00, 0x6A, 0x0F };

	cmd[1] = what;
	return twt_command(reader, cmd, sizeof(cmd), NULL, 0);
}

/*
 * Helper functions
 */
int
twt_command(ifd_reader_t *reader, const char *cmd, size_t cmd_len,
		void *res, size_t res_len)
{
	unsigned char	buffer[254];
	int		rc;

	if (ct_config.debug > 1)
		DEBUG("sending:%s", ct_hexdump(cmd, cmd_len));

	if (res_len > sizeof(buffer)-1
	 || cmd_len > sizeof(buffer)-1)
		return -1;

	memcpy(buffer, cmd, cmd_len);
	cmd_len = twt_send_checksum(buffer, cmd_len);

	rc = ifd_device_transceive(reader->device,
					buffer, cmd_len,
					buffer, res_len + 1, -1);
	if (rc < 0) {
		ct_error("towitoko: transceive error");
		return -1;
	}

	if (ct_config.debug > 1)
		DEBUG("received:%s", ct_hexdump(buffer, res_len + 1));

	if (!twt_recv_checksum(buffer, res_len + 1)) {
		ct_error("towitoko: command failed (bad checksum)");
		return -1;
	}

	if (res && res_len)
		memcpy(res, buffer, res_len);

	return 0;
}

static unsigned char
twt_checksum(unsigned char cs, const unsigned char *data, size_t len)
{
	unsigned char	b;

	while (len--) {
		b = cs ^ *data++;
		/* rotate left one bit an toggle LSB */
		cs = ((b << 1) | (b >> 7)) ^ 0x01;
	}
	return cs;
}

int
twt_recv_checksum(const unsigned char *data, size_t len)
{
	if (len == 0)
		return 0;

	return data[len-1] == twt_checksum(0x01, data, len - 1);
}

unsigned int
twt_send_checksum(unsigned char *data, size_t len)
{
	data[len] = twt_checksum(0x00, data, len);
	return len + 1;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops	towitoko_driver = {
	open:		twt_open,
	close:		twt_close,
	change_parity:	twt_change_parity,
	activate:	twt_activate,
	deactivate:	twt_deactivate,
	card_status:	twt_card_status,
	card_reset:	twt_card_reset,
	send:		twt_send,
	recv:		twt_recv,
};

void
ifd_towitoko_register(void)
{
	ifd_driver_register("towitoko", &towitoko_driver);
}
