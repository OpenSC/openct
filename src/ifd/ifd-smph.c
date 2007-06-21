/*
 * Driver for SmartMouse/Phoenix readers
 *
 * Thanks to Alexandre Becoulet and its SCTK project ;)
 * In agreement with him, this project's license has been changed to LGPL.
 * URL: http://freshmeat.net/projects/sctk/
 *
 * 2005, Antoine Nguyen <ngu.antoine@gmail.com>
 */

#include "internal.h"
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/poll.h>

#define PHS_CONV_DIRECT	0
#define PHS_CONV_INDIRECT 1
#define TIMEOUT	1000

/* table for indirect to direct byte mode conversion */
static const uint8_t dir_conv_table[0x100] =
{
  0xff, 0x7f, 0xbf, 0x3f, 0xdf, 0x5f, 0x9f, 0x1f,
  0xef, 0x6f, 0xaf, 0x2f, 0xcf, 0x4f, 0x8f, 0xf,
  0xf7, 0x77, 0xb7, 0x37, 0xd7, 0x57, 0x97, 0x17,
  0xe7, 0x67, 0xa7, 0x27, 0xc7, 0x47, 0x87, 0x7,
  0xfb, 0x7b, 0xbb, 0x3b, 0xdb, 0x5b, 0x9b, 0x1b,
  0xeb, 0x6b, 0xab, 0x2b, 0xcb, 0x4b, 0x8b, 0xb,
  0xf3, 0x73, 0xb3, 0x33, 0xd3, 0x53, 0x93, 0x13,
  0xe3, 0x63, 0xa3, 0x23, 0xc3, 0x43, 0x83, 0x3,
  0xfd, 0x7d, 0xbd, 0x3d, 0xdd, 0x5d, 0x9d, 0x1d,
  0xed, 0x6d, 0xad, 0x2d, 0xcd, 0x4d, 0x8d, 0xd,
  0xf5, 0x75, 0xb5, 0x35, 0xd5, 0x55, 0x95, 0x15,
  0xe5, 0x65, 0xa5, 0x25, 0xc5, 0x45, 0x85, 0x5,
  0xf9, 0x79, 0xb9, 0x39, 0xd9, 0x59, 0x99, 0x19,
  0xe9, 0x69, 0xa9, 0x29, 0xc9, 0x49, 0x89, 0x9,
  0xf1, 0x71, 0xb1, 0x31, 0xd1, 0x51, 0x91, 0x11,
  0xe1, 0x61, 0xa1, 0x21, 0xc1, 0x41, 0x81, 0x1,
  0xfe, 0x7e, 0xbe, 0x3e, 0xde, 0x5e, 0x9e, 0x1e,
  0xee, 0x6e, 0xae, 0x2e, 0xce, 0x4e, 0x8e, 0xe,
  0xf6, 0x76, 0xb6, 0x36, 0xd6, 0x56, 0x96, 0x16,
  0xe6, 0x66, 0xa6, 0x26, 0xc6, 0x46, 0x86, 0x6,
  0xfa, 0x7a, 0xba, 0x3a, 0xda, 0x5a, 0x9a, 0x1a,
  0xea, 0x6a, 0xaa, 0x2a, 0xca, 0x4a, 0x8a, 0xa,
  0xf2, 0x72, 0xb2, 0x32, 0xd2, 0x52, 0x92, 0x12,
  0xe2, 0x62, 0xa2, 0x22, 0xc2, 0x42, 0x82, 0x2,
  0xfc, 0x7c, 0xbc, 0x3c, 0xdc, 0x5c, 0x9c, 0x1c,
  0xec, 0x6c, 0xac, 0x2c, 0xcc, 0x4c, 0x8c, 0xc,
  0xf4, 0x74, 0xb4, 0x34, 0xd4, 0x54, 0x94, 0x14,
  0xe4, 0x64, 0xa4, 0x24, 0xc4, 0x44, 0x84, 0x4,
  0xf8, 0x78, 0xb8, 0x38, 0xd8, 0x58, 0x98, 0x18,
  0xe8, 0x68, 0xa8, 0x28, 0xc8, 0x48, 0x88, 0x8,
  0xf0, 0x70, 0xb0, 0x30, 0xd0, 0x50, 0x90, 0x10,
  0xe0, 0x60, 0xa0, 0x20, 0xc0, 0x40, 0x80, 0x0
};

enum prot_e
{
  prot_phoenix,		/* phoenix smartcard interface */
  prot_smartmouse,	/* smartmouse smartcard interface */
};

typedef struct smph_priv
{
  enum prot_e prot;
  unsigned int mode;
} smph_priv_t;

static int smph_card_reset(ifd_reader_t *, int, void *, size_t);
static int smph_recv(ifd_reader_t *, unsigned int, unsigned char *, size_t, long);

/*
 * Common functions
 */
