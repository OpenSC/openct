/*
 * Main OpenCT include file
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_H
#define OPENCT_H

typedef struct ct_socket		ct_handle;

#define IFD_MAX_READER		16

typedef struct ct_info {
	char		ct_name[64];
	unsigned int	ct_slots;
	unsigned char	ct_display : 1,
			ct_keypad  : 1;
} ct_info_t;

#define IFD_CARD_PRESENT        0x0001
#define IFD_CARD_STATUS_CHANGED 0x0002

/* Lock types
 *  - shared locks allow concurrent access from
 *    other applications run by the same user.
 *    Used e.g. by pkcs11 login.
 *  - exclusive locks deny any access by other
 *    applications.
 *
 * When a lock is granted, a lock handle is passed
 * to the client, which it must present in the
 * subsequent unlock call.
 */
typedef unsigned int	ct_lock_handle;
enum {
	IFD_LOCK_SHARED,
	IFD_LOCK_EXCLUSIVE,
};

extern ct_handle *	ct_reader_connect(unsigned int);
extern int		ct_reader_status(ct_handle *, ct_info_t *);
extern int		ct_card_status(ct_handle *h, unsigned int slot, int *status);
extern int		ct_card_reset(ct_handle *h, unsigned int slot,
				void *atr, size_t atr_len);
extern int		ct_card_request(ct_handle *h, unsigned int slot,
				unsigned int timeout, const char *message,
				void *atr, size_t atr_len);
extern int		ct_card_lock(ct_handle *h, unsigned int slot,
				int type, ct_lock_handle *);
extern int		ct_card_unlock(ct_handle *h, unsigned int slot,
				ct_lock_handle);
extern int		ct_card_transact(ct_handle *h, unsigned int slot,
				const void *apdu, size_t apdu_len,
				void *recv_buf, size_t recv_len);

extern int		ct_master_control(const char *command,
				char *replybuf, size_t replysize);

#endif /* OPENCT_H */

