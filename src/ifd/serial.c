/*
 * I/O routines for serial devices
 *
 * Copyright (C) 2003 Olaf Kirch <okir@lst.de>
 */

#include "internal.h"
#include <sys/types.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>

static unsigned int termios_to_speed(unsigned int bits);
static unsigned int speed_to_termios(unsigned int speed);

/*
 * Reset the device
 */
static int ifd_serial_reset(ifd_device_t * dev)
{
	ifd_device_params_t params, orig_params;
	int rc;

	if ((rc = ifd_device_get_parameters(dev, &orig_params)) < 0)
		return rc;

	/* Drop DTR */
	params = orig_params;
	params.serial.speed = 0;
	params.serial.dtr = 0;
	if ((rc = ifd_device_set_parameters(dev, &params)) < 0)
		return rc;
	usleep(500000);

	/* Change back to original config */
	if ((rc = ifd_device_set_parameters(dev, &orig_params)) < 0)
		return rc;
	return 0;
}

/*
 * Get the current configuration
 */
static int ifd_serial_get_params(ifd_device_t * dev,
				 ifd_device_params_t * params)
{
	int control;
	unsigned int bits;
	struct termios t;

	memset(params, 0, sizeof(*params));

	if (tcgetattr(dev->fd, &t) < 0) {
		ct_error("%s: tcgetattr: %m", dev->name);
		return -1;
	}

	switch (t.c_cflag & CSIZE) {
	case CS5:
		bits = 5;
		break;
	case CS6:
		bits = 6;
		break;
	case CS7:
		bits = 7;
		break;
	case CS8:
		bits = 8;
		break;
	default:
		bits = 8;	/* hmmm */
	}

	params->serial.speed = termios_to_speed(cfgetospeed(&t));
	params->serial.bits = bits;
	params->serial.stopbits = (t.c_cflag & CSTOPB) ? 2 : 1;
	if (!(t.c_cflag & PARENB))
		params->serial.parity = IFD_SERIAL_PARITY_NONE;
	else if (t.c_cflag & PARODD)
		params->serial.parity = IFD_SERIAL_PARITY_ODD;
	else
		params->serial.parity = IFD_SERIAL_PARITY_EVEN;

	if ((t.c_iflag & (INPCK | PARMRK)) == (INPCK | PARMRK))
		params->serial.check_parity = 1;

	if (ioctl(dev->fd, TIOCMGET, &control) < 0) {
		ct_error("%s: TIOCMGET: %m", dev->name);
		return -1;
	}
	if (control & TIOCM_RTS)
		params->serial.rts = 1;
	if (control & TIOCM_DTR)
		params->serial.dtr = 1;

	dev->settings = *params;
	return 0;
}

/*
 * Set serial line params
 */
static int ifd_serial_set_params(ifd_device_t * dev,
				 const ifd_device_params_t * params)
{
	unsigned int speed;
	int control, ocontrol;
	struct termios t;

	if (tcgetattr(dev->fd, &t) < 0) {
		ct_error("%s: tcgetattr: %m", dev->name);
		return -1;
	}

	if (ct_config.debug) {
		char parity = 'N';

		if (params->serial.parity == IFD_SERIAL_PARITY_EVEN)
			parity = 'E';
		else if (params->serial.parity == IFD_SERIAL_PARITY_ODD)
			parity = 'O';
		ifd_debug(1, "setting serial line to %u, %u%c%u, "
			  "dtr=%d, rts=%d",
			  params->serial.speed,
			  params->serial.bits,
			  parity,
			  params->serial.stopbits,
			  params->serial.dtr, params->serial.rts);
	}

	cfsetospeed(&t, speed_to_termios(params->serial.speed));
	cfsetispeed(&t, speed_to_termios(params->serial.speed));

	t.c_cflag &= ~CSIZE;
	switch (params->serial.bits) {
	case 5:
		t.c_cflag |= CS5;
		break;
	case 6:
		t.c_cflag |= CS6;
		break;
	case 7:
		t.c_cflag |= CS7;
		break;
	default:
		t.c_cflag |= CS8;
		break;
	}

	t.c_cflag &= ~(PARENB | PARODD);
	switch (params->serial.parity) {
	case IFD_SERIAL_PARITY_EVEN:
		t.c_cflag |= PARENB;
		break;
	case IFD_SERIAL_PARITY_ODD:
		t.c_cflag |= PARENB | PARODD;
		break;
	}

	t.c_cflag &= ~CSTOPB;
	if (params->serial.stopbits > 1)
		t.c_cflag |= CSTOPB;

	t.c_iflag = IGNBRK;
	if (params->serial.check_parity)
		t.c_iflag = INPCK | PARMRK;
	else
		t.c_iflag |= IGNPAR;

#ifdef CRTSCTS
	t.c_cflag &= ~CRTSCTS;
#endif
	t.c_cflag |= HUPCL | CREAD | CLOCAL;
	t.c_oflag = 0;
	t.c_lflag = 0;

	if (tcsetattr(dev->fd, TCSANOW, &t) < 0) {
		ct_error("%s: tcsetattr: %m", dev->name);
		return -1;
	}

	if ((speed = termios_to_speed(cfgetospeed(&t))) != 0)
		dev->etu = 1000000 / speed;

	if (ioctl(dev->fd, TIOCMGET, &ocontrol) < 0) {
		ct_error("%s: TIOCMGET: %m", dev->name);
		return -1;
	}
	control = ocontrol & ~(TIOCM_DTR | TIOCM_RTS);
	if (params->serial.rts)
		control |= TIOCM_RTS;
	if (params->serial.dtr)
		control |= TIOCM_DTR;
	if (((control ^ ocontrol) & (TIOCM_DTR | TIOCM_RTS))
	    && ioctl(dev->fd, TIOCMSET, &control) < 0) {
		ct_error("%s: TIOCMGET: %m", dev->name);
		return -1;
	}

	dev->settings = *params;
	return 0;
}

