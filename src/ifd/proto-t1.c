/*
 * Implementation of T=1
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <sys/time.h>
#include <sys/poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

typedef struct {
	ifd_protocol_t	base;
	int		state;

	unsigned char	ns;
	unsigned char	nr;
	unsigned int	ifsc;
	unsigned int	ifsd;

	unsigned int	timeout;
	unsigned int	retries;
	unsigned int	rc_bytes;

	unsigned int	(*checksum)(const unsigned char *,
					size_t, unsigned char *);
} t1_state_t;

/* T=1 protocol constants */
#define T1_I_BLOCK		0x00
#define T1_R_BLOCK		0x80
#define T1_S_BLOCK		0xC0
#define T1_MORE_BLOCKS		0x20

/* I block */
#define T1_I_SEQ_SHIFT		6

/* R block */
#define T1_IS_ERROR(pcb)	((pcb) & 0x1F)
#define T1_EDC_ERROR		0x01
#define T1_OTHER_ERROR		0x02
#define T1_R_SEQ_SHIFT		4

/* S block stuff */
#define T1_S_IS_RESPONSE(pcb)	((pcb) & T1_S_RESPONSE)
#define T1_S_TYPE(pcb)		((pcb) & 0x0F)
#define T1_S_RESPONSE		0x20
#define T1_S_RESYNC		0x00
#define T1_S_IFS		0x01
#define T1_S_ABORT		0x02
#define T1_S_WTX		0x03

#define T1_BUFFER_SIZE		(3 + 254 + 2)

enum {
	SENDING, RECEIVING, RESYNCH
};

static void		t1_set_checksum(t1_state_t *, int);
static unsigned	int	t1_block_type(unsigned char);
static unsigned int	t1_seq(unsigned char);
static int		t1_resynch(t1_state_t *t1);
static unsigned	int	t1_build(t1_state_t *, unsigned char *,
				unsigned char, unsigned char,
				ct_buf_t *, size_t *);
static unsigned int	t1_compute_checksum(t1_state_t *,
				unsigned char *, size_t);
static int		t1_verify_checksum(t1_state_t *, unsigned char *, unsigned int);
static int		t1_xcv(t1_state_t *, unsigned char *, size_t);

/*
 * Set default T=1 protocol parameters
 */
static void
t1_set_defaults(t1_state_t *t1)
{
	t1->retries  = 3;
	t1->timeout  = 3000;
	t1->ifsc     = 32;
	t1->ifsd     = 32;
	t1->nr	     = 0;
	t1->ns	     = 0;
}

void
t1_set_checksum(t1_state_t *t1, int csum)
{
	switch (csum) {
	case IFD_PROTOCOL_T1_CHECKSUM_LRC:
		t1->rc_bytes = 1;
		t1->checksum = csum_lrc_compute;
		break;
	case IFD_PROTOCOL_T1_CHECKSUM_CRC:
		t1->rc_bytes = 2;
		t1->checksum = csum_crc_compute;
		break;
	}
}

/*
 * Attach t1 protocol
 */
static int
t1_init(ifd_protocol_t *prot)
{
	t1_state_t	*t1 = (t1_state_t *) prot;

	t1_set_defaults(t1);
	t1_set_checksum(t1, IFD_PROTOCOL_T1_CHECKSUM_LRC);
	return 0;
}

/*
 * Detach t1 protocol
 */
static void
t1_release(ifd_protocol_t *prot)
{
	/* NOP */
}

/*
 * Get/set parmaters for T1 protocol
 */
static int
t1_set_param(ifd_protocol_t *prot, int type, long value)
{
	t1_state_t	*t1 = (t1_state_t *) prot;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		t1->timeout = value;
		break;
	case IFD_PROTOCOL_T1_RESYNCH:
		t1->state = RESYNCH;
		break;
	case IFD_PROTOCOL_T1_CHECKSUM_LRC:
	case IFD_PROTOCOL_T1_CHECKSUM_CRC:
		t1_set_checksum(t1, type);
		break;
	default:
		ct_error("Unsupported parameter %d", type);
		return -1;
	}

	return 0;
}

static int
t1_get_param(ifd_protocol_t *prot, int type, long *result)
{
	t1_state_t	*t1 = (t1_state_t *) prot;
	long value;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		value = t1->timeout;
		break;
	default:
		ct_error("Unsupported parameter %d", type);
		return -1;
	}

	if (result)
		*result = value;

	return 0;
}

/*
 * Send an APDU through T=1
 */