static int smph_setctrl(ifd_device_t *dev, const int  ctrl)
{
  int tmp;
  
  if (ioctl(dev->fd, TIOCMGET, &tmp) == -1)
    return -1;
  tmp &= ~(TIOCM_RTS | TIOCM_CTS | TIOCM_DTR);
  tmp |= ctrl;
  return (ioctl(dev->fd, TIOCMSET, &tmp));
}

/*
 * Initialize the reader
 */
static int _smph_open(ifd_reader_t *reader, const char *device_name,
		      smph_priv_t *privd)
{
  ifd_device_params_t params;
  ifd_device_t *dev;

  reader->nslots = 1;
  if (!(dev = ifd_device_open(device_name)))
    return -1;
  reader->device = dev;

  if (dev->type == IFD_DEVICE_TYPE_SERIAL)
    {
      if (ifd_device_get_parameters(dev, &params) < 0)
        return -1;

      params.serial.speed = 9600;
      params.serial.bits = 8;
      params.serial.stopbits = 1;
      params.serial.parity = IFD_SERIAL_PARITY_NONE;
      params.serial.dtr = 1;
      params.serial.rts = 1;

      if (ifd_device_set_parameters(dev, &params) < 0)
        return -1;
    }
  dev->user_data = (void *)privd;
  dev->timeout = TIMEOUT;
  return 0;
}

static int phx_open(ifd_reader_t * reader, const char *device_name)
{
  smph_priv_t *privd = NULL;

  ifd_debug(1, "device=%s", device_name);
  reader->name = "Phoenix reader";
  if (!(privd = (smph_priv_t *)malloc(sizeof (smph_priv_t))))
    {
      ct_error("out of memory");
      return IFD_ERROR_NO_MEMORY;
    }
  privd->mode = PHS_CONV_DIRECT;
  privd->prot = prot_phoenix;
  return _smph_open(reader, device_name, privd);
}

static int smtm_open(ifd_reader_t * reader, const char *device_name)
{
  smph_priv_t *privd = NULL;

  ifd_debug(1, "device=%s", device_name);
  reader->name = "SmartMouse reader";
  if (!(privd = (smph_priv_t *)malloc(sizeof (smph_priv_t))))
    {
      ct_error("out of memory");
      return IFD_ERROR_NO_MEMORY;
    }
  privd->mode = PHS_CONV_DIRECT;
  privd->prot = prot_smartmouse;
  return _smph_open(reader, device_name, privd);
}

/*
 * Change the parity
 */
static int smph_change_parity(ifd_reader_t *reader, int parity)
{
  ifd_device_t *dev = reader->device;
  ifd_device_params_t params;
  
  if (dev->type != IFD_DEVICE_TYPE_SERIAL)
    return IFD_ERROR_NOT_SUPPORTED;
  
  if (ifd_device_get_parameters(dev, &params) < 0)
    return -1;
    
  params.serial.parity = parity;
  return ifd_device_set_parameters(dev, &params);
}

/*
 * Activate the reader
 */
static int smph_activate(ifd_reader_t * reader)
{
  ifd_device_t *dev = reader->device;
  smph_priv_t *privd = (smph_priv_t *)dev->user_data;
  int tmp;
  uint8_t mode;

  if (smph_card_reset(reader, 0, &mode, 1) < 0)
    return -1;

  ifd_debug(1, "Mode received: 0x%x\n", mode);
  switch (mode)
    {
    case 0x03:
      privd->mode = PHS_CONV_INDIRECT;
      tmp = IFD_SERIAL_PARITY_ODD;
      break;
    case 0x3B:
      privd->mode = PHS_CONV_DIRECT;
      tmp = IFD_SERIAL_PARITY_EVEN;
      break;
    default:
      return -1;
    }
  smph_change_parity(reader, tmp);

  return 0;
}

static int smph_deactivate(ifd_reader_t * reader)
{
  ifd_device_t *dev = reader->device;

  tcflush(dev->fd, TCIOFLUSH);
  if (smph_setctrl(dev, TIOCM_CTS))
    return -1;
  return 0;
}

/*
 * Check card status
 */
static int smph_card_status(ifd_reader_t * reader, int slot, int *status)
{
  ifd_device_t *dev = reader->device;
  int tmp;

  if (slot)
    {
      ct_error("smph: bad slot index %u", slot);
      return IFD_ERROR_INVALID_SLOT;
    }
  
  tcflush(dev->fd, TCIOFLUSH);
  if (ioctl(dev->fd, TIOCMGET, &tmp) < 0)
    return -1;

  *status = 0;
  *status |= ((tmp & TIOCM_CTS) != TIOCM_CTS) ? IFD_CARD_PRESENT : 0;
  return 0;
}

/*
 * Reset the card and get the ATR
 */