/*
 * Flush pending input
 */
static void ifd_serial_flush(ifd_device_t * dev)
{
	tcflush(dev->fd, TCIFLUSH);
}

/*
 * Send a BREAK command
 */
void ifd_serial_send_break(ifd_device_t * dev, unsigned int usec)
{
	ioctl(dev->fd, TIOCSBRK);
	usleep(usec);
	ioctl(dev->fd, TIOCCBRK);
}

/*
 * Input/output routines
 */
static int ifd_serial_send(ifd_device_t * dev, const unsigned char *buffer,
			   size_t len)
{
	size_t total = len;
	int n;

	while (len) {
		n = write(dev->fd, buffer, len);
		if (n < 0) {
			ct_error("Error writing to %s: %m", dev->name);
			return -1;
		}
		buffer += n;
		len -= n;
	}

	return total;
}

static int ifd_serial_recv(ifd_device_t * dev, unsigned char *buffer,
			   size_t len, long timeout)
{
	size_t total = len, to_read;
	struct timeval begin;
	int n, last_ff = 0;

	gettimeofday(&begin, NULL);

	while (len) {
		struct pollfd pfd;
		long wait;

		if ((wait = timeout - ifd_time_elapsed(&begin)) < 0)
			goto timeout;

		pfd.fd = dev->fd;
		pfd.events = POLLIN;
		n = poll(&pfd, 1, wait);
		if (n < 0) {
			ct_error("%s: error while waiting for input: %m",
				 dev->name);
			return -1;
		}
		if (n == 0)
			continue;

		to_read = len;
		if (dev->settings.serial.check_parity)
			to_read = 1;

		n = read(dev->fd, buffer, to_read);
		if (n < 0) {
			ct_error("%s: failed to read from device: %m",
				 dev->name);
			return -1;
		}
		if (ct_config.debug >= 9)
			ifd_debug(9, "serial recv:%s", ct_hexdump(buffer, n));
		/* Check for parity errors and 0xFF */
		if (dev->settings.serial.check_parity) {
			if (last_ff) {
				if (buffer[0] == 0x00) {
					ct_error("%s: parity error on input",
						 dev->name);
					return -1;
				}
				if (buffer[0] != 0xFF) {
					ifd_debug(1,
						  "%s: unexpected character pair FF %02x",
						  dev->name, buffer[0]);
				}
				last_ff = 0;
			} else if (buffer[0] == 0xFF) {
				last_ff = 1;
				continue;
			}
		}
		buffer += n;
		len -= n;
	}

	return total;

      timeout:			/* Timeouts are a little special; they may happen e.g.
				 * when trying to obtain the ATR */
	if (!ct_config.suppress_errors)
		ct_error("%s: timed out while waiting for input", dev->name);
	ifd_debug(9, "(%u bytes received so far)", total - len);
	return IFD_ERROR_TIMEOUT;
}

/*
 * Get status of modem lines
 */
