/*
 * Core functions of the IFD handler library
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef IFD_INTERNAL_H
#define IFD_INTERNAL_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <openct/types.h>
#include <openct/ifd.h>
#include <openct/device.h>
#include <openct/driver.h>
#include <openct/conf.h>
#include <openct/logging.h>
#include <openct/error.h>
#include <openct/buffer.h>

struct ifd_device {
	char *name;
	int type;
	long timeout;

	unsigned int hotplug:1;

	int fd;
	void *dev;		/* use instead of fd, if no fd available for implementation */

	ifd_device_params_t settings;
	struct ifd_device_ops *ops;

	void *user_data;

	/* per-device data may follow */

	unsigned int etu;	/* XXX: unnecessary? */
};

struct ifd_device_ops {
	/* Reset device */
	int (*reset) (ifd_device_t *);

	int (*set_params) (ifd_device_t *, const ifd_device_params_t *);
	int (*get_params) (ifd_device_t *, ifd_device_params_t *);

	/* Flush any pending input */
	void (*flush) (ifd_device_t *);
	void (*send_break) (ifd_device_t *, unsigned int);

	/*
	 * Send/receive data. Some devices such as USB will support
	 * the transceive command, others such as serial devices will
	 * need to use send/recv
	 */
	int (*transceive) (ifd_device_t *,
			   const void *, size_t, void *, size_t, long);
	int (*send) (ifd_device_t *, const unsigned char *, size_t);
	int (*recv) (ifd_device_t *, unsigned char *, size_t, long);
	int (*control) (ifd_device_t *, void *, size_t);

	void (*close) (ifd_device_t *);

	int (*get_eventfd) (ifd_device_t *, short *events);

