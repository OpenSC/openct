/*
 * Core functions of the IFD handler library
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef IFD_INTERNAL_H
#define IFD_INTERNAL_H

#include "ifd.h"
#include "device.h"
#include "driver.h"
#include <openct/conf.h>
#include <openct/config.h>
#include <openct/logging.h>
#include <openct/error.h>
#include <openct/buffer.h>

/* For poll_presence */
struct pollfd;

struct ifd_device {
	char *		name;
	int		type;
	unsigned int	etu;
	long		timeout;
	ifd_device_params_t settings;

	struct ifd_device_ops *ops;

	int		fd;

	/* per-device data may follow */
};

struct ifd_device_ops {
	/* Try to identify the attached device. In the case of
	 * a serial device, perform serial PnP. For USB devices,
	 * get the vendor/device ID */
	int		(*identify)(ifd_device_t *, char *, size_t);

	/* Reset device */
#if 0
	int		(*reset)(ifd_device_t *, void *, size_t);
#endif

	int		(*set_params)(ifd_device_t *, const ifd_device_params_t *);
	int		(*get_params)(ifd_device_t *, ifd_device_params_t *);

	/* Flush any pending input */
	void		(*flush)(ifd_device_t *);

	/*
	 * Send/receive data. Some devices such as USB will support
	 * the transceive command, others such as serial devices will
	 * need to use send/recv
	 */
	int		(*transceive)(ifd_device_t *,
					const void *, size_t,
					void *, size_t, long);
	int		(*send)(ifd_device_t *, const void *, size_t);
	int		(*recv)(ifd_device_t *, void *, size_t, long);
	int		(*control)(ifd_device_t *, void *, size_t);

	void		(*close)(ifd_device_t *);

	/* Poll for device presence. This function is called
	 * prior to the poll call (with revents == 0), int this
	 * case poll_presence is supposed to set up the poll
	 * structure.
	 * Then, it is called after poll() returns - in this case
	 * it should check the contents of pollfd to find out
	 * whether the device got removed.
	 *
	 * This is pretty much tailored for USB support, so
	 * the addition of PCMCIA devices may cause this
	 * to change.
	 */
	int		(*poll_presence)(ifd_device_t *, struct pollfd *);
};

struct ifd_protocol_ops {
	int		id;
	const char *	name;
	size_t		size;
	int		(*init)(ifd_protocol_t *);
	void		(*release)(ifd_protocol_t *);
	int		(*set_param)(ifd_protocol_t *, int, long);
	int		(*get_param)(ifd_protocol_t *, int, long *);
	int		(*transceive)(ifd_protocol_t *, int dad,
					const void *, size_t,
					void *, size_t);
};

struct ifd_protocol {
	ifd_reader_t	*reader;
	unsigned int	dad;
	struct ifd_protocol_ops	*ops;
};

extern struct ifd_protocol_ops	ifd_protocol_t1;
extern struct ifd_protocol_ops	ifd_protocol_t0;
extern struct ifd_protocol_ops	ifd_protocol_trans;

/* Debugging macros */
#define ifd_debug(level, fmt, args...) \
	do { \
		if ((level) <= ct_config.debug) \
			ct_debug("%s: " fmt, __FUNCTION__ , ##args); \
	} while (0)

/* reader.c */
extern int		ifd_send_command(ifd_protocol_t *,
				const void *, size_t);
extern int		ifd_recv_response(ifd_protocol_t *,
				void *, size_t, long);

/* driver.c */
extern void		ifd_driver_register(const char *,
				struct ifd_driver_ops *);
extern const ifd_driver_t *ifd_driver_get(const char *);
extern const char *	ifd_driver_for_id(const char *);

/* device.c */
extern ifd_device_t *	ifd_open_serial(const char *);
extern ifd_device_t *	ifd_open_psaux(const char *);
extern ifd_device_t *	ifd_open_usb(const char *);
extern ifd_device_t *	ifd_device_new(const char *,
				struct ifd_device_ops *, size_t);
extern void		ifd_device_free(ifd_device_t *);


/* checksum.c */
extern unsigned int	csum_lrc_compute(const unsigned char *, size_t, unsigned char *);
extern unsigned int	csum_crc_compute(const unsigned char *, size_t, unsigned char *);

/* hotplug.c */
extern int		ifd_hotplug_init(void);
extern int		ifd_hotplug_attach(const char *, const char *);
extern int		ifd_hotplug_detach(const char *, const char *);

/* module.c */
extern int		ifd_load_module(const char *, const char *);

/* utils.c */
extern void		ifd_revert_bits(unsigned char *, size_t);
extern unsigned int	ifd_count_bits(unsigned int);

/* protocol.c */
extern void		ifd_protocol_register(struct ifd_protocol_ops *);

#endif /* IFD_INTERNAL_H */
