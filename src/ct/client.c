/*
 * Communication with ifd handler
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <openct/openct.h>
#include <openct/socket.h>
#include <openct/tlv.h>
#include <openct/error.h>
#include <openct/path.h>
#include <openct/protocol.h>

struct ct_handle {
	ct_socket_t *sock;
	unsigned int index;	/* reader index */
	unsigned int card[OPENCT_MAX_SLOTS];	/* card seq */
	const ct_info_t *info;
};

static void ct_args_int(ct_buf_t *, ifd_tag_t, unsigned int);
static void ct_args_string(ct_buf_t *, ifd_tag_t, const char *);
static void ct_args_opaque(ct_buf_t *, ifd_tag_t,
			   const unsigned char *, size_t);

/*
 * Get reader info
 */
int ct_reader_info(unsigned int reader, ct_info_t * result)
{
	const ct_info_t *info;
	int rc;

	if ((rc = ct_status(&info)) < 0 || reader > (unsigned int)rc)
		return -1;

	/* Make sure the server process is alive */
	if (info[reader].ct_pid == 0
	    || (kill(info[reader].ct_pid, 0) < 0 && errno == ESRCH))
		return -1;

	*result = info[reader];
	return 0;
}

/*
 * Connect to a reader manager
 */
ct_handle *ct_reader_connect(unsigned int reader)
{
	const ct_info_t *info;
	char path[PATH_MAX];
	char file[PATH_MAX];
	ct_handle *h;
	int rc, len;

	len = PATH_MAX;

	snprintf(file, PATH_MAX, "%d", reader);
	if (!ct_format_path(path, PATH_MAX, file)) {
		return NULL;
	}

	if ((rc = ct_status(&info)) < 0 || reader > (unsigned int)rc)
		return NULL;

	if (!(h = (ct_handle *) calloc(1, sizeof(*h))))
		return NULL;

	if (!(h->sock = ct_socket_new(CT_SOCKET_BUFSIZ))) {
		free(h);
		return NULL;
	}
	if (ct_socket_connect(h->sock, path) < 0) {
		ct_reader_disconnect(h);
		return NULL;
	}

	h->info = info + reader;
	return h;
}

/*
 * Disconnect from reader manager
 */
void ct_reader_disconnect(ct_handle * h)
{
	if (h->sock)
		ct_socket_free(h->sock);
	memset(h, 0, sizeof(*h));
	free(h);
}

/*
 * Retrieve reader status
 */
int ct_reader_status(ct_handle * h, ct_info_t * info)
{
	*info = *h->info;
	return 0;
}

#if 0
/*
 * Print something to the reader's display
 */
int ct_reader_output(ct_handle * h, const char *message)
{
	unsigned char buffer[256];
	ct_buf_t args, resp;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_OUTPUT);
	ct_buf_putc(&args, CT_UNIT_READER);

	/* Add arguments if given */
	if (message)
		ct_args_string(&args, CT_TAG_MESSAGE, message);

	return ct_socket_call(h->sock, &args, &resp);
}
#endif

/*
 * Get card status
 */
int ct_card_status(ct_handle * h, unsigned int slot, int *status)
{
	const ct_info_t *info;
	unsigned int seq;

	info = h->info;
	if (slot > info->ct_slots)
		return IFD_ERROR_INVALID_ARG;

	seq = info->ct_card[slot];

	*status = 0;
	if (seq != 0) {
		*status = IFD_CARD_PRESENT;
		if (seq != h->card[slot])
			*status |= IFD_CARD_STATUS_CHANGED;
	}

	h->card[slot] = seq;
	return 0;
}

/*
 * Reset the card - this is the same as "request icc" without parameters
 */
int ct_card_reset(ct_handle * h, unsigned int slot, void *atr, size_t atr_len)
{
	return ct_card_request(h, slot, 0, NULL, atr, atr_len);
}