int ifd_serial_get_dtr(ifd_device_t * dev)
{
	int status;

	if (ioctl(dev->fd, TIOCMGET, &status) < 0) {
		ct_error("%s: ioctl(TIOCMGET) failed: %m", dev->name);
		return -1;
	}
	return (status & TIOCM_DTR) ? 1 : 0;
}

int ifd_serial_get_dsr(ifd_device_t * dev)
{
	int status;

	if (ioctl(dev->fd, TIOCMGET, &status) < 0) {
		ct_error("%s: ioctl(TIOCMGET) failed: %m", dev->name);
		return -1;
	}
	return (status & TIOCM_DSR) ? 1 : 0;
}

int ifd_serial_get_cts(ifd_device_t * dev)
{
	int status;

	if (ioctl(dev->fd, TIOCMGET, &status) < 0) {
		ct_error("%s: ioctl(TIOCMGET) failed: %m", dev->name);
		return -1;
	}
	return (status & TIOCM_CTS) ? 1 : 0;
}

/*
 * Close the device
 */
static void ifd_serial_close(ifd_device_t * dev)
{
	if (dev->fd >= 0)
		close(dev->fd);
	dev->fd = -1;
}

static struct ifd_device_ops ifd_serial_ops;

/*
 * Open serial device
 */
ifd_device_t *ifd_open_serial(const char *name)
{
	ifd_device_params_t params;
	ifd_device_t *dev;
	int fd;

	if ((fd = open(name, O_RDWR | O_NDELAY)) < 0) {
		ct_error("Unable to open %s: %m", name);
		return NULL;
	}

	/* Clear the NDELAY flag */
	fcntl(fd, F_SETFL, 0);

	ifd_serial_ops.reset = ifd_serial_reset;
	ifd_serial_ops.set_params = ifd_serial_set_params;
	ifd_serial_ops.get_params = ifd_serial_get_params;
	ifd_serial_ops.flush = ifd_serial_flush;
	ifd_serial_ops.send_break = ifd_serial_send_break;
	ifd_serial_ops.send = ifd_serial_send;
	ifd_serial_ops.recv = ifd_serial_recv;
	ifd_serial_ops.close = ifd_serial_close;

	dev = ifd_device_new(name, &ifd_serial_ops, sizeof(*dev));
	dev->timeout = 1000;	/* acceptable? */
	dev->type = IFD_DEVICE_TYPE_SERIAL;
	dev->fd = fd;

	memset(&params, 0, sizeof(params));
	params.serial.speed = 9600;
	params.serial.bits = 8;
	params.serial.parity = IFD_SERIAL_PARITY_NONE;
	params.serial.stopbits = 1;
	params.serial.rts = 1;
	params.serial.dtr = 1;

	ifd_serial_set_params(dev, &params);

	return dev;
}

/*
 * Map termios speed flags to speed
 */
static struct {
	unsigned int bits, speed;
} termios_speed[] = {
#ifdef B0
	{ B0, 0 },
#endif
#ifdef B50
	{ B50, 50 },
#endif
#ifdef B75
	{ B75, 75 },
#endif
#ifdef B110
	{ B110, 110 },
#endif
#ifdef B134
	{ B134, 134 },
#endif
#ifdef B150
	{ B150, 150 },
#endif
#ifdef B200
	{ B200, 200 },
#endif
#ifdef B300
	{ B300, 300 },
#endif
#ifdef B600
	{ B600, 600 },
#endif
#ifdef B1200
	{ B1200, 1200 },
#endif
#ifdef B1800
	{ B1800, 1800 },
#endif
#ifdef B2400
	{ B2400, 2400 },
#endif
#ifdef B4800
	{ B4800, 4800 },
#endif
#ifdef B9600
	{ B9600, 9600 },
#endif
#ifdef B19200
	{
	B19200, 19200 },
#endif
#ifdef B38400
	{
	B38400, 38400 },
#endif
#ifdef B57600
	{ B57600, 57600 },
#endif
#ifdef B115200
	{ B115200, 115200 },
#endif
#ifdef B230400
	{ B230400, 230400 },
#endif
	{ -1, -1 }
};

static unsigned int speed_to_termios(unsigned int speed)
{
	unsigned int n;

	for (n = 0; termios_speed[n].bits >= 0; n++) {
		if (termios_speed[n].speed >= speed) {
			return termios_speed[n].bits;
		}
	}
	return B9600;
}

static unsigned int termios_to_speed(unsigned int bits)
{
	unsigned int n;

	for (n = 0; termios_speed[n].bits >= 0; n++) {
		if (termios_speed[n].bits == bits) {
			return termios_speed[n].speed;
		}
	}

	return 0;
}
