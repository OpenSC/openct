/*
 * Core functions of the IFD handler library
 *
 */

#ifndef IFD_CORE_H
#define IFD_CORE_H

#include <sys/types.h>

typedef struct ifd_apdu {
	unsigned char *		snd_buf;
	unsigned int		snd_len;
	unsigned char *		rcv_buf;
	unsigned int		rcv_len;
} ifd_apdu_t;

typedef struct ifd_device	ifd_device_t;

typedef struct ifd_device_params_t {
	/* TBD */
} ifd_device_params_t;

enum {
	IFD_PROTOCOL_DEFAULT = 0,
	IFD_PROTOCOL_T0 = 1,
	IFD_PROTOCOL_T1,
	IFD_PROTOCOL_2WIRE,
	IFD_PROTOCOL_3WIRE,
	IFD_PROTOCOL_I2C,
	IFD_PROTOCOL_TLP,		/* older Gemplus protocool */
	IFD_PROTOCOL_GBP,		/* Gemplus block protocol */

};

typedef struct ifd_protocol	ifd_protocol_t;

typedef struct ifd_driver {
	const char *		name;
	struct ifd_driver_ops *	ops;
} ifd_driver_t;

typedef struct ifd_reader {
	unsigned int		num;
	unsigned int		handle;
	ifd_device_t *		device;
	ifd_protocol_t *	proto;
	void *			proto_state;
	ifd_driver_t *		driver;
} ifd_reader_t;

enum {
	IFD_PROTOCOL_RECV_TIMEOUT = 0x0000,

	/* T=0 specific parameters */
	__IFD_PROTOCOL_T0_PARAM_BASE = IFD_PROTOCOL_T0 << 16,

	/* T=1 specific parameters */
	__IFD_PROTOCOL_T1_PARAM_BASE = IFD_PROTOCOL_T1 << 16,
	IFD_PROTOCOL_T1_BLOCKSIZE,
};

enum {
	IFD_DAD_ICC1 = 0,
	IFD_DAD_IFD = 1,
	IFD_DAD_ICC2 = 2,
};

extern int			ifd_set_protocol(ifd_reader_t *, int);
extern int			ifd_set_protocol_param(ifd_reader_t *, int, long);
extern int			ifd_transceive(ifd_reader_t *, int, ifd_apdu_t *);

extern ifd_protocol_t *		ifd_protocol_by_id(int);
extern ifd_protocol_t *		ifd_protocol_by_name(const char *);

extern int			ifd_select_protocol(ifd_reader_t *, const char *);

/* Internal stuff, really */
extern ifd_device_t *		ifd_open_serial(const char *);

#endif /* IFD_CORE_H */