static int smph_card_reset(ifd_reader_t *reader, int slot, void *atr,
			   size_t size)
{
  ifd_device_t *dev = reader->device;
  smph_priv_t *privd = dev->user_data;
  int res;

  if (slot)
    {
      ct_error("%s: bad slot index %u", 
	       (privd->prot == prot_phoenix) ? "phoenix" : "smartmouse", slot);
      return IFD_ERROR_INVALID_SLOT;
    }

  tcflush(dev->fd, TCIOFLUSH);
  if (smph_setctrl(dev, (privd->prot == prot_phoenix) 
		   ? TIOCM_RTS | TIOCM_CTS | TIOCM_DTR 
		   : TIOCM_CTS | TIOCM_DTR) < 0)
    return -1;

  /* FIXME: use ifd_serial_reset instead? */
  sleep(1);

  if (smph_setctrl(dev, (privd->prot == prot_phoenix) 
		   ? TIOCM_CTS | TIOCM_DTR :
		   TIOCM_RTS | TIOCM_CTS | TIOCM_DTR) < 0)
    return -1;

  /* FIXME: use ifd_serial_reset instead? */
  usleep(200);

  if ((res = smph_recv(reader, 0, (unsigned char *)atr, size, dev->timeout)) < 1)
    return -1;

  ifd_debug(1, "Bytes received %i\n", res);
  return res;
}

/*
 * Send command to IFD
 */
static int _smph_send(ifd_device_t *dev, const unsigned char *buffer, 
		      size_t len)
{
  unsigned char tmp;
  unsigned int i;
  struct pollfd pfd;
  
  ifd_debug(3, "data:%s", ct_hexdump(buffer, len));
  for (i = 0; i < len; i++)
    {
      if (write(dev->fd, buffer + i, 1) < 1)
	return -1;
      tcdrain(dev->fd);
    }

  for (i = 0; i < len; i++)
    {
      pfd.fd = dev->fd;
      pfd.events = POLLIN;
      if (poll(&pfd, 1, dev->timeout) < 1)
	return -1;
      if (read(dev->fd, &tmp, 1) < 1)
	return -1;
      if (tmp != *(buffer + i))
	return -1;
    }
  return 0;
}

static int smph_send(ifd_reader_t * reader, unsigned int dad,
		     const unsigned char *buffer, size_t len)
{
  smph_priv_t *privd;
  ifd_device_t *dev = reader->device;
  uint8_t *fbuff = NULL;
  int i;
  
  if (!dev)
    return -1;
  privd = (smph_priv_t *)dev->user_data;
  if (privd->mode == PHS_CONV_INDIRECT)
    {
      if (!(fbuff = (uint8_t *)malloc(len * sizeof (uint8_t))))
	{
	  ct_error("out of memory");
	  return IFD_ERROR_NO_MEMORY;
	}
      for (i = 0; i < len; i++)
	fbuff[i] = dir_conv_table[buffer[i]];
      i = _smph_send(dev, fbuff, len);
      free(fbuff);
      return i;
    }
  return _smph_send(dev, buffer, len);
}

/*
 * Receive data from IFD
 */
static int smph_recv(ifd_reader_t * reader, unsigned int dad,
		     unsigned char *buffer, size_t len, long timeout)
{
  ifd_device_t *dev = reader->device;
  smph_priv_t *privd;
  int n;
  int i;

  for (i = 0; i < len; i++)
    {
      n = ifd_device_recv(dev, buffer + i, 1, timeout);
      if (n == IFD_ERROR_TIMEOUT)
	break;
      if (n == -1)
	return -1;
    }

  privd = (smph_priv_t *)dev->user_data;
  if (privd->mode == PHS_CONV_INDIRECT)
    for (i = 0; i < len; i++)
      buffer[i] = dir_conv_table[buffer[i]];
  
  ifd_debug(3, "data:%s", ct_hexdump(buffer, len));
  return i;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops phx_driver;
static struct ifd_driver_ops smtm_driver;

void ifd_smph_register(void)
{
  phx_driver.open = phx_open;
  //smph_driver.change_parity = smph_change_parity;
  phx_driver.activate = smph_activate;
  phx_driver.deactivate = smph_deactivate;
  phx_driver.card_status = smph_card_status;
  phx_driver.card_reset = smph_card_reset;
  phx_driver.send = smph_send;
  phx_driver.recv = smph_recv;
  ifd_driver_register("phoenix", &phx_driver);

  smtm_driver.open = smtm_open;
  //smph_driver.change_parity = smph_change_parity;
  smtm_driver.activate = smph_activate;
  smtm_driver.deactivate = smph_deactivate;
  smtm_driver.card_status = smph_card_status;
  smtm_driver.card_reset = smph_card_reset;
  smtm_driver.send = smph_send;
  smtm_driver.recv = smph_recv;
  ifd_driver_register("smartmouse", &smtm_driver);
}
