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

enum {
	IFD_APDU_CASE_1,
	IFD_APDU_CASE_2S,
	IFD_APDU_CASE_3S,
	IFD_APDU_CASE_4S,
	IFD_APDU_CASE_2E,
	IFD_APDU_CASE_3E,
	IFD_APDU_CASE_4E,

	IFD_APDU_BAD = -1
};

typedef struct ifd_device	ifd_device_t;
typedef union ifd_device_params	ifd_device_params_t;

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

#define IFD_MAX_ATR_LEN		64
typedef struct ifd_slot {
	unsigned int		handle;
	int			status;
	unsigned int		atr_len;
	unsigned char		atr[IFD_MAX_ATR_LEN];

	ifd_protocol_t *	proto;
} ifd_slot_t;

#define IFD_MAX_SLOTS		8
typedef struct ifd_reader {
	unsigned int		num;
	unsigned int		handle;

	const char *		name;
	unsigned int		flags;
	unsigned int		nslots;
	ifd_slot_t		slot[IFD_MAX_SLOTS];

	const ifd_driver_t *	driver;
	ifd_device_t *		device;

	/* In case the IFD needs a specific protocol too */
	ifd_protocol_t *	proto;
} ifd_reader_t;

#define IFD_READER_DISPLAY	0x0100
#define IFD_READER_KEYPAD	0x0200

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

#define IFD_CARD_PRESENT	0x0001
#define IFD_CARD_STATUS_CHANGED	0x0002

extern ifd_reader_t *		ifd_new_serial(const char *, const char *);
extern ifd_reader_t *		ifd_new_usb(const char *, const char *);
extern int			ifd_attach(ifd_reader_t *);
extern void			ifd_detach(ifd_reader_t *);

extern int			ifd_activate(ifd_reader_t *);
extern int			ifd_deactivate(ifd_reader_t *);

extern int			ifd_set_protocol(ifd_reader_t *, int);
extern int			ifd_set_protocol_param(ifd_reader_t *,
					int, long);
extern int			ifd_transceive(ifd_reader_t *, int,
					ifd_apdu_t *);
extern int			ifd_card_status(ifd_reader_t *,
					unsigned int, int *);
extern int			ifd_card_reset(ifd_reader_t *,
					unsigned int,
					void *, size_t);

extern int			ifd_apdu_case(const ifd_apdu_t *,
					unsigned int *, unsigned int *);

extern struct ifd_protocol_ops *ifd_protocol_by_id(int);
extern struct ifd_protocol_ops *ifd_protocol_by_name(const char *);

extern int			ifd_select_protocol(ifd_reader_t *, const char *);

#endif /* IFD_CORE_H */
