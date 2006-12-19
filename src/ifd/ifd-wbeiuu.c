/*
 * Driver for WB Electronics' Infinity USB Unlimited card readers
 *
 * Copyright (C) 2006, Juan Carlos Borrás <jcborras@gmail.com>
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

#define USB_TIMEOUT	1000

typedef struct wbeiuu_status_ {
	/* We need a GBP driver to talk to serial readers */
	int convention;
	int baud_rate;
	int clk_freq;
	int protocol;
	int card_state;
	unsigned char tx_buffer[1024];
} wbeiuu_status_t;

wbeiuu_status_t wbeiuu_status;

// PENDING: 

// 1.- ATRs vary depending on whether the card is inserted before or
// after the wbeiuu is recognized and ifdhandler is launched.  (not
// only spurious bytes set to zero but also different ATR reporting
// depending whether it is dondone by wbeiuu_print_bytestring or
// inside wbeiuu_card_reset()

// 2.- Another problem I see is the structure ifd_device_params_t
// defined in include/openct/device.h. you can get a pointer to in by
// calling ifd_device_get_parameters. It is a good thing to have it
// but I does not take into account that the wbeuiu can set
// independently CLK frequency and the speed of the uart. Actually
// according to that structure the wbeiuu would be a serial device
// rather than a usb device.
//
// Therefore I presume we are going to need a struct params holding
// things like all uart params (stopbits, parity, speed) and CLK
// frequency. Specially because it is a waste of time to work at
// 3.865MHz and 9600 while the OBFG makes the card tick up to a 25MHz

// 3.- The wbeiuu is not properly released after ifdhandler is
// killed. Which is anoying since it forces you to reset it somehow.

// 4.- Add LED special effects

// 5.- wbeiuu_open() fails on even calls to it

// 6.- ifd_device_close(dev); // Should I remove it and all the others?
// Maybe RED meaning dev open and off when closing it would help debugging

// 7.- Inverse Convention conversion for the ATR and all other
// transmissions

// 8.- A struct for saving the wbeiuu state and the inserted card params

static int wbeiuu_set_led(ifd_device_t * dev, uint16_t R, uint16_t G,
			  uint16_t B, uint8_t F)
{
	const int BUFSIZE = 8;
	int status;
	uint8_t buf[BUFSIZE];

	buf[0] = 0x04;		// Set LED command
	buf[1] = R & 0xFF;
	buf[2] = (R >> 8) & 0xFF;
	buf[3] = G & 0xFF;
	buf[4] = (G >> 8) & 0xFF;
	buf[5] = B & 0xFF;
	buf[6] = (B >> 8) & 0xFF;
	buf[7] = F;

	status = ifd_sysdep_usb_bulk(dev, 0x02, buf, BUFSIZE, USB_TIMEOUT);
	if (status < 0)
		ifd_debug(1,
			  "%s:%d Tranmission failure when setting the LED. status=%d",
			  __FILE__, __LINE__, status);

	return status;
}

static void wbeiuu_print_bytestring(unsigned char *ptr, int length)
{
	static const char hexconv[16] = {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
		'A', 'B', 'C', 'D', 'E', 'F'
	};

	int i;
	unsigned char msb, lsb;
	unsigned char *str;

	str = calloc(3 * length + 1, sizeof(char));	// (2*char+1space)/byte + '\0'

	for (i = 0; i < length; i++) {
		msb = ptr[i] / 16;
		lsb = ptr[i] % 16;
		str[3 * i] = hexconv[msb];
		str[3 * i + 1] = hexconv[lsb];
		str[3 * i + 2] = ' ';
	}
	str[3 * length + 1] = 0x00;

	ifd_debug(1, "%s:%d %s", __FILE__, __LINE__, str);
	free(str);
}

