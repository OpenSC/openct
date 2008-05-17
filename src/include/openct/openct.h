/*
 * Main OpenCT include file
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_OPENCT_H
#define OPENCT_OPENCT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

/* Various implementation limits */
#define OPENCT_MAX_READERS	16
#define OPENCT_MAX_SLOTS	8

typedef struct ct_info {
	char		ct_name[64];
	unsigned int	ct_slots;
	unsigned int	ct_card[OPENCT_MAX_SLOTS];
	unsigned 	ct_display : 1,
			ct_keypad  : 1;
	pid_t		ct_pid;
} ct_info_t;

typedef struct ct_handle	ct_handle;

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
	IFD_LOCK_EXCLUSIVE
};

/*
 * PIN encoding types
 */
enum {
	IFD_PIN_ENCODING_BCD,
	IFD_PIN_ENCODING_ASCII
};

extern int		ct_status(const ct_info_t **);

extern int		ct_reader_info(unsigned int, ct_info_t *);
extern ct_handle *	ct_reader_connect(unsigned int);
extern void		ct_reader_disconnect(ct_handle *);
extern int		ct_reader_status(ct_handle *, ct_info_t *);
extern int		ct_card_status(ct_handle *h, unsigned int slot, int *status);
extern int 		ct_card_set_protocol(ct_handle *h, unsigned int slot,
				 unsigned int protocol);
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
extern int		ct_card_verify(ct_handle *h, unsigned int slot,
				unsigned int timeout, const char *prompt,
				unsigned int pin_encoding,
				unsigned int pin_length,
				unsigned int pin_offset,
				const void *send_buf, size_t send_len,
				void *recv_buf, size_t recv_len);
extern int		ct_card_read_memory(ct_handle *, unsigned int slot,
				unsigned short address,
				void *recv_buf, size_t recv_len);
extern int		ct_card_write_memory(ct_handle *, unsigned int slot,
				unsigned short address,
				const void *send_buf, size_t send_len);

extern int		ct_status_destroy(void);
extern int		ct_status_clear(unsigned int, const char *);
extern ct_info_t *	ct_status_alloc_slot(int *);
extern int		ct_status_update(ct_info_t *);

#ifdef __cplusplus
}
#endif

#endif /* OPENCT_OPENCT_H */
