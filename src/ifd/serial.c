/*
 * I/O routines for serial devices
 *
 * Copyright (C) 2003 Olaf Kirch <okir@lst.de>
 */

#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>

#include "internal.h"

static unsigned int termios_to_speed(unsigned int bits);
static unsigned int speed_to_termios(unsigned int speed);

/*
 * Get the current configuration
 */
static int
ifd_serial_get_params(ifd_device_t *dev, ifd_device_params_t *params)
{
	int		control;
	unsigned int	bits;
	struct termios	t;

	memset(params, 0, sizeof(*params));

	if (tcgetattr(dev->fd, &t) < 0) {
		ct_error("%s: tcgetattr: %m", dev->name);
		return -1;
	}

	switch (t.c_cflag & CSIZE) {
	case CS5: bits = 5; break;
	case CS6: bits = 6; break;
	case CS7: bits = 7; break;
	case CS8: bits = 8; break;
	default:  bits = 8; /* hmmm */
	}

	params->serial.speed = termios_to_speed(cfgetospeed(&t));
	params->serial.bits = bits;
	params->serial.stopbits = (t.c_cflag & CSTOPB)? 2 : 1;
	if (!(t.c_cflag & PARENB))
		params->serial.parity = IFD_SERIAL_PARITY_NONE;
	else if (t.c_cflag & PARODD)
		params->serial.parity = IFD_SERIAL_PARITY_ODD;
	else
		params->serial.parity = IFD_SERIAL_PARITY_EVEN;

	if (ioctl(dev->fd, TIOCMGET, &control) < 0) {
		ct_error("%s: TIOCMGET: %m", dev->name);
		return -1;
	}
	if (control & TIOCM_RTS) params->serial.rts = 1;
	if (control & TIOCM_DTR) params->serial.dtr = 1;

	dev->settings = *params;
	return 0;
}

/*
 * Set serial line params
 */
static int
ifd_serial_set_params(ifd_device_t *dev, const ifd_device_params_t *params)
{
	unsigned int	speed;
	int		control;
	struct termios	t;

	if (!memcmp(&dev->settings, params, sizeof(*params)))
		goto skip_setattr;

	if (tcgetattr(dev->fd, &t) < 0) {
		ct_error("%s: tcgetattr: %m", dev->name);
		return -1;
	}

	cfsetospeed(&t, speed_to_termios(params->serial.speed));

	t.c_cflag &= ~CSIZE;
	switch (params->serial.bits) {
	case 5:  t.c_cflag |= CS5; break;
	case 6:  t.c_cflag |= CS6; break;
	case 7:  t.c_cflag |= CS7; break;
	default: t.c_cflag |= CS8; break;
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

	t.c_iflag = IGNBRK | IGNPAR;
	t.c_oflag = 0;
	t.c_cflag |= HUPCL | CREAD | CLOCAL;
	t.c_lflag = 0;

	if (tcsetattr(dev->fd, TCSANOW, &t) < 0) {
		ct_error("%s: tcsetattr: %m", dev->name);
		return -1;
	}

	if ((speed = termios_to_speed(cfgetospeed(&t))) != 0)
		dev->etu = 1000000 / speed;


skip_setattr:
	if (ioctl(dev->fd, TIOCMGET, &control) < 0) {
		ct_error("%s: TIOCMGET: %m", dev->name);
		return -1;
	}
	control &= ~(TIOCM_DTR | TIOCM_RTS);
	if (params->serial.rts) control |= TIOCM_RTS;
	if (params->serial.dtr) control |= TIOCM_DTR;
	if (ioctl(dev->fd, TIOCMSET, &control) < 0) {
		ct_error("%s: TIOCMGET: %m", dev->name);
		return -1;
	}

	return 0;
}

/*
 * Flush pending input
 */
static void
ifd_serial_flush(ifd_device_t *dev)
{
	tcflush(dev->fd, TCIFLUSH);
}

/*
 * Input/output routines
 */
static int
ifd_serial_send(ifd_device_t *dev, const unsigned char *buffer, size_t len)
{
	size_t		total = len;
	int		n;

	while (len) {
		n = write(dev->fd, buffer, len);
		if (n < 0) {
			ct_error("Error writing to %s: %m",
					dev->name);
			return -1;
		}
		(caddr_t) buffer += n;
		len -= n;
	}

	return total;
}

static int
ifd_serial_recv(ifd_device_t *dev, unsigned char *buffer, size_t len, long timeout)
{
	size_t		total = len;
	struct timeval	begin;
	int		n;

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
		n = read(dev->fd, buffer, len);
		if (n < 0) {
			ct_error("%s: failed to read from device: %m",
					dev->name);
			return -1;
		}
		(caddr_t) buffer += n;
		len -= n;
	}

	return total;

timeout:/* Timeouts are a little special; they may happen e.g.
	 * when trying to obtain the ATR */
	if (!ct_config.hush_errors)
		ct_error("%s: timed out while waiting for input",
				dev->name);
	return IFD_ERROR_TIMEOUT;
}

