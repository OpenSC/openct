/*
 * I/O routines for serial devices
 *
 * Copyright (C) 2003 Olaf Kirch <okir@lst.de>
 */

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

typedef struct ifd_serial {
	ifd_device_t	base;

	int		fd;
} ifd_serial_t;

static unsigned int termios_to_speed(unsigned int bits);
static unsigned int speed_to_termios(unsigned int speed);

/*
 * Get the current configuration
 */
static int
ifd_serial_get_params(ifd_device_t *dp, ifd_device_params_t *params)
{
	ifd_serial_t	*dev = (ifd_serial_t *) dp;
	int		control;
	unsigned int	bits;
	struct termios	t;

	memset(params, 0, sizeof(*params));

	if (tcgetattr(dev->fd, &t) < 0) {
		ifd_error("%s: tcgetattr: %m", dp->name);
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
		ifd_error("%s: TIOCMGET: %m", dp->name);
		return -1;
	}
	if (control & TIOCM_RTS) params->serial.rts = 1;
	if (control & TIOCM_DTR) params->serial.dtr = 1;

	dp->settings = *params;
	return 0;
}

/*
 * Set serial line params
 */
static int
ifd_serial_set_params(ifd_device_t *dp, const ifd_device_params_t *params)
{
	ifd_serial_t	*dev = (ifd_serial_t *) dp;
	unsigned int	speed;
	int		control;
	struct termios	t;

	if (!memcmp(&dp->settings, params, sizeof(*params)))
		goto skip_setattr;

	if (tcgetattr(dev->fd, &t) < 0) {
		ifd_error("%s: tcgetattr: %m", dp->name);
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

	t.c_iflag = IGNBRK | INPCK;
	t.c_oflag = 0;
	t.c_cflag |= HUPCL | CREAD | CLOCAL;
	t.c_lflag = 0;

	if (tcsetattr(dev->fd, TCSANOW, &t) < 0) {
		ifd_error("%s: tcsetattr: %m", dp->name);
		return -1;
	}

	if ((speed = termios_to_speed(cfgetospeed(&t))) != 0)
		dp->etu = 1000000 / speed;


skip_setattr:
	if (ioctl(dev->fd, TIOCMGET, &control) < 0) {
		ifd_error("%s: TIOCMGET: %m", dp->name);
		return -1;
	}
	control &= ~(TIOCM_DTR | TIOCM_RTS);
	if (params->serial.rts) control |= TIOCM_RTS;
	if (params->serial.dtr) control |= TIOCM_DTR;
	if (ioctl(dev->fd, TIOCMSET, &control) < 0) {
		ifd_error("%s: TIOCMGET: %m", dp->name);
		return -1;
	}

	return 0;
}

/*
 * Flush pending input
 */
static void
ifd_serial_flush(ifd_device_t *dp)
{
	ifd_serial_t	*dev = (ifd_serial_t *) dp;

	tcflush(dev->fd, TCIFLUSH);
}

/*
 * Input/output routines
 */
static int
ifd_serial_send(ifd_device_t *dp, const void *buffer, size_t len)
{
	ifd_serial_t	*dev = (ifd_serial_t *) dp;
	size_t		total = len;
	int		n;

	while (len) {
		n = write(dev->fd, buffer, len);
		if (n < 0) {
			ifd_error("Error writing to %s: %m",
					dp->name);
			return -1;
		}
		(caddr_t) buffer += n;
		len -= n;
	}

	return total;
}

static long
since(struct timeval *then)
{
	struct timeval	now, delta;

	gettimeofday(&now, NULL);
	timersub(&now, then, &delta);
	return delta.tv_sec * 1000 + (delta.tv_usec % 1000);
}

static int
ifd_serial_recv(ifd_device_t *dp, void *buffer, size_t len, long timeout)
{
	ifd_serial_t	*dev = (ifd_serial_t *) dp;
	size_t		total = len;
	struct timeval	begin;
	int		n;

	gettimeofday(&begin, NULL);

	while (len) {
		struct pollfd pfd;
		long wait;

		if ((wait = timeout - since(&begin)) < 0)
			goto timeout;

		pfd.fd = dev->fd;
		pfd.events = POLLIN;
		n = poll(&pfd, 1, wait);
		if (n < 0) {
			ifd_error("%s: error while waiting for input: %m",
					dp->name);
			return -1;
		}
		if (n == 0)
			continue;
		n = read(dev->fd, buffer, len);
		if (n < 0) {
			ifd_error("%s: failed to read from device: %m",
					dp->name);
			return -1;
		}
		(caddr_t) buffer += n;
		len -= n;
	}

	return total;

timeout:/* Timeouts are a little special; they may happen e.g.
	 * when trying to obtain the ATR */
	if (!ifd_config.hush_errors)
		ifd_error("%s: timed out while waiting for intput",
				dp->name);
	return IFD_ERROR_TIMEOUT;
}

/*
 * Identify attached device
 */
static int
ifd_serial_identify(ifd_device_t *dev, char *namebuf, size_t len)
{
	ifd_error("Serial PNP not yet implemented");
	return -1;
}

/*
 * Close the device
 */
void
ifd_serial_close(ifd_device_t *dp)
{
	ifd_serial_t *dev = (ifd_serial_t *) dp;

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
	ifd_serial_t	*dev;
	int		fd;

	if ((fd = open(name, O_RDWR|O_NDELAY)) < 0) {
		ifd_error("Unable to open %s: %m", name);
		return NULL;
	}

	/* Clear the NDELAY flag */
	fcntl(fd, F_SETFL, 0);

	dev = (ifd_serial_t *) ifd_device_new(name, &ifd_serial_ops, sizeof(*dev));
	dev->base.timeout = 1000; /* acceptable? */
	dev->base.type = IFD_DEVICE_TYPE_SERIAL;
	dev->fd = fd;

	params.serial.speed = 9600;
	params.serial.bits = 8;
	params.serial.parity = IFD_SERIAL_PARITY_NONE;
	params.serial.stopbits = 1;
	params.serial.rts = 1;
	params.serial.dtr = 1;

	ifd_serial_set_params((ifd_device_t *) dev, &params);

	return (ifd_device_t *) dev;
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
