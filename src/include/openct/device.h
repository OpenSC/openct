/*
 * Device functions of the IFD handler library
 *
 */

#ifndef IFD_DEVICE_H
#define IFD_DEVICE_H

#include <ifd/core.h>

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

enum {
	IFD_DEVICE_TYPE_SERIAL,
	IFD_DEVICE_TYPE_USB,
	IFD_DEVICE_TYPE_PS2,
	IFD_DEVICE_TYPE_OTHER
};

extern ifd_device_t *	ifd_device_open(const char *);
extern int		ifd_device_type(ifd_device_t *);
extern int		ifd_device_guess_type(const char *);
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
extern void		ifd_device_close(ifd_device_t *);

/* scheduled to go away */
extern ifd_device_t *	ifd_open_serial(const char *);
extern ifd_device_t *	ifd_open_psaux(const char *);
extern ifd_device_t *	ifd_open_usb(const char *);

#endif /* IFD_DEVICE_H */