static int wbeiuu_open(ifd_reader_t * reader, const char *dev_name)
{
	int status;
	ifd_device_t *dev;
        ifd_device_params_t params;

	ifd_debug(1, "%s:%d wbeiuu_open()", __FILE__, __LINE__);

	reader->name = "WB Electronics Infinity USB Unlimited";
	reader->nslots = 1;	// I has physically two, but electrically only one

	dev = ifd_device_open(dev_name);

	//  if ((!dev = ifd_device_open(device_name)))
	if (!dev) {
		ifd_debug(1, "%s:%d Error when opening device %s.",
			  __FILE__, __LINE__, dev_name);
		return -1;
	}

	if (ifd_device_type(dev) != IFD_DEVICE_TYPE_USB) {
		ifd_debug(1, "%s:%d device %s is not a USB device", __FILE__,
			  __LINE__, dev_name);
		ct_error("wbeiuu: device %s is not a USB device", dev_name);
		//ifd_device_close(dev);
		return -1;
	}

        params = dev->settings;
        params.usb.interface = 0;
        if (ifd_device_set_parameters(dev, &params) < 0) {
                ct_error("wbeiuu: setting parameters failed", dev_name);
                ifd_device_close(dev);
                return -1;
        }

	reader->device = dev;
	dev->timeout = USB_TIMEOUT;

	// Why twice??
	ifd_debug(1, "%s:%d Sending CTS message", __FILE__, __LINE__);
	status =
	    ifd_usb_control(dev, 0x03, 0x02, 0x02, 0x00, NULL, 0, USB_TIMEOUT);
	//ifd_debug(1, "%s:%d status = %d", __FILE__, __LINE__, status);
	if (status < 0) {
		ifd_debug(1,
			  "%s:%d Transmission failure when sending CTS message. status = %d",
			  __FILE__, __LINE__, status);
		//ifd_device_close(dev);
		return -1;
	}
	//ifd_debug(1, "%s:%d Setting the LED", __FILE__, __LINE__);
//  status = wbeiuu_set_led(dev, 0x0000, 0x0000, 0x1000, 0x80);
//      if (status < 0) {
//              ifd_debug(1, "%s:%d Tranmission failure when setting the LED. status=%d", 
//              __FILE__, __LINE__, status);
//              //ifd_device_close(dev);
//              return -1;
//      }

	return 0;
}

static int wbeiuu_close(ifd_reader_t * reader)
{
	ifd_debug(1, "%s:%d wbeiuu_close()", __FILE__, __LINE__);
	//ifd_device_close(reader->device);
	return 0;
}