/*
 * Get status of modem lines
 */
int
ifd_serial_get_dtr(ifd_device_t *dev)
{
	int	status;

	if (ioctl(dev->fd, TIOCMGET, &status) < 0) {
		ct_error("%s: ioctl(TIOCMGET) failed: %m", dev->name);
		return -1;
	}
	return (status & TIOCM_DTR)? 1 : 0;
}

int
ifd_serial_get_dsr(ifd_device_t *dev)
{
	int	status;

	if (ioctl(dev->fd, TIOCMGET, &status) < 0) {
		ct_error("%s: ioctl(TIOCMGET) failed: %m", dev->name);
		return -1;
	}
	return (status & TIOCM_DSR)? 1 : 0;
}

int
ifd_serial_get_cts(ifd_device_t *dev)
{
	int	status;

	if (ioctl(dev->fd, TIOCMGET, &status) < 0) {
		ct_error("%s: ioctl(TIOCMGET) failed: %m", dev->name);
		return -1;
	}
	return (status & TIOCM_CTS)? 1 : 0;
}

/*
 * Identify attached device
 */
static int
ifd_serial_identify(ifd_device_t *dev, char *namebuf, size_t len)
{
	ct_error("Serial PNP not yet implemented");
	return -1;
}

/*
 * Close the device
 */
void
ifd_serial_close(ifd_device_t *dev)
{
	if (dev->fd >= 0)
		close(dev->fd);
	dev->fd = -1;
}

static struct ifd_device_ops ifd_serial_ops = {
	identify:	ifd_serial_identify,
	flush:		ifd_serial_flush,
	get_params:	ifd_serial_get_params,
	set_params:	ifd_serial_set_params,
	send:		ifd_serial_send,
	recv:		ifd_serial_recv,
	close:		ifd_serial_close
};

/*
 * Open serial device
 */
ifd_device_t *
ifd_open_serial(const char *name)
{
	ifd_device_params_t params;
	ifd_device_t	*dev;
	int		fd;

	if ((fd = open(name, O_RDWR|O_NDELAY)) < 0) {
		ct_error("Unable to open %s: %m", name);
		return NULL;
	}

	/* Clear the NDELAY flag */
	fcntl(fd, F_SETFL, 0);

	dev = ifd_device_new(name, &ifd_serial_ops, sizeof(*dev));
	dev->timeout = 1000; /* acceptable? */
	dev->type = IFD_DEVICE_TYPE_SERIAL;
	dev->fd = fd;

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
	int	bits, speed;
} termios_speed[] = {
      {	B0,		0 },
      {	B50,		50 },
      {	B75,		75 },
      {	B110,		110 },
      {	B134,		134 },
      {	B150,		150 },
      {	B200,		200 },
      {	B300,		300 },
      {	B600,		600 },
      {	B1200,		1200 },
      {	B1800,		1800 },
      {	B2400,		2400 },
      {	B4800,		4800 },
      {	B9600,		9600 },
      {	B19200,		19200 },
      {	B38400,		38400 },
      {	B57600,		57600 },
      {	B115200,	115200 },
      {	B230400,	230400 },
      { -1, -1 }
};

unsigned int
speed_to_termios(unsigned int speed)
{
	unsigned int	n;

	for (n = 0; termios_speed[n].bits >= 0; n++) {
		if (termios_speed[n].speed >= speed) {
			return termios_speed[n].bits;
		}
	}
	return B9600;
}

unsigned int
termios_to_speed(unsigned int bits)
{
	unsigned int n;

	for (n = 0; termios_speed[n].bits >= 0; n++) {
		if (termios_speed[n].bits == bits) {
			return termios_speed[n].speed;
		}
	}

	return 0;
}
