/*
 * Communication with ifd manager
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <stdio.h>
#include <string.h>
#include <openct/openct.h>
#include <openct/socket.h>
#include <openct/tlv.h>
#include <openct/error.h>
#include "protocol.h"
#include "config.h"

static void	ct_args_int(ct_buf_t *, ifd_tag_t, unsigned int);
static void	ct_args_string(ct_buf_t *, ifd_tag_t, const char *);

/*
 * Send a control command to the master socket
 */
int
ct_master_control(const char *cmd, char *buf, size_t size)
{
	ct_socket_t	*sock;
	char		path[128];
	int		rc;

	snprintf(path, sizeof(path), "%s/master", OPENCT_SOCKET_PATH);
	if (!(sock = ct_socket_new(512)))
		return IFD_ERROR_NO_MEMORY;

	if ((rc = ct_socket_connect(sock, path)) < 0)
		goto out;

	/* Put command into send buffer and transmit */
	if ((rc = ct_socket_puts(sock, cmd)) < 0
	 || (rc = ct_socket_flsbuf(sock, 2)) < 0)
		goto out;

	/* Get complete response from server */
	while (!sock->eof) {
		if ((rc = ct_socket_filbuf(sock)) < 0)
			goto out;
	}

	rc = ct_socket_gets(sock, buf, size);

out:	ct_socket_close(sock);
	return rc;
}

/*
 * Connect to a reader manager
 */
ct_handle *
ct_reader_connect(unsigned int reader)
{
	ct_socket_t	*sock;
	char		path[128];

	snprintf(path, sizeof(path), "%s/%u",
			OPENCT_SOCKET_PATH, reader);

	if (!(sock = ct_socket_new(512)))
		return NULL;
	if (ct_socket_connect(sock, path) < 0) {
		ct_socket_free(sock);
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
	ct_tlv_parser_t tlv;
	unsigned char	buffer[256], *units;
	ct_buf_t	args, resp;
	size_t		nunits;
	int		rc;

	memset(info, 0, sizeof(*info));

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_STATUS);
	ct_buf_putc(&args, CT_UNIT_READER);

	rc = ct_socket_call(h, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ct_tlv_parse(&tlv, &resp)) < 0)
		return rc;

	/* Get the reader name first */
	ct_tlv_get_string(&tlv, CT_TAG_READER_NAME,
			info->ct_name, sizeof(info->ct_name));

	/* Get the list of device units */
	if (ct_tlv_get_opaque(&tlv, CT_TAG_READER_UNITS, &units, &nunits) >= 0) {
		while (nunits--) {
			unsigned int	u = *units++;

			if (u < CT_UNIT_READER) {
				if (u >= info->ct_slots)
					info->ct_slots = u + 1;
			} else if (u == CT_UNIT_DISPLAY) {
				info->ct_display = 1;
			} else if (u == CT_UNIT_KEYPAD) {
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
	ct_tlv_parser_t tlv;
	unsigned char	buffer[256];
	ct_buf_t	args, resp;
	int		rc;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_STATUS);
	ct_buf_putc(&args, slot);

	rc = ct_socket_call(h, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ct_tlv_parse(&tlv, &resp)) < 0)
		return rc;

	/* Get the card status */
	return ct_tlv_get_int(&tlv, CT_TAG_CARD_STATUS, status);
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
	ct_tlv_parser_t tlv;
	unsigned char	buffer[256];
	ct_buf_t	args, resp;
	int		rc;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_RESET);
	ct_buf_putc(&args, slot);

	/* Add arguments if given */
	if (timeout)
		ct_args_int(&args, CT_TAG_TIMEOUT, timeout);
	if (message)
		ct_args_string(&args, CT_TAG_MESSAGE, message);

	rc = ct_socket_call(h, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ct_tlv_parse(&tlv, &resp)) < 0)
		return rc;

	/* Get the ATR */
	return ct_tlv_get_bytes(&tlv, CT_TAG_ATR, atr, atr_len);
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
	ct_buf_t	args, resp;
	int		rc;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, recv_buf, recv_size);

	ct_buf_putc(&args, CT_CMD_TRANSACT);
	ct_buf_putc(&args, slot);
	if ((rc = ct_buf_put(&args, send_data, send_len)) < 0)
		return rc;

	rc = ct_socket_call(h, &args, &resp);
	if (rc < 0)
		return rc;

	return ct_buf_avail(&resp);
}

/*
 * Lock/unlock a card
 */
int
ct_card_lock(ct_handle *h, unsigned int slot, int type, ct_lock_handle *res)
{
	ct_tlv_parser_t tlv;
	unsigned char	buffer[256];
	ct_buf_t	args, resp;
	int		rc;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_LOCK);
	ct_buf_putc(&args, slot);

	ct_args_int(&args, CT_TAG_LOCKTYPE, type);

	rc = ct_socket_call(h, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ct_tlv_parse(&tlv, &resp)) < 0)
		return rc;

	if (ct_tlv_get_int(&tlv, CT_TAG_LOCK, res) < 0)
		return IFD_ERROR_GENERIC;

	return 0;
}

int
ct_card_unlock(ct_handle *h, unsigned int slot, ct_lock_handle lock)
{
	unsigned char	buffer[256];
	ct_buf_t	args, resp;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_UNLOCK);
	ct_buf_putc(&args, slot);

	ct_args_int(&args, CT_TAG_LOCK, lock);

	return ct_socket_call(h, &args, &resp);
}

/*
 * Add arguments when calling a resource manager function
 */
void
ct_args_int(ct_buf_t *bp, ifd_tag_t tag, unsigned int value)
{
	ct_tlv_builder_t builder;

	ct_tlv_builder_init(&builder, bp);
	ct_tlv_put_int(&builder, tag, value);
}

void	ct_args_string(ct_buf_t *bp, ifd_tag_t tag, const char *value)
{
	ct_tlv_builder_t builder;

	ct_tlv_builder_init(&builder, bp);
	ct_tlv_put_string(&builder, tag, value);
}