static int wbeiuu_activate(ifd_reader_t * reader)
{
	char cmd[4];
	char product_name[17];
	char firm_version[5];
	char loader_version[5];
	int status;
	//unsigned char buf[256];
	ifd_device_t *dev;

	ifd_debug(1, "%s:%d wbeiuu_activate()", __FILE__, __LINE__);

	dev = reader->device;

	status = wbeiuu_set_led(dev, 0x0000, 0x1000, 0x0000, 0x80);
	if (status < 0) {
		ifd_debug(1,
			  "%s:%d Tranmission failure when setting the LED. status=%d",
			  __FILE__, __LINE__, status);
		ifd_device_close(dev);
		return -1;
	}

	cmd[0] = 0x02;		// GET_PRODUCT_NAME
	status = ifd_sysdep_usb_bulk(dev, 0x02, &cmd, 1, USB_TIMEOUT);
	if (status < 0) {
		ifd_debug(1, "%s:%d Error. status = %d", __FILE__, __LINE__,
			  status);
		ifd_device_close(dev);
		return -1;
	}

	status = ifd_sysdep_usb_bulk(dev, 0x82, &product_name, 20, USB_TIMEOUT);
	if (status < 0) {
		ifd_debug(1, "%s:%d Error. status = %d", __FILE__, __LINE__,
			  status);
		//ifd_device_close(dev);
		return -1;
	} else {
		ifd_debug(1, "%s:%d No errors and status = %d", __FILE__,
			  __LINE__, status);
		// Which means that status returns the number of bytes read
	}

	product_name[16] = '\0';
	ifd_debug(1, "%s:%d Product Name: %s", __FILE__, __LINE__,
		  product_name);

	cmd[0] = 0x01;		// GET_FIRMWARE_VERSION
	status = ifd_sysdep_usb_bulk(dev, 0x02, &cmd, 1, USB_TIMEOUT);
	if (status < 0) {
		ifd_debug(1, "%s:%d Error. status = %d", __FILE__, __LINE__,
			  status);
		//ifd_device_close(dev);
		return -1;
	}

	status =
	    ifd_sysdep_usb_bulk(dev, 0x82, &firm_version, 100, USB_TIMEOUT);
	if (status < 0) {
		ifd_debug(1, "%s:%d Error. status = %d", __FILE__, __LINE__,
			  status);
		//ifd_device_close(dev);
		return -1;
	} else {
		ifd_debug(1, "%s:%d No errors and status = %d", __FILE__,
			  __LINE__, status);
		// Which means that status returns the number of bytes read
	}
	firm_version[4] = '\0';
	ifd_debug(1, "%s:%d Firmware version: %s", __FILE__, __LINE__,
		  firm_version);

	cmd[0] = 0x50;		// GET_LOADER_VERSION
	status = ifd_sysdep_usb_bulk(dev, 0x02, &cmd, 1, USB_TIMEOUT);
	if (status < 0) {
		ifd_debug(1, "%s:%d Error. status = %d", __FILE__, __LINE__,
			  status);
		//ifd_device_close(dev);
		return -1;
	}

	status =
	    ifd_sysdep_usb_bulk(dev, 0x82, &loader_version, 10, USB_TIMEOUT);
	if (status < 0) {
		ifd_debug(1, "%s:%d Error. status = %d", __FILE__, __LINE__,
			  status);
		//ifd_device_close(dev);
		return -1;
	} else {
		ifd_debug(1, "%s:%d No errors and status = %d", __FILE__,
			  __LINE__, status);
		// Which means that status returns the number of bytes read
	}
	loader_version[4] = '\0';
	ifd_debug(1, "%s:%d Loader version: %s", __FILE__, __LINE__,
		  loader_version);

	/*
	   // Check what happens when we read and there is nothing
	   ifd_debug(1, "%s:%d Try to read while nothing to read", __FILE__, __LINE__);
	   status = ifd_sysdep_usb_bulk(dev, 0x82, &buf, 128, USB_TIMEOUT);
	   if (status < 0) {
	   ifd_debug(1, "%s:%d Error as expected: %d", __FILE__, __LINE__, status);
	   //ifd_device_close(dev);
	   //return -1;
	   } else {
	   ifd_debug(1, "%s:%d Funny. No errors", __FILE__, __LINE__);    
	   }
	   // status = -5. That's an error
	 */

	// Enable the so-called "phoenix" mode
	cmd[0] = 0x49;		// IUU_UART_ENABLE
	cmd[1] = 0x02;		// 9600  
	cmd[2] = 0x98;		// bps
	cmd[3] = 0x21;		// one stop bit, even parity

	status = ifd_sysdep_usb_bulk(dev, 0x02, &cmd, 4, USB_TIMEOUT);
	if (status < 0) {
		ifd_debug(1, "%s:%d Error as expected: %d", __FILE__, __LINE__,
			  status);
		//ifd_device_close(dev);
		return -1;
	} else {
		ifd_debug(1, "%s:%d Device set in phoenix mode", __FILE__,
			  __LINE__);
	}

	/*

	   // Check what happens when we phoenix read and there is nothing
	   cmd[0] = 0x56;
	   ifd_debug(1, "%s:%d Phoenix read while nothing to read", __FILE__, __LINE__);

	   unsigned char tmp=0x00;
	   status = ifd_sysdep_usb_bulk(dev, 0x02, cmd, 1, USB_TIMEOUT);
	   if (status < 0) {
	   ifd_debug(1, "%s:%d Error: %d", __FILE__, __LINE__, status);
	   // -5 is communication error ($OPENCT/src/incldue/openct/error.h)
	   } else {
	   ifd_debug(1, "%s:%d No errors. %d bytes read", __FILE__, __LINE__, status);    
	   if (status==1) {
	   ifd_debug(1, "%s:%d whose value is %x", __FILE__, __LINE__, tmp);    
	   }
	   }
	 */

	/*
	   ifd_debug(1, "%s:%d Reading how many bytes in FIFO", __FILE__, __LINE__);
	   status = ifd_sysdep_usb_bulk(dev, 0x82, &buf, 1, USB_TIMEOUT);
	   if (status) {
	   ifd_debug(1, "%s:%d Error as expected: %d", __FILE__, __LINE__, status);
	   } else {
	   ifd_debug(1, "%s:%d No errors. Bytes to read: %u\n", 
	   __FILE__, __LINE__, status);    
	   }

	   unsigned int n = buf[0];
	   ifd_debug(1, "%s:%d Reading all bytes  in FIFO\n", __FILE__, __LINE__);
	   status = ifd_sysdep_usb_bulk(dev, 0x82, &buf, n, USB_TIMEOUT);
	   if (status) {
	   ifd_debug(1, "%s:%d Error as expected: %d", __FILE__, __LINE__, status);
	   } else {
	   ifd_debug(1, "%s:%d Funny. No errors", __FILE__, __LINE__);
	   }
	 */

	/*
	   Code for retrieving the ATR comes here
	   char myatr[1024];
	   size_t atr_len;
	   status = wbeiuu_card_reset(reader, 0, myatr, atr_len);
	   if (status < 0) {
	   ifd_debug(1, "%s:%d Error. status=%d", __FILE__, __LINE__, status);
	   } else {
	   ifd_debug(1, "%s:%d Funny. No errors", __FILE__, __LINE__);
	   return 0;
	   }
	 */

	status = wbeiuu_set_led(dev, 0x0000, 0x0000, 0x1000, 0xff);
	if (status < 0) {
		ifd_debug(1,
			  "%s:%d Tranmission failure when setting the LED. status=%d",
			  __FILE__, __LINE__, status);
		ifd_device_close(dev);
		return -1;
	}

	return 0;
}