static int
t1_transceive(ifd_protocol_t *prot, int dad, ifd_apdu_t *apdu)
{
	t1_state_t	*t1 = (t1_state_t *) prot;
	ct_buf_t	sbuf, rbuf, tbuf;
	unsigned char	sdata[T1_BUFFER_SIZE];
	unsigned int	slen, retries, last_send = 0;

	if (apdu->snd_len == 0)
		return -1;

	/* Perform resynch if required */
	if (t1->state == RESYNCH && t1_resynch(t1) < 0)
		return -1;

	t1->state = SENDING;
	retries = t1->retries;

	/* Initialize send/recv buffer */
	ct_buf_set(&sbuf, apdu->snd_buf, apdu->snd_len);
	ct_buf_init(&rbuf, apdu->rcv_buf, apdu->rcv_len);

	/* Send the first block */
	slen = t1_build(t1, sdata, dad, T1_I_BLOCK, &sbuf, &last_send);

	while (1) {
		unsigned char	pcb;
		int		n;

		retries--;

		if ((n = t1_xcv(t1, sdata, slen)) < 0) {
			ifd_debug(1, "transmit/receive failed");
			if (retries == 0 || last_send)
				return -1;
			slen = t1_build(t1, sdata, dad,
					T1_R_BLOCK | T1_OTHER_ERROR,
					NULL, NULL);
			continue;
		}

		if (!t1_verify_checksum(t1, sdata, n)) {
			ifd_debug(1, "checksum failed");
			if (retries == 0 || last_send)
				return -1;
			slen = t1_build(t1, sdata,
					dad, T1_R_BLOCK | T1_EDC_ERROR,
					NULL, NULL);
			continue;
		}

		pcb = sdata[1];
		switch (t1_block_type(pcb)) {
		case T1_R_BLOCK:
			if (T1_IS_ERROR(pcb)) {
				if (retries == 0)
					return -1;
				if (t1->state == SENDING) {
					slen = t1_build(t1, sdata,
							dad, T1_I_BLOCK,
							&sbuf, &last_send);
					continue;
				}
			}

			if (t1->state == RECEIVING) {
				slen = t1_build(t1, sdata,
						dad, T1_R_BLOCK,
						NULL, NULL);
				break;
			}

			/* If the card terminal requests the next
			 * sequence number, it received the previous
			 * block successfully */
			if (t1_seq(pcb) != t1->ns) {
				ct_buf_get(&sbuf, NULL, last_send);
				last_send = 0;
				t1->ns ^= 1;
			}

			/* If there's no data available, the ICC
			 * shouldn't be asking for more */
			if (ct_buf_avail(&sbuf) == 0)
				return -1;

			slen = t1_build(t1, sdata,
					dad, T1_I_BLOCK,
					&sbuf, &last_send);
			break;

		case T1_I_BLOCK:
			/* The first I-block sent by the ICC indicates
			 * the last block we sent was received successfully. */
			if (t1->state == SENDING) {
				ct_buf_get(&sbuf, NULL, last_send);
				last_send = 0;
				t1->ns ^= 1;
			}

			t1->state = RECEIVING;

			/* If the block sent by the card doesn't match
			 * what we expected it to send, reply with
			 * an R block */
			if (t1_seq(pcb) != t1->nr) {
				slen = t1_build(t1, sdata,
						dad, T1_R_BLOCK | T1_OTHER_ERROR,
						NULL, NULL);
				continue;
			}

			t1->nr ^= 1;

			if (ct_buf_put(&rbuf, sdata + 3, sdata[2]) < 0)
				return -1;

			if ((pcb & T1_MORE_BLOCKS) == 0)
				goto done;

			slen = t1_build(t1, sdata, dad, T1_R_BLOCK, NULL, NULL);
			break;

		case T1_S_BLOCK:
			if (T1_S_IS_RESPONSE(pcb))
				return -1;

			ct_buf_init(&tbuf, sdata, sizeof(sdata));

			switch (T1_S_TYPE(pcb)) {
			case T1_S_RESYNC:
				ifd_debug(1, "resynch requested");
				if (retries == 0)
					return -1;
				t1_set_defaults(t1);
				break;
			case T1_S_ABORT:
				ifd_debug(1, "abort requested");
				return -1;
			case T1_S_IFS:
				if (sdata[3] == 0)
					return -1;
				t1->ifsc = sdata[3];
				ct_buf_put(&tbuf, &sdata[3], 1);
				break;
			case T1_S_WTX:
				/* We don't handle the wait time extension
				 * yet */
				/* t1->wtx = sdata[3]; */
				ct_buf_put(&tbuf, &sdata[3], 1);
				break;
			default:
				ct_error("T=1: Unknown S block type");
				return -1;
			}

			t1_build(t1, sdata, dad,
				T1_S_BLOCK | T1_S_RESPONSE | T1_S_TYPE(pcb),
				&tbuf, NULL);
		}

		/* Everything went just splendid */
		retries = t1->retries;
	}

done:	apdu->rcv_len = ct_buf_avail(&rbuf);
	return apdu->rcv_len;
}

