/*
 * Main OpenCT include file
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_H
#define OPENCT_H

typedef struct ifd_socket		ct_handle;

#define IFD_MAX_READER		16

typedef struct ct_info {
	char		ct_name[64];
	unsigned int	ct_slots;
	unsigned char	ct_display : 1,
			ct_keypad  : 1;
} ct_info_t;

#define IFD_CARD_PRESENT        0x0001
#define IFD_CARD_STATUS_CHANGED 0x0002

extern ct_handle *	ct_reader_connect(unsigned int);
extern int		ct_reader_status(ct_handle *, ct_info_t *);
extern int		ct_card_status(ct_handle *h, unsigned int slot, int *status);
extern int		ct_card_reset(ct_handle *h, unsigned int slot,
				void *atr, size_t atr_len);
extern int		ct_card_request(ct_handle *h, unsigned int slot,
				unsigned int timeout, const char *message,
				void *atr, size_t atr_len);

#endif /* OPENCT_H */