static int wbeiuu_deactivate(ifd_reader_t * reader)
{
	unsigned char buf = 0x4A;	// IUU_UART_DISABLE

	ifd_debug(1, "%s:%d wbeiuu_deactivate()", __FILE__, __LINE__);

	if (ifd_sysdep_usb_bulk(reader->device, 0x02, &buf, 1, USB_TIMEOUT) < 0) {
		//ifd_device_close(reader->device);
		return -1;
	}

	return 0;
}

static int wbeiuu_change_parity(ifd_reader_t * reader, int parity)
{
	//int status;

	ifd_debug(1, "%s:%d wbeiuu_chage_parity()", __FILE__, __LINE__);

	// PENDING:
	// 1.- USB devices do not have "parity", but the wbeiuu is
	//     a sort of serial device
	// 2.- The annoying thing is that the wbeiuu parity can only be
	//     set by setting all the other uart params at once
	// 3.- Which means we have to keep the wbeiuu state in a bunch of
	//     static vars (phoenix_mode=on|off, baudrate, stopbits and so on...
	// parity values defined in $OPENCT/src/include/device.h

	switch (parity) {
	case IFD_SERIAL_PARITY_NONE:
		ifd_debug(1, "%s:%d Would set parity to none if implemented",
			  __FILE__, __LINE__);
		break;
	case IFD_SERIAL_PARITY_ODD:
		ifd_debug(1, "%s:%d Would set odd parity if implemented",
			  __FILE__, __LINE__, parity);
		break;
	case IFD_SERIAL_PARITY_EVEN:
		ifd_debug(1, "%s:%d Would set even parity if implemented",
			  __FILE__, __LINE__, parity);
		break;
	default:
		ifd_debug(1, "%s:%d parity parameter cannot be %d",
			  __FILE__, __LINE__, parity);
		return -1;
	}

//  buf[0] = IUU_UART_ESC;
//  buf[1] = IUU_UART_CHANGE;
//  buf[2] = (uint8_t) ((br >> 8) & 0x00FF);   /* high byte */
//  buf[3] = (uint8_t) (0x00FF & br);   /* low byte */
//  buf[4] = (uint8_t) (parity & sbits);        /* both parity and stop now */
//  
//  status = iuu_write(inf, buf, 5);
//   if (status != IUU_OPERATION_OK)
//     iuu_process_error(status, __FILE__, __LINE__);

	return 0;
}