static unsigned
t1_block_type(unsigned char pcb)
{
	switch (pcb & 0xC0) {
	case T1_R_BLOCK:
		return T1_R_BLOCK;
	case T1_S_BLOCK:
		return T1_S_BLOCK;
	default:
		return T1_I_BLOCK;
	}
}

static unsigned int
t1_seq(unsigned char pcb)
{
	switch (pcb & 0xC0) {
	case T1_R_BLOCK:
		return (pcb >> T1_R_SEQ_SHIFT) & 1;
	case T1_S_BLOCK:
		return 0;
	default:
		return (pcb >> T1_I_SEQ_SHIFT) & 1;
	}
}

unsigned int
t1_build(t1_state_t *t1, unsigned char *block,
		unsigned char dad, unsigned char pcb,
		ct_buf_t *bp, size_t *lenp)
{
	unsigned int	len;

	len = bp? ct_buf_avail(bp) : 0;
	if (len > t1->ifsc) {
		pcb |= T1_MORE_BLOCKS;
		len = t1->ifsc;
	}

	/* Add the sequence number */
	switch (t1_block_type(pcb)) {
	case T1_R_BLOCK:
		pcb |= t1->nr << T1_R_SEQ_SHIFT;
		break;
	case T1_I_BLOCK:
		pcb |= t1->ns << T1_I_SEQ_SHIFT;
		break;
	}

	block[0] = dad;
	block[1] = pcb;
	block[2] = len;

	if (len)
		memcpy(block + 3, ct_buf_head(bp), len);
	if (lenp)
		*lenp = len;

	return t1_compute_checksum(t1, block, len + 3);
}

/*
 * Resynchronize
 */
int
t1_resynch(t1_state_t *t1)
{
	unsigned char	block[T1_BUFFER_SIZE];
	unsigned int	n;

	t1_set_defaults(t1);

	n = t1_build(t1, block, 0x21, T1_S_BLOCK | T1_S_RESYNC, NULL, NULL);
	return t1_xcv(t1, block, n);
}

/*
 * Protocol struct
 */
struct ifd_protocol_ops	ifd_protocol_t1 = {
	IFD_PROTOCOL_T1,
	"T=1",
	sizeof(t1_state_t),
	t1_init,
	t1_release,
	t1_set_param,
	t1_get_param,
	t1_transceive,
};

/*
 * Build/verify checksum
 */
unsigned int
t1_compute_checksum(t1_state_t *t1, unsigned char *data, size_t len)
{
	return len + t1->checksum(data, len, data + len);
}

int
t1_verify_checksum(t1_state_t *t1, unsigned char *rbuf, size_t len)
{
	unsigned char	csum[2];
	int		m, n;

	m = len - t1->rc_bytes;
	n = t1->rc_bytes;

	if (m < 0)
		return 0;

	t1->checksum(rbuf, m, csum);
	if (!memcmp(rbuf + m, csum, n))
		return 1;

#if 0
	/* Some (e.g. eToken) will send a 0 checksum */
	if (rbuf[m] == 0 && (n == 1 || rbuf[m+1] == 0))
		return 1;
#endif

	return 0;
}

/*
 * Send/receive block
 */
int
t1_xcv(t1_state_t *t1, unsigned char *block, size_t slen)
{
	ifd_protocol_t	*prot = &t1->base;
	unsigned int	rlen;
	int		n, m;

	if (ct_config.debug >= 3)
		ifd_debug(3, "sending %s", ct_hexdump(block, slen));

	n = ifd_send_command(prot, block, slen);
	if (n < 0)
		return n;

	/* Maximum amount of data we'll receive - some devices
	 * such as the eToken need this. If you request more, it'll
	 * just barf */
	rlen = t1->ifsc + 5;

	if (prot->reader->device->type != IFD_DEVICE_TYPE_SERIAL) {
		/* Get the response en bloc */
		n = ifd_recv_response(prot, block, rlen, t1->timeout);
		if (n >= 0) {
			m = block[2] + 3 + t1->rc_bytes;
			if (m < n)
				n = m;
		}
	} else {
		/* Get the header */
		if (ifd_recv_response(prot, block, 3, t1->timeout) < 0)
			return -1;

		n = block[2] + t1->rc_bytes;
		if (n + 3 > T1_BUFFER_SIZE || block[2] >= 254) {
			ct_error("receive buffer too small");
			return -1;
		}

		/* Now get the rest */
		if (ifd_recv_response(prot, block + 3, n, t1->timeout) < 0)
			return -1;

		n += 3;
	}

	if (n >= 0 && ct_config.debug >= 3)
		ifd_debug(3, "received %s", ct_hexdump(block, n));

	return n;
}