int ct_card_request(ct_handle * h, unsigned int slot,
		    unsigned int timeout, const char *message,
		    void *atr, size_t atr_len)
{
	ct_tlv_parser_t tlv;
	unsigned char buffer[256];
	ct_buf_t args, resp;
	int rc;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_RESET);
	ct_buf_putc(&args, slot);

	/* Add arguments if given */
	if (timeout)
		ct_args_int(&args, CT_TAG_TIMEOUT, timeout);
	if (message)
		ct_args_string(&args, CT_TAG_MESSAGE, message);

	rc = ct_socket_call(h->sock, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ct_tlv_parse(&tlv, &resp)) < 0)
		return rc;

	/* Get the ATR. There may be no ATR if the card is synchronous */
	rc = ct_tlv_get_bytes(&tlv, CT_TAG_ATR, atr, atr_len);
	if (rc < 0)
		rc = 0;

	return rc;
}

#if 0
int ct_card_eject(ct_handle * h, unsigned int slot,
		  unsigned int timeout, const char *message)
{
	unsigned char buffer[256];
	ct_buf_t args, resp;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_EJECT_ICC);
	ct_buf_putc(&args, slot);

	/* Add arguments if given */
	if (timeout)
		ct_args_int(&args, CT_TAG_TIMEOUT, timeout);
	if (message)
		ct_args_string(&args, CT_TAG_MESSAGE, message);

	return ct_socket_call(h->sock, &args, &resp);
}
#endif

int ct_card_set_protocol(ct_handle * h, unsigned int slot,
			 unsigned int protocol)
{
	unsigned char buffer[256];
	ct_buf_t args, resp;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_SET_PROTOCOL);
	ct_buf_putc(&args, slot);
	ct_args_int(&args, CT_TAG_PROTOCOL, protocol);
	return ct_socket_call(h->sock, &args, &resp);
}

/*
 * Transceive an APDU
 */
int ct_card_transact(ct_handle * h, unsigned int slot,
		     const void *send_data, size_t send_len,
		     void *recv_buf, size_t recv_size)
{
	ct_tlv_parser_t tlv;
	unsigned char buffer[CT_SOCKET_BUFSIZ];
	ct_buf_t args, resp;
	int rc;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_TRANSACT);
	ct_buf_putc(&args, slot);

	ct_args_opaque(&args, CT_TAG_CARD_REQUEST,
		       (const unsigned char *)send_data, send_len);

	rc = ct_socket_call(h->sock, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ct_tlv_parse(&tlv, &resp)) < 0)
		return rc;

	/* Get the ATR */
	return ct_tlv_get_bytes(&tlv, CT_TAG_CARD_RESPONSE,
				recv_buf, recv_size);
}

/*
 * Read from a synchronous card
 */
int ct_card_read_memory(ct_handle * h, unsigned int slot,
			unsigned short address, void *recv_buf, size_t recv_len)
{
	ct_tlv_parser_t tlv;
	unsigned char buffer[CT_SOCKET_BUFSIZ];
	ct_buf_t args, resp;
	int rc;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_MEMORY_READ);
	ct_buf_putc(&args, slot);

	ct_args_int(&args, CT_TAG_ADDRESS, address);
	ct_args_int(&args, CT_TAG_COUNT, recv_len);

	rc = ct_socket_call(h->sock, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ct_tlv_parse(&tlv, &resp)) < 0)
		return rc;

	return ct_tlv_get_bytes(&tlv, CT_TAG_DATA, recv_buf, recv_len);
}

int ct_card_write_memory(ct_handle * h, unsigned int slot,
			 unsigned short address,
			 const void *send_buf, size_t send_len)
{
	unsigned char buffer[CT_SOCKET_BUFSIZ];
	ct_buf_t args, resp;
	int rc;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_MEMORY_WRITE);
	ct_buf_putc(&args, slot);

	ct_args_int(&args, CT_TAG_ADDRESS, address);
	ct_args_opaque(&args, CT_TAG_DATA, (const unsigned char *)send_buf,
		       send_len);

	rc = ct_socket_call(h->sock, &args, &resp);
	if (rc < 0)
		return rc;

	return 0;
}

/*
 * Verify PIN
 */