static int wbeiuu_change_speed(ifd_reader_t * reader, unsigned int speed)
{
	// PENDING:
	// 1.- Speed in bits per second is one thing, but being able to
	//     set CLK frequency is another different matter than cannot
	//     go wasted
	ifd_debug(1, "%s:%d wbeiuu_chage_speed()", __FILE__, __LINE__);
	return 0;
}

static int wbeiuu_card_reset(ifd_reader_t * reader, int slot, void *atr,
			     size_t atr_len)
{
	int status;
	unsigned char buf[256];
	unsigned char len;
	ifd_device_t *dev;

	ifd_debug(1, "%s:%d wbeiuu_card_reset()", __FILE__, __LINE__);

	dev = reader->device;

//  status = wbeiuu_set_led(dev, 0x1000, 0x0000, 0x0000, 0x80);
//      if (status < 0) {
//              ifd_debug(1, "%s:%d Tranmission failure when setting the LED. status=%d", 
//              __FILE__, __LINE__, status);
//              //ifd_device_close(dev);
//              return -1;
//      }

	// Flushing (do we have to flush the uart too?)
	status = ifd_sysdep_usb_bulk(dev, 0x82, &buf, 256, USB_TIMEOUT);
	if (status < 0) {
		//ifd_device_close(dev);
		ifd_debug(1, "%s:%d Error. If status=-5 then OK. status=%d.",
			  __FILE__, __LINE__, status);
		//return -1;
	}
	// Resetting card
	buf[0] = 0x52;		// IUU_RST_SET
	buf[1] = 0x06;		// IUU_DELAY_MS
	buf[2] = 0x0c;		// milliseconds
	buf[3] = 0x53;		// IUU_RST_CLEAR

	ifd_debug(1, "%s:%d Resetting the card()", __FILE__, __LINE__);
	status = ifd_sysdep_usb_bulk(dev, 0x02, &buf, 4, USB_TIMEOUT);
	if (status < 0) {
		//ifd_device_close(dev);
		ifd_debug(1, "%s:%d Bailing out. status=%d", __FILE__, __LINE__,
			  status);
		return -1;
	}
	// waiting for the IUU uart to be filled by the card
	usleep(500000);
	// The wait time depend on the CLK signal frequency

	ifd_debug(1, "%s:%d Retrieving the ATR", __FILE__, __LINE__);
	buf[0] = 0x56;		// wbeiuu uart rx
	status = ifd_sysdep_usb_bulk(dev, 0x02, buf, 1, USB_TIMEOUT);
	if (status < 0) {
		//ifd_device_close(dev);
		ifd_debug(1, "%s:%d Bailing out. status=%d", __FILE__, __LINE__,
			  status);
		return -1;
	}
	ifd_debug(1, "%s:%d How many bytes waiting at the FIFO?", __FILE__,
		  __LINE__, status);
	status = ifd_sysdep_usb_bulk(dev, 0x82, &len, 1, USB_TIMEOUT);
	if (status < 0) {
		//ifd_device_close(dev);
		ifd_debug(1, "%s:%d Error. status=%d.", __FILE__, __LINE__,
			  status);
		//return -1;
	} else {
		ifd_debug(1, "%s:%d %d bytes waiting at the fifo", __FILE__,
			  __LINE__, len);
	}

	ifd_debug(1, "%s:%d Retrieving now the ATR", __FILE__, __LINE__,
		  status);
	status = ifd_sysdep_usb_bulk(dev, 0x82, buf, len, 10 * USB_TIMEOUT);
	if (status < 0) {
		//ifd_device_close(dev);
		ifd_debug(1, "%s:%d Error. status=%d.", __FILE__, __LINE__,
			  status);
		//return -1;
	} else if (status != len) {
		ifd_debug(1,
			  "%s:%d Error. Expecting %d bytes, but read only %d",
			  __FILE__, __LINE__, len, status);
	}
	wbeiuu_print_bytestring(buf, len);

//  int i;
//  char *tmp = atr;
//  for (i=0; i<len; i++) {
//    //tmp = (char) atr[i];
//    ifd_debug(1, "%s:%d buf[%d] = %x ; ATR[%d] = %x", 
//              __FILE__, __LINE__, i, 0x00FF & buf[i], i, 0x00FF & tmp[i]); 
//  }

	// Now we just have to copy it back
	if (len <= atr_len) {
		ifd_debug(1, "%s:%d %d is smaller than %d so it should fit",
			  __FILE__, __LINE__, len, atr_len);
		memcpy(atr, buf, len);
	} else {
		ifd_debug(1,
			  "%s:%d Retrieved ATR is %d bytes but the buffer is only %d",
			  __FILE__, __LINE__, len, atr_len);
		return -1;
	}

//  tmp = atr;
//  for (i=0; i<len; i++) {
//    //tmp = (char) atr[i];
//    ifd_debug(1, "%s:%d buf[%d] = %x ; ATR[%d] = %x", 
//              __FILE__, __LINE__, i, 0x00FF & buf[i], i, 0x00FF & tmp[i]); 
//  }

	return len;
}

