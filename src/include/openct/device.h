/*
 * Device functions of the IFD handler library
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_DEVICE_H
#define OPENCT_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <openct/ifd.h>

struct pollfd; /* for poll_presence */

/* Types of devices supported by libifd */
enum {
	IFD_DEVICE_TYPE_SERIAL = 0,
	IFD_DEVICE_TYPE_USB,
	IFD_DEVICE_TYPE_PS2,
	IFD_DEVICE_TYPE_PCMCIA,
	IFD_DEVICE_TYPE_PCMCIA_BLOCK,
	IFD_DEVICE_TYPE_OTHER
};

union ifd_device_params {
	struct {
		unsigned int	speed;
		int		bits;
		int		stopbits;
		int		parity;
		int		check_parity;
		unsigned int	rts : 1,
				dtr : 1;
	} serial;
	struct {
		int configuration;
		int interface;
		int altsetting;
		int ep_o;
		int ep_i;
		int ep_intr;
	} usb;
};

enum {
	IFD_SERIAL_PARITY_NONE = 0,
	IFD_SERIAL_PARITY_ODD  = 1,
	IFD_SERIAL_PARITY_EVEN = 2
};
#define IFD_SERIAL_PARITY_TOGGLE(n)	((n)? ((n) ^ 3) : 0)

#define IFD_MAX_DEVID_PARTS	5
typedef struct ifd_devid {
	int		type;
	unsigned int	num;
	unsigned int	val[IFD_MAX_DEVID_PARTS];
} ifd_devid_t;

/*
 * Control messages to be sent through
 * ifd_device_control must always have a guard word
 * that contains the device type.
 */
enum {
	IFD_USB_URB_TYPE_ISO = 0,
	IFD_USB_URB_TYPE_INTERRUPT = 1,
	IFD_USB_URB_TYPE_CONTROL = 2,
	IFD_USB_URB_TYPE_BULK = 3
};
typedef struct ifd_usb_capture ifd_usb_capture_t;

extern ifd_device_t *	ifd_device_open(const char *);
extern void		ifd_device_close(ifd_device_t *);
extern int		ifd_device_type(ifd_device_t *);
extern int		ifd_device_reset(ifd_device_t *);
extern void		ifd_device_flush(ifd_device_t *);
extern void		ifd_device_send_break(ifd_device_t *, unsigned int);
extern int		ifd_device_identify(const char *, char *, size_t);
extern int		ifd_device_get_parameters(ifd_device_t *,
				ifd_device_params_t *);
extern int		ifd_device_set_parameters(ifd_device_t *,
				const ifd_device_params_t *);
extern int		ifd_device_transceive(ifd_device_t *,
				const void *, size_t,
				void *, size_t, long);
extern int		ifd_device_send(ifd_device_t *, const unsigned char *, size_t);
extern int		ifd_device_recv(ifd_device_t *, unsigned char *, size_t, long);
extern int		ifd_device_control(ifd_device_t *, void *, size_t);
extern void		ifd_device_set_hotplug(ifd_device_t *, int);
extern int		ifd_device_get_eventfd(ifd_device_t *, short *events);
extern int		ifd_device_poll_presence(ifd_device_t *,
				struct pollfd *);

extern int		ifd_device_id_parse(const char *, ifd_devid_t *);
extern int		ifd_device_id_match(const ifd_devid_t *,
				const ifd_devid_t *);

extern int		ifd_usb_control(ifd_device_t *,
				unsigned int requesttype,
				unsigned int request,
				unsigned int value,
				unsigned int index,
				void *data, size_t len,
				long timeout);
extern int		ifd_usb_begin_capture(ifd_device_t *,
				int type, int endpoint,
				size_t maxpacket,
				ifd_usb_capture_t **);
extern int		ifd_usb_capture_event(ifd_device_t *,
				ifd_usb_capture_t *,
				void *buffer, size_t len);
extern int		ifd_usb_capture(ifd_device_t *,
				ifd_usb_capture_t *,
				void *buffer, size_t len,
				long timeout);
extern int		ifd_usb_end_capture(ifd_device_t *,
				ifd_usb_capture_t *);

extern void		ifd_serial_send_break(ifd_device_t *, unsigned int usec);
extern int		ifd_serial_get_cts(ifd_device_t *);
extern int		ifd_serial_get_dsr(ifd_device_t *);
extern int		ifd_serial_get_dtr(ifd_device_t *);

#ifdef __cplusplus
}
#endif

#endif /* OPENCT_DEVICE_H */
