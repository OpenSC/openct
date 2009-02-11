/*
 * Core functions of the IFD handler library
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_IFD_H
#define OPENCT_IFD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <openct/openct.h>
#include <openct/apdu.h>

typedef struct ifd_device	ifd_device_t;
typedef union ifd_device_params	ifd_device_params_t;

enum {
	IFD_PROTOCOL_DEFAULT = -1,
	IFD_PROTOCOL_T0 = 0,
	IFD_PROTOCOL_T1,
	IFD_PROTOCOL_2WIRE = 16,
	IFD_PROTOCOL_3WIRE,
	IFD_PROTOCOL_I2C_SHORT,
	IFD_PROTOCOL_I2C_LONG,
	IFD_PROTOCOL_TLP,		/* older Gemplus protocol */
	IFD_PROTOCOL_GBP,		/* Gemplus block protocol */
	IFD_PROTOCOL_EUROCHIP,		/* Eurochip Countercard */
	IFD_PROTOCOL_TCL,		/* ISO 14443-4 T=CL */
	IFD_PROTOCOL_ESCAPE,		/* Virtual 'escape' protocol */
	IFD_PROTOCOL_TRANSPARENT = 128
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
	time_t			next_update;

	unsigned char		dad;	/* address when using T=1 */
	unsigned int		atr_len;
	unsigned char		atr[IFD_MAX_ATR_LEN];

	ifd_protocol_t *	proto;
	void *			reader_data;
} ifd_slot_t;

typedef struct ifd_reader {
	unsigned int		num;
	unsigned int		handle;

	const char *		name;
	unsigned int		flags;
	unsigned int		nslots;
	ifd_slot_t		slot[OPENCT_MAX_SLOTS];

	const ifd_driver_t *	driver;
	ifd_device_t *		device;
	ct_info_t *		status;

	/* In case the IFD needs to keep state */
	void *			driver_data;
} ifd_reader_t;

#define IFD_READER_ACTIVE	0x0001
#define IFD_READER_HOTPLUG	0x0002
#define IFD_READER_DISPLAY	0x0100
#define IFD_READER_KEYPAD	0x0200

enum {
	IFD_PROTOCOL_RECV_TIMEOUT = 0x0000,
	IFD_PROTOCOL_BLOCK_ORIENTED,

	/* T=0 specific parameters */
	__IFD_PROTOCOL_T0_PARAM_BASE = IFD_PROTOCOL_T0 << 16,

	/* T=1 specific parameters */
	__IFD_PROTOCOL_T1_PARAM_BASE = IFD_PROTOCOL_T1 << 16,
	IFD_PROTOCOL_T1_BLOCKSIZE,
	IFD_PROTOCOL_T1_CHECKSUM_CRC,
	IFD_PROTOCOL_T1_CHECKSUM_LRC,
	IFD_PROTOCOL_T1_IFSC,
	IFD_PROTOCOL_T1_IFSD,
	IFD_PROTOCOL_T1_STATE,
	IFD_PROTOCOL_T1_MORE
};

enum {
	IFD_DAD_HOST = 0,
	IFD_DAD_IFD,
	IFD_DAD_ICC1,
	IFD_DAD_ICC2
};


extern int			ifd_init(void);

extern ifd_reader_t *		ifd_open(const char *driver_name,
					const char *device_name);
extern void			ifd_close(ifd_reader_t *);
extern int			ifd_reader_count(void);
extern int			ifd_attach(ifd_reader_t *);
extern void			ifd_detach(ifd_reader_t *);
extern ifd_reader_t *		ifd_reader_by_handle(unsigned int handle);
extern ifd_reader_t *		ifd_reader_by_index(unsigned int index);

extern int			ifd_spawn_handler(const char *, const char *, int);
extern int			ifd_scan_usb(void);

extern int			ifd_activate(ifd_reader_t *);
extern int			ifd_deactivate(ifd_reader_t *);
extern int			ifd_output(ifd_reader_t *, const char *);

extern int			ifd_atr_complete(const unsigned char *, size_t);

extern int			ifd_set_protocol(ifd_reader_t *reader,
					unsigned int slot,
					int id);
extern int			ifd_card_command(ifd_reader_t *reader,
					unsigned int slot,
					const void *sbuf, size_t slen,
					void *rbuf, size_t rlen);
extern int			ifd_card_status(ifd_reader_t *reader,
					unsigned int slot,
					int *status);
extern int			ifd_card_reset(ifd_reader_t *reader,
					unsigned int slot,
					void *atr_buf,
					size_t atr_len);
extern int			ifd_card_request(ifd_reader_t *reader,
					unsigned int slot,
					time_t timeout,
					const char *message,
					void *atr_buf,
					size_t atr_len);
extern int			ifd_card_eject(ifd_reader_t *reader,
					unsigned int slot,
					time_t timeout,
					const char *message);
extern int			ifd_card_perform_verify(ifd_reader_t *reader,
					unsigned int slot,
					time_t timeout,
					const char *message,
					const unsigned char *data, size_t data_len,
					unsigned char *resp, size_t resp_len);
extern int			ifd_card_read_memory(ifd_reader_t *,
					unsigned int, unsigned short,
					unsigned char *, size_t);
extern int			ifd_card_write_memory(ifd_reader_t *,
					unsigned int, unsigned short,
					const unsigned char *, size_t);

extern ifd_protocol_t *		ifd_protocol_new(int id,
					ifd_reader_t *reader,
					unsigned int dad);
extern int			ifd_protocol_set_parameter(ifd_protocol_t *p,
					int type,
					long value);
extern int			ifd_protocol_get_parameter(ifd_protocol_t *p,
					int type,
					long *value);
extern int			ifd_protocol_read_memory(ifd_protocol_t *,
					int, unsigned short,
					unsigned char *, size_t);
extern int			ifd_protocol_write_memory(ifd_protocol_t *,
					int, unsigned short,
					const unsigned char *, size_t);
extern void			ifd_protocol_free(ifd_protocol_t *);
extern int			ifd_before_command(ifd_reader_t *);
extern int			ifd_after_command(ifd_reader_t *);
extern int			ifd_get_eventfd(ifd_reader_t *, short *);
extern void			ifd_poll(ifd_reader_t *);
extern int			id_event(ifd_reader_t *);

/* Debugging macro */
#ifdef __GNUC__
#define ifd_debug(level, fmt, args...) \
	do { \
		if ((level) <= ct_config.debug) \
			ct_debug("%s: " fmt, __FUNCTION__ , ##args); \
	} while (0)
#else
extern void			ifd_debug(int level, const char *fmt, ...);
#endif

#ifdef __cplusplus
}
#endif

#endif /* OPENCT_IFD_H */