static int wbeiuu_card_status(ifd_reader_t * reader, int slot, int *status)
{
	// PENDING:
	// 1.- Make sure that both slots are not occupied
	// 2.- Remove all card params after extraction (i.e. Inverse Convention)

	int usb_status;
	char st = 0x03;		// IUU_GET_STATE_REGISTER
	ifd_device_t *dev;

	//ifd_debug(1, "%s:%d wbeiuu_card_status()", __FILE__, __LINE__);

	dev = reader->device;
	usb_status = ifd_sysdep_usb_bulk(dev, 0x02, &st, 1, USB_TIMEOUT);
	if (usb_status < 0) {
		//ifd_device_close(dev);
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		return -1;
	}

	usb_status = ifd_sysdep_usb_bulk(dev, 0x82, &st, 1, USB_TIMEOUT);
	if (status < 0) {
		//ifd_device_close(dev);
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		return -1;
	}
	//ifd_debug(4, "%s:%d Status register: %x", __FILE__, __LINE__, st);

	if (st == 0x05) {	// both slots occupied
		*status = IFD_CARD_STATUS_CHANGED;
		return IFD_ERROR_NO_CARD;
	}

	if (st == 0x01 || st == 0x04)
		*status = IFD_CARD_PRESENT;

	return 0;
}

