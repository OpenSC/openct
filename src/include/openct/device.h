/*
 * Device functions of the IFD handler library
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef IFD_DEVICE_H
#define IFD_DEVICE_H

#include <openct/core.h>

/* Types of devices supported by libifd */
enum {
	IFD_DEVICE_TYPE_SERIAL = 0,
	IFD_DEVICE_TYPE_USB,
	IFD_DEVICE_TYPE_PS2,
	IFD_DEVICE_TYPE_OTHER,
};

union ifd_device_params {
	struct {
		unsigned int	speed;
		int		bits;
		int		stopbits;
		int		parity;
		unsigned int	rts : 1,
				dtr : 1;
	} serial;
};

enum {
	IFD_SERIAL_PARITY_NONE = 0,
	IFD_SERIAL_PARITY_ODD  = 1,
	IFD_SERIAL_PARITY_EVEN = 2,
};
#define IFD_SERIAL_PARITY_TOGGLE(n)	((n)? ((n) ^ 3) : 0)

/*
 * Control messages to be sent through
 * ifd_device_control must always have a guard word
 * that contains the device type.
 */
typedef struct ifd_usb_cmsg {
	int		guard;	/* device type */

	int		requesttype;
	int		request;
	int		value;
	int		index;
	void *		data;
	size_t		len;
} ifd_usb_cmsg_t;

extern ifd_device_t *	ifd_device_open(const char *);
extern ifd_device_t *	ifd_device_open_channel(unsigned int num);
extern int		ifd_device_type(ifd_device_t *);
extern void		ifd_device_flush(ifd_device_t *);
extern int		ifd_device_identify(const char *, char *, size_t);
extern int		ifd_device_get_parameters(ifd_device_t *,
				ifd_device_params_t *);
extern int		ifd_device_set_parameters(ifd_device_t *,
				const ifd_device_params_t *);
extern int		ifd_device_transceive(ifd_device_t *,
				ifd_apdu_t *, long);
extern int		ifd_device_send(ifd_device_t *, const void *, size_t);
extern int		ifd_device_recv(ifd_device_t *, void *, size_t, long);
extern int		ifd_device_control(ifd_device_t *, void *, size_t);
extern void		ifd_device_close(ifd_device_t *);

/* Internal system dependent device functions */
extern int		ifd_sysdep_device_type(const char *);
extern const char *	ifd_sysdep_channel_to_name(unsigned int num);
extern int		ifd_sysdep_usb_control(int, ifd_usb_cmsg_t *, long);
extern int		ifd_sysdep_usb_scan(void);

/* scheduled to go away */
extern ifd_device_t *	ifd_open_serial(const char *);
extern ifd_device_t *	ifd_open_psaux(const char *);
extern ifd_device_t *	ifd_open_usb(const char *);

#endif /* IFD_DEVICE_H */
