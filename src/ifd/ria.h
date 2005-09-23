/*
 * Access to remote IFD handlers
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef IFD_REMOTE_H
#define IFD_REMOTE_H

typedef struct ria_client {
	/* Socket for communication with ifdproxy */
	ct_socket_t *sock;
	uint32_t xid;

	/* queue for buffering data */
	ct_buf_t data;

	/* application data */
	void *user_data;
} ria_client_t;

#define RIA_NAME_MAX	32
typedef struct ria_device {
	char address[RIA_NAME_MAX];
	char type[RIA_NAME_MAX / 2];
	char handle[RIA_NAME_MAX];
	char name[RIA_NAME_MAX];
} ria_device_t;

typedef struct ria_serial_conf {
	uint32_t speed;
	uint8_t bits;
	uint8_t stopbits;
	uint8_t parity;
	uint8_t check_parity;
	uint8_t dtr;
	uint8_t rts;
} ria_serial_conf_t;

enum {
	/* These are for the manager only */
	RIA_MGR_LIST = 0x00,
	RIA_MGR_INFO,
	RIA_MGR_CLAIM,
	RIA_MGR_REGISTER,

	__RIA_PEER_CMD_BASE = 0x10,
	RIA_RESET_DEVICE = 0x10,
	RIA_FLUSH_DEVICE,
	RIA_SEND_BREAK,
	RIA_SERIAL_GET_CONFIG,
	RIA_SERIAL_SET_CONFIG,

	RIA_DATA = 0x80
};

extern ria_client_t *ria_connect(const char *);
extern void ria_free(ria_client_t *);
extern int ria_send(ria_client_t *, unsigned char, const void *, size_t);
extern int ria_command(ria_client_t *, unsigned char,
		       const void *, size_t, void *, size_t, long timeout);

extern int ria_svc_listen(const char *, int);
extern ria_client_t *ria_export_device(const char *, const char *);
extern int ria_register_device(ria_client_t *, const char *);
extern void ria_print_packet(ct_socket_t *, int,
			     const char *, header_t *, ct_buf_t *);

#endif				/* IFD_REMOTE_H */
