/*
 * Communication with ifd manager
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <stdio.h>
#include <string.h>
#include <ifd/openct.h>
#include <ifd/socket.h>
#include <ifd/conf.h>
#include <ifd/tlv.h>
#include <ifd/error.h>
#include "protocol.h"

static void	ct_args_int(ifd_buf_t *, ifd_tag_t, unsigned int);
static void	ct_args_string(ifd_buf_t *, ifd_tag_t, const char *);

/*
 * Connect to a reader manager
 */
ct_handle *
ct_reader_connect(unsigned int reader)
{
	ifd_socket_t	*sock;
	char		path[128];

	snprintf(path, sizeof(path), "%s/%u",
		ifd_config.socket_dir, reader);

	if (!(sock = ifd_socket_new(512)))
		return NULL;
	if (ifd_socket_connect(sock, path) < 0) {
		ifd_socket_free(sock);
		return NULL;
	}

	return sock;
}

/*
 * Retrieve reader status
 */
int
ct_reader_status(ct_handle *h, ct_info_t *info)
{
	ifd_tlv_parser_t tlv;
	unsigned char	buffer[256], *units;
	ifd_buf_t	args, resp;
	size_t		nunits;
	int		rc;

	memset(info, 0, sizeof(*info));

	ifd_buf_init(&resp, buffer, sizeof(buffer));
	ifd_buf_init(&args, buffer, sizeof(buffer));

	ifd_buf_putc(&args, IFD_CMD_STATUS);
	ifd_buf_putc(&args, IFD_UNIT_CT);

	rc = ifd_socket_call(h, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ifd_tlv_parse(&tlv, &resp)) < 0)
		return rc;

	/* Get the reader name first */
	ifd_tlv_get_string(&tlv, IFD_TAG_READER_NAME, info->ct_name, sizeof(info->ct_name));

	/* Get the list of device units */
	if (ifd_tlv_get_opaque(&tlv, IFD_TAG_READER_UNITS, &units, &nunits) >= 0) {
		while (nunits--) {
			unsigned int	u = *units++;

			if (u < IFD_UNIT_CT) {
				if (u >= info->ct_slots)
					info->ct_slots = u + 1;
			} else if (u == IFD_UNIT_DISPLAY) {
				info->ct_display = 1;
			} else if (u == IFD_UNIT_KEYPAD) {
				info->ct_keypad = 1;
			}
		}
	}

	return 0;
}

/*
 * Get card status
 */
int
ct_card_status(ct_handle *h, unsigned int slot, int *status)
{
	ifd_tlv_parser_t tlv;
	unsigned char	buffer[256];
	ifd_buf_t	args, resp;
	int		rc;

	ifd_buf_init(&resp, buffer, sizeof(buffer));
	ifd_buf_init(&args, buffer, sizeof(buffer));

	ifd_buf_putc(&args, IFD_CMD_STATUS);
	ifd_buf_putc(&args, slot);

	rc = ifd_socket_call(h, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ifd_tlv_parse(&tlv, &resp)) < 0)
		return rc;

	/* Get the card status */
	return ifd_tlv_get_int(&tlv, IFD_TAG_CARD_STATUS, status);
}

/*
 * Reset the card - this is the same as "request icc" without parameters
 */
int
ct_card_reset(ct_handle *h, unsigned int slot, void *atr, size_t atr_len)
{
	return ct_card_request(h, slot, 0, NULL, atr, atr_len);
}

int
ct_card_request(ct_handle *h, unsigned int slot,
		unsigned int timeout, const char *message,
		void *atr, size_t atr_len)
{
	ifd_tlv_parser_t tlv;
	unsigned char	buffer[256];
	ifd_buf_t	args, resp;
	int		rc;

	ifd_buf_init(&resp, buffer, sizeof(buffer));
	ifd_buf_init(&args, buffer, sizeof(buffer));

	ifd_buf_putc(&args, IFD_CMD_RESET);
	ifd_buf_putc(&args, slot);

	/* Add arguments if given */
	if (timeout)
		ct_args_int(&args, IFD_TAG_TIMEOUT, timeout);
	if (message)
		ct_args_string(&args, IFD_TAG_MESSAGE, message);

	rc = ifd_socket_call(h, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ifd_tlv_parse(&tlv, &resp)) < 0)
		return rc;

	/* Get the ATR */
	return ifd_tlv_get_bytes(&tlv, IFD_TAG_ATR, atr, atr_len);
}

/*
 * Transceive an APDU
 */
int
ct_card_transact(ct_handle *h, unsigned int slot,
			const void *send_data, size_t send_len,
			void *recv_buf, size_t recv_size)
{
	unsigned char	buffer[512];
	ifd_buf_t	args, resp;
	int		rc;

	ifd_buf_init(&resp, buffer, sizeof(buffer));
	ifd_buf_init(&args, recv_buf, recv_size);

	ifd_buf_putc(&args, IFD_CMD_TRANSACT);
	ifd_buf_putc(&args, slot);
	if ((rc = ifd_buf_put(&args, send_data, send_len)) < 0)
		return rc;

	rc = ifd_socket_call(h, &args, &resp);
	if (rc < 0)
		return rc;

	return ifd_buf_avail(&resp);
}

/*
 * Lock/unlock a card
 */
int
ct_card_lock(ct_handle *h, unsigned int slot, int type, ct_lock_handle *res)
{
	ifd_tlv_parser_t tlv;
	unsigned char	buffer[256];
	ifd_buf_t	args, resp;
	int		rc;

	ifd_buf_init(&resp, buffer, sizeof(buffer));
	ifd_buf_init(&args, buffer, sizeof(buffer));

	ifd_buf_putc(&args, IFD_CMD_LOCK);
	ifd_buf_putc(&args, slot);

	ct_args_int(&args, IFD_TAG_LOCKTYPE, type);

	rc = ifd_socket_call(h, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ifd_tlv_parse(&tlv, &resp)) < 0)
		return rc;

	if (ifd_tlv_get_int(&tlv, IFD_TAG_LOCK, res) < 0)
		return IFD_ERROR_GENERIC;

	return 0;
}

int
ct_card_unlock(ct_handle *h, unsigned int slot, ct_lock_handle lock)
{
	unsigned char	buffer[256];
	ifd_buf_t	args, resp;

	ifd_buf_init(&resp, buffer, sizeof(buffer));
	ifd_buf_init(&args, buffer, sizeof(buffer));

	ifd_buf_putc(&args, IFD_CMD_UNLOCK);
	ifd_buf_putc(&args, slot);

	ct_args_int(&args, IFD_TAG_LOCK, lock);

	return ifd_socket_call(h, &args, &resp);
}

/*
 * Add arguments when calling a resource manager function
 */
void
ct_args_int(ifd_buf_t *bp, ifd_tag_t tag, unsigned int value)
{
	ifd_tlv_builder_t builder;

	ifd_tlv_builder_init(&builder, bp);
	ifd_tlv_put_int(&builder, tag, value);
}

void	ct_args_string(ifd_buf_t *bp, ifd_tag_t tag, const char *value)
{
	ifd_tlv_builder_t builder;

	ifd_tlv_builder_init(&builder, bp);
	ifd_tlv_put_string(&builder, tag, value);
}