static int wbeiuu_send(ifd_reader_t * reader, unsigned int dad,
		       const unsigned char *buffer, size_t len)
{
	int status;
	unsigned char buf[3];
	ifd_device_t *dev;

	ifd_debug(1, "%s:%d wbeiuu_send()", __FILE__, __LINE__);

	dev = reader->device;

	status = wbeiuu_set_led(dev, 0x0000, 0x0000, 0x1000, 0x80);
	if (status < 0) {
		ifd_debug(1,
			  "%s:%d Tranmission failure when setting the LED. status=%d",
			  __FILE__, __LINE__, status);
		//ifd_device_close(dev);
		return -1;
	}
	// The wbeiuu uart fifo is just 255 bytes
	// anything bigger than that must be sent in smaller chunks
	// which is turn must be syncronized with the card swalloing
	// capabilites
	if (len > 255) {
		//ifd_device_close(dev);
		ifd_debug(1, "%s:%d Bailing out: len>255 = %d", __FILE__,
			  __LINE__, len);
		return -1;
	}

	buf[0] = 0x5e;		// IUU_UART_ESC;
	buf[1] = 0x04;		// IUU_UART_TX;
	buf[2] = len;

	status = ifd_sysdep_usb_bulk(dev, 0x02, &buf, len, USB_TIMEOUT);
	if (status < 0) {
		//ifd_device_close(dev);
		ifd_debug(1, "%s:%d Bailing out.", __FILE__, __LINE__);
		return -1;
	}

	status = ifd_sysdep_usb_bulk(dev, 0x02, &buffer, len, USB_TIMEOUT);
	if (status < 0) {
		//ifd_device_close(dev);
		ifd_debug(1, "%s:%d Bailing out.", __FILE__, __LINE__);
		return -1;
	}

	ifd_debug(1, "%s:%d Returning status = %d.", __FILE__, __LINE__,
		  status);

	return status;
}

static int wbeiuu_recv(ifd_reader_t * reader, unsigned int dad,
		       unsigned char *buffer, size_t len, long timeout)
{
	// PENDING:
	// 1.- Chunking if len>255
	// 2.- Ensuring the wbeiuu is in phoenix mode
	int status;
	ifd_device_t *dev;
	unsigned char cmd = 0x55;	// wbeiuu uart rx
	unsigned char nbytes = 0x00;
	unsigned char buf[255];

	ifd_debug(1, "%s:%d wbeiuu_recv()", __FILE__, __LINE__);

	dev = reader->device;

	ifd_debug(1, "%s:%d wbeiuu_recv()", __FILE__, __LINE__);
	status = wbeiuu_set_led(dev, 0x0000, 0x0000, 0x1000, 0x80);
	if (status < 0) {
		ifd_debug(1,
			  "%s:%d Tranmission failure when setting the LED. status=%d",
			  __FILE__, __LINE__, status);
		//ifd_device_close(dev);
		return -1;
	}
	// Checking that the buffer fits in the wbeiuu uart
	if (len > 255) {
		//ifd_device_close(dev);
		ifd_debug(1, "%s:%d Bailing out: len>255 = %d", __FILE__,
			  __LINE__, len);
		return -1;
	}
	// Sending the command
	status = ifd_sysdep_usb_bulk(dev, 0x02, &cmd, 1, timeout);
	if (status < 0) {
		//ifd_device_close(dev);
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		return -1;
	}
	// Reading how many bytes are ready to be retrieved
	status = ifd_sysdep_usb_bulk(dev, 0x82, &nbytes, 1, timeout);
	if (status < 0) {
		//ifd_device_close(dev);
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		return -1;
	}

	ifd_debug(1, "%s:%d Reading %d bytes from the UART", __FILE__, __LINE__,
		  nbytes);
	status = ifd_sysdep_usb_bulk(dev, 0x82, &buf, nbytes, timeout);
	if (status < 0) {
		//ifd_device_close(dev);
		ifd_debug(1, "%s:%d Bailing out", __FILE__, __LINE__);
		return -1;
	}

	memcpy(buffer, buf, nbytes);

	return nbytes;
}

static int wbeiuu_card_request(ifd_reader_t * reader, int slot,
			       time_t timeout, const char *message,
			       void *atr, size_t atr_len)
{
	ifd_debug(1, "%s:%d wbeiuu_card_request", __FILE__, __LINE__);
	return 0;
}

static int wbeiuu_card_eject(ifd_reader_t * reader, int slot,
			     time_t timeout, const char *message)
{
	ifd_debug(1, "%s:%d wbeiuu_card_eject", __FILE__, __LINE__);
	return 0;
}