int ct_card_verify(ct_handle * h, unsigned int slot,
		   unsigned int timeout, const char *prompt,
		   unsigned int pin_encoding,
		   unsigned int pin_length,
		   unsigned int pin_offset,
		   const void *send_buf, size_t send_len,
		   void *recv_buf, size_t recv_len)
{
	unsigned char buffer[256];
	ct_buf_t args, resp;
	ct_tlv_builder_t builder;
	ct_tlv_parser_t parser;
	unsigned char control = 0x00;
	int rc;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, recv_buf, recv_len);

	ct_buf_putc(&args, CT_CMD_PERFORM_VERIFY);
	ct_buf_putc(&args, slot);

	if (timeout)
		ct_args_int(&args, CT_TAG_TIMEOUT, timeout);
	if (prompt)
		ct_args_string(&args, CT_TAG_MESSAGE, prompt);

	ct_tlv_builder_init(&builder, &args, 1);
	ct_tlv_put_tag(&builder, CT_TAG_PIN_DATA);

	/* Build the control byte */
	if (pin_encoding == IFD_PIN_ENCODING_ASCII)
		control |= 0x01;
	else if (pin_encoding != IFD_PIN_ENCODING_BCD)
		return IFD_ERROR_INVALID_ARG;
	if (pin_length)
		control |= pin_length << 4;
	ct_tlv_add_byte(&builder, control);

	/* Offset is 1 based */
	ct_tlv_add_byte(&builder, pin_offset + 1);
	ct_tlv_add_bytes(&builder, (const unsigned char *)send_buf, send_len);

	rc = ct_socket_call(h->sock, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ct_tlv_parse(&parser, &resp)) < 0)
		return rc;

	/* Get the ATR */
	return ct_tlv_get_bytes(&parser,
				CT_TAG_CARD_RESPONSE, recv_buf, recv_len);
}

/*
 * Lock/unlock a card
 */
int ct_card_lock(ct_handle * h, unsigned int slot, int type,
		 ct_lock_handle * res)
{
	ct_tlv_parser_t tlv;
	unsigned char buffer[256];
	ct_buf_t args, resp;
	int rc;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_LOCK);
	ct_buf_putc(&args, slot);

	ct_args_int(&args, CT_TAG_LOCKTYPE, type);

	rc = ct_socket_call(h->sock, &args, &resp);
	if (rc < 0)
		return rc;

	if ((rc = ct_tlv_parse(&tlv, &resp)) < 0)
		return rc;

	if (ct_tlv_get_int(&tlv, CT_TAG_LOCK, res) == 0)
		return IFD_ERROR_GENERIC;

	return 0;
}

int ct_card_unlock(ct_handle * h, unsigned int slot, ct_lock_handle lock)
{
	unsigned char buffer[256];
	ct_buf_t args, resp;

	ct_buf_init(&args, buffer, sizeof(buffer));
	ct_buf_init(&resp, buffer, sizeof(buffer));

	ct_buf_putc(&args, CT_CMD_UNLOCK);
	ct_buf_putc(&args, slot);

	ct_args_int(&args, CT_TAG_LOCK, lock);

	return ct_socket_call(h->sock, &args, &resp);
}

/*
 * Add arguments when calling a resource manager function
 */
static void ct_args_int(ct_buf_t * bp, ifd_tag_t tag, unsigned int value)
{
	ct_tlv_builder_t builder;

	ct_tlv_builder_init(&builder, bp, 1);
	ct_tlv_put_int(&builder, tag, value);
}

static void ct_args_string(ct_buf_t * bp, ifd_tag_t tag, const char *value)
{
	ct_tlv_builder_t builder;

	ct_tlv_builder_init(&builder, bp, 1);
	ct_tlv_put_string(&builder, tag, value);
}

static void ct_args_opaque(ct_buf_t * bp, ifd_tag_t tag,
			   const unsigned char *value, size_t len)
{
	ct_tlv_builder_t builder;

	ct_tlv_builder_init(&builder, bp, 1);
	ct_tlv_put_opaque(&builder, tag, value, len);
}