	/* Poll for device presence. This function is called
	 * prior to the poll call (with revents == 0), in this
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
	int (*poll_presence) (ifd_device_t *, struct pollfd *);
};

struct ifd_protocol_ops {
	int id;
	const char *name;
	size_t size;
	int (*init) (ifd_protocol_t *);
	void (*release) (ifd_protocol_t *);
	int (*set_param) (ifd_protocol_t *, int, long);
	int (*get_param) (ifd_protocol_t *, int, long *);
	int (*resynchronize) (ifd_protocol_t *, int dad);
	int (*transceive) (ifd_protocol_t *, int dad,
			   const void *, size_t, void *, size_t);
	int (*sync_read) (ifd_protocol_t *, int,
			  unsigned short, unsigned char *, size_t);
	int (*sync_write) (ifd_protocol_t *, int,
			   unsigned short, const unsigned char *, size_t);
};

struct ifd_protocol {
	ifd_reader_t *reader;
	unsigned int dad;
	struct ifd_protocol_ops *ops;
};

extern struct ifd_protocol_ops ifd_protocol_t1;
extern struct ifd_protocol_ops ifd_protocol_t0;
extern struct ifd_protocol_ops ifd_protocol_gbp;
extern struct ifd_protocol_ops ifd_protocol_trans;
extern struct ifd_protocol_ops ifd_protocol_i2c_short;
extern struct ifd_protocol_ops ifd_protocol_i2c_long;
extern struct ifd_protocol_ops ifd_protocol_2wire;
extern struct ifd_protocol_ops ifd_protocol_3wire;
extern struct ifd_protocol_ops ifd_protocol_eurochip;
extern struct ifd_protocol_ops ifd_protocol_esc;

extern void ifd_acr30u_register(void);
extern void ifd_cardman_register(void);
extern void ifd_ccid_register(void);
extern void ifd_cm4000_register(void);
extern void ifd_egate_register(void);
extern void ifd_epass3k_register(void);
extern void ifd_etoken_register(void);
extern void ifd_etoken64_register(void);
extern void ifd_eutron_register(void);
extern void ifd_gempc_register(void);
extern void ifd_ikey2k_register(void);
extern void ifd_ikey3k_register(void);
extern void ifd_kaan_register(void);
extern void ifd_pertosmart_ac1030_register(void);
extern void ifd_pertosmart_ac1038_register(void);
extern void ifd_smartboard_register(void);
extern void ifd_smph_register(void);
extern void ifd_starkey_register(void);
extern void ifd_towitoko_register(void);
/* extern void ifd_wbeiuu_register(void); driver not working yet */
extern void ifd_cyberjack_register(void);
extern void ifd_rutoken_register(void);

/* reader.c */
extern int ifd_error(ifd_reader_t *);
extern int ifd_event(ifd_reader_t *);
extern int ifd_send_command(ifd_protocol_t *, const void *, size_t);
extern int ifd_recv_response(ifd_protocol_t *, void *, size_t, long);

/* driver.c */
extern unsigned int ifd_drivers_list(const char **, size_t);

/* device.c */
extern ifd_device_t *ifd_open_pcmcia_block(const char *);
extern ifd_device_t *ifd_open_pcmcia(const char *);
extern ifd_device_t *ifd_open_psaux(const char *);
extern ifd_device_t *ifd_open_remote(const char *);
extern ifd_device_t *ifd_open_serial(const char *);
extern ifd_device_t *ifd_open_usb(const char *);
extern ifd_device_t *ifd_device_new(const char *,
				    struct ifd_device_ops *, size_t);
extern void ifd_device_free(ifd_device_t *);

/* checksum.c */
extern unsigned int csum_lrc_compute(const uint8_t *, size_t, unsigned char *);
extern unsigned int csum_crc_compute(const uint8_t *, size_t, unsigned char *);

/* Internal system dependent device functions */
extern int ifd_sysdep_usb_poll_presence(ifd_device_t *, struct pollfd *);
extern int ifd_sysdep_usb_get_eventfd(ifd_device_t *, short *events);
extern int ifd_sysdep_usb_control(ifd_device_t *,
				  unsigned int,
				  unsigned int,
				  unsigned int,
				  unsigned int, void *, size_t, long);
extern int ifd_sysdep_usb_bulk(ifd_device_t *, int, void *, size_t, long);
extern int ifd_sysdep_usb_set_configuration(ifd_device_t *, int);
extern int ifd_sysdep_usb_set_interface(ifd_device_t *, int, int);
extern int ifd_sysdep_usb_claim_interface(ifd_device_t *, int);
extern int ifd_sysdep_usb_release_interface(ifd_device_t *, int);
extern int ifd_sysdep_usb_begin_capture(ifd_device_t *, int, int, size_t,
					ifd_usb_capture_t **);
extern int ifd_sysdep_usb_capture_event(ifd_device_t *, ifd_usb_capture_t *,
			   void *, size_t);
extern int ifd_sysdep_usb_capture(ifd_device_t *, ifd_usb_capture_t *, void *,
				  size_t, long);
extern int ifd_sysdep_usb_end_capture(ifd_device_t *, ifd_usb_capture_t * cap);
extern int ifd_sysdep_usb_open(const char *device);
extern int ifd_sysdep_usb_reset(ifd_device_t *);

/* module.c */
extern int ifd_load_module(const char *, const char *);

/* utils.c */
extern void ifd_revert_bits(unsigned char *, size_t);
extern unsigned int ifd_count_bits(unsigned int);
extern long ifd_time_elapsed(struct timeval *);
#ifndef HAVE_DAEMON
extern int daemon(int, int);
#endif

/* protocol.c */
extern int ifd_protocol_register(struct ifd_protocol_ops *);
extern int ifd_sync_detect_icc(ifd_reader_t *, int, void *, size_t);
extern unsigned int ifd_protocols_list(const char **, unsigned int);

/* proto-t1.c */
extern int t1_negotiate_ifsd(ifd_protocol_t *, unsigned int, int);

#endif				/* IFD_INTERNAL_H */