static int wbeiuu_output(ifd_reader_t * reader, const char *message)
{
	ifd_debug(1, "%s:%d wbeiuu_output", __FILE__, __LINE__);
	return 0;
}

static int wbeiuu_perform_verify(ifd_reader_t * reader,
				 int slot, unsigned int timeout,
				 const char *prompt, const unsigned char *data,
				 size_t data_len, unsigned char *resp,
				 size_t resp_len)
{
	ifd_debug(1, "%s:%d wbeiuu_perform_verify", __FILE__, __LINE__);
	return 0;
}

#if 0
/* not used so far */
static int wbeiuu_set_protocol(ifd_reader_t * reader, int slot, int protocol)
{
	ifd_debug(1, "%s:%d wbeiuu_set_protocol", __FILE__, __LINE__);

	switch (protocol) {
	case IFD_PROTOCOL_T0:
		return IFD_SUCCESS;
		break;
	case IFD_PROTOCOL_T1:
	default:
		return IFD_ERROR_NOT_SUPPORTED;
	}

	return 0;
}
#endif

static int wbeiuu_transparent(ifd_reader_t * reader, int slot,
			      const void *sbuf, size_t slen,
			      void *rbuf, size_t rlen)
{
	ifd_debug(1, "%s:%d wbeiuu_transparent", __FILE__, __LINE__);
	//ifd_driver_t *dev = reader->driver_data;

//  switch (dev->icc_proto) {
//  case IFD_PROTOCOL_T0:
//    //return cm_transceive_t0(reader, sbuf, slen, rbuf, rlen);
//  case IFD_PROTOCOL_T1:
//    return IFD_ERROR_NOT_SUPPORTED; /* not yet */
//  default:
//    return IFD_ERROR_NOT_SUPPORTED;
//  }

	return 0;
}

static int wbeiuu_sync_read(ifd_reader_t * reader, int slot, int proto,
			    unsigned short addr,
			    unsigned char *rbuf, size_t rlen)
{
	ifd_debug(1, "%s:%d wbeiuu_sync_read", __FILE__, __LINE__);
	return 0;
}

static int wbeiuu_sync_write(ifd_reader_t * reader, int slot, int proto,
			     unsigned short addr,
			     const unsigned char *sbuf, size_t slen)
{
	ifd_debug(1, "%s:%d wbeiuu_sync_write", __FILE__, __LINE__);
	return 0;
}

/*
 * Driver operations
 */
static struct ifd_driver_ops wbeiuu_driver;

void ifd_wbeiuu_register(void)
{
	wbeiuu_driver.open = wbeiuu_open;
	wbeiuu_driver.close = wbeiuu_close;

	wbeiuu_driver.change_parity = wbeiuu_change_parity;
	wbeiuu_driver.change_speed = wbeiuu_change_speed;

	wbeiuu_driver.activate = wbeiuu_activate;
	wbeiuu_driver.deactivate = wbeiuu_deactivate;

	wbeiuu_driver.card_status = wbeiuu_card_status;
	wbeiuu_driver.card_reset = wbeiuu_card_reset;
	wbeiuu_driver.card_request = wbeiuu_card_request;
	wbeiuu_driver.card_eject = wbeiuu_card_eject;

	wbeiuu_driver.output = wbeiuu_output;
	wbeiuu_driver.perform_verify = wbeiuu_perform_verify;

	wbeiuu_driver.send = wbeiuu_send;
	wbeiuu_driver.recv = wbeiuu_recv;

	//wbeiuu_driver.set_protocol = wbeiuu_set_protocol;
	wbeiuu_driver.transparent = wbeiuu_transparent;
	wbeiuu_driver.sync_read = wbeiuu_sync_read;
	wbeiuu_driver.sync_write = wbeiuu_sync_write;

	ifd_driver_register("wbeiuu", &wbeiuu_driver);
}
