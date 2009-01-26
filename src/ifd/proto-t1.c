/*
 * Implementation of T=1
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 *
 * improvements by:
 * Copyright (C) 2004 Ludovic Rousseau <ludovic.rousseau@free.fr>
 */

#include "internal.h"
#include <sys/poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	ifd_protocol_t base;
	int state;
	int block_oriented;

	unsigned char ns;
	unsigned char nr;
	unsigned int ifsc;
	unsigned int ifsd;

	unsigned int timeout, wtx;
	unsigned int retries;
	unsigned int rc_bytes;

	unsigned int (*checksum) (const unsigned char *,
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
#define T1_IS_ERROR(pcb)	((pcb) & 0x0F)
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

#define NAD 0
#define PCB 1
#define LEN 2
#define DATA 3

/* internal state, do not mess with it. */
/* should be != DEAD after reset/init */
enum {
	SENDING, RECEIVING, RESYNCH, DEAD
};

static void t1_set_checksum(t1_state_t *, int);
static unsigned int t1_block_type(unsigned char);
static unsigned int t1_seq(unsigned char);
static unsigned int t1_build(t1_state_t *, unsigned char *,
			     unsigned char, unsigned char,
			     ct_buf_t *, size_t *);
static unsigned int t1_compute_checksum(t1_state_t *, unsigned char *, size_t);
static int t1_verify_checksum(t1_state_t *, unsigned char *, size_t);
static int t1_xcv(t1_state_t *, unsigned char *, size_t, size_t);

/*
 * Set default T=1 protocol parameters
 */
static void t1_set_defaults(t1_state_t * t1)
{
	t1->retries = 3;
	/* This timeout is rather insane, but we need this right now
	 * to support cryptoflex keygen */
	t1->timeout = 20000;
	t1->ifsc = 32;
	t1->ifsd = 32;
	t1->nr = 0;
	t1->ns = 0;
	t1->wtx = 0;
}

static void t1_set_checksum(t1_state_t * t1, int csum)
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
static int t1_init(ifd_protocol_t * prot)
{
	t1_state_t *t1 = (t1_state_t *) prot;

	t1_set_defaults(t1);
	t1_set_checksum(t1, IFD_PROTOCOL_T1_CHECKSUM_LRC);

	/* If the device is attached through USB etc, assume the
	 * device will do the framing for us */
	if (prot->reader->device->type != IFD_DEVICE_TYPE_SERIAL)
		t1->block_oriented = 1;
	return 0;
}

/*
 * Detach t1 protocol
 */
static void t1_release(ifd_protocol_t * prot)
{
	/* NOP */
}

/*
 * Get/set parmaters for T1 protocol
 */
static int t1_set_param(ifd_protocol_t * prot, int type, long value)
{
	t1_state_t *t1 = (t1_state_t *) prot;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		t1->timeout = value;
		break;
	case IFD_PROTOCOL_BLOCK_ORIENTED:
		t1->block_oriented = value;
		break;
	case IFD_PROTOCOL_T1_CHECKSUM_LRC:
	case IFD_PROTOCOL_T1_CHECKSUM_CRC:
		t1_set_checksum(t1, type);
		break;
	case IFD_PROTOCOL_T1_IFSC:
		t1->ifsc = value;
		break;
	case IFD_PROTOCOL_T1_IFSD:
		t1->ifsd = value;
		break;
	default:
		ct_error("Unsupported parameter %d", type);
		return -1;
	}

	return 0;
}

static int t1_get_param(ifd_protocol_t * prot, int type, long *result)
{
	t1_state_t *t1 = (t1_state_t *) prot;
	long value;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		value = t1->timeout;
		break;
	case IFD_PROTOCOL_BLOCK_ORIENTED:
		value = t1->block_oriented;
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
static int t1_transceive(ifd_protocol_t * prot, int dad, const void *snd_buf,
			 size_t snd_len, void *rcv_buf, size_t rcv_len)
{
	t1_state_t *t1 = (t1_state_t *) prot;
	ct_buf_t sbuf, rbuf, tbuf;
	unsigned char sdata[T1_BUFFER_SIZE], sblk[5];
	unsigned int slen, retries, resyncs, sent_length = 0;
	size_t last_send = 0;

	if (snd_len == 0)
		return -1;

	/* we can't talk to a dead card / reader. Reset it! */
	if (t1->state == DEAD)
		return -1;

	t1->state = SENDING;
	retries = t1->retries;
	resyncs = 3;

	/* Initialize send/recv buffer */
	ct_buf_set(&sbuf, (void *)snd_buf, snd_len);
	ct_buf_init(&rbuf, rcv_buf, rcv_len);

	/* Send the first block */
	slen = t1_build(t1, sdata, dad, T1_I_BLOCK, &sbuf, &last_send);

	while (1) {
		unsigned char pcb;
		int n;

		retries--;

		if ((n = t1_xcv(t1, sdata, slen, sizeof(sdata))) < 0) {
			ifd_debug(1, "fatal: transmit/receive failed");
			t1->state = DEAD;
			goto error;
		}

		if (!t1_verify_checksum(t1, sdata, n)) {
			ifd_debug(1, "checksum failed");
			if (retries == 0 || sent_length)
				goto resync;
			slen = t1_build(t1, sdata,
					dad, T1_R_BLOCK | T1_EDC_ERROR,
					NULL, NULL);
			continue;
		}

		pcb = sdata[PCB];
		switch (t1_block_type(pcb)) {
		case T1_R_BLOCK:
			if (T1_IS_ERROR(pcb)) {
				ifd_debug(1, "received error block, err=%d",
					  T1_IS_ERROR(pcb));
				goto resync;
			}

			if (t1->state == RECEIVING) {
				slen = t1_build(t1, sdata,
						dad, T1_R_BLOCK, NULL, NULL);
				break;
			}

			/* If the card terminal requests the next
			 * sequence number, it received the previous
			 * block successfully */
			if (t1_seq(pcb) != t1->ns) {
				ct_buf_get(&sbuf, NULL, last_send);
				sent_length += last_send;
				last_send = 0;
				t1->ns ^= 1;
			}

			/* If there's no data available, the ICC
			 * shouldn't be asking for more */
			if (ct_buf_avail(&sbuf) == 0)
				goto resync;

			slen = t1_build(t1, sdata, dad, T1_I_BLOCK,
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
				slen = t1_build(t1, sdata, dad,
						T1_R_BLOCK | T1_OTHER_ERROR,
						NULL, NULL);
				continue;
			}

			t1->nr ^= 1;

			if (ct_buf_put(&rbuf, sdata + 3, sdata[LEN]) < 0)
				goto error;

			if ((pcb & T1_MORE_BLOCKS) == 0)
				goto done;

			slen = t1_build(t1, sdata, dad, T1_R_BLOCK, NULL, NULL);
			break;

		case T1_S_BLOCK:
			if (T1_S_IS_RESPONSE(pcb) && t1->state == RESYNCH) {
				t1->state = SENDING;
				sent_length = 0;
				last_send = 0;
				resyncs = 3;
				retries = t1->retries;
				ct_buf_init(&rbuf, rcv_buf, rcv_len);
				slen = t1_build(t1, sdata, dad, T1_I_BLOCK,
						&sbuf, &last_send);
				continue;
			}

			if (T1_S_IS_RESPONSE(pcb))
				goto resync;

			ct_buf_init(&tbuf, sblk, sizeof(sblk));

			switch (T1_S_TYPE(pcb)) {
			case T1_S_RESYNC:
				/* the card is not allowed to send a resync. */
				goto resync;
			case T1_S_ABORT:
				ifd_debug(1, "abort requested");
				break;
			case T1_S_IFS:
				ifd_debug(1, "CT sent S-block with ifs=%u",
					  sdata[DATA]);
				if (sdata[DATA] == 0)
					goto resync;
				t1->ifsc = sdata[DATA];
				ct_buf_putc(&tbuf, sdata[DATA]);
				break;
			case T1_S_WTX:
				/* We don't handle the wait time extension
				 * yet */
				ifd_debug(1, "CT sent S-block with wtx=%u",
					  sdata[DATA]);
				t1->wtx = sdata[DATA];
				ct_buf_putc(&tbuf, sdata[DATA]);
				break;
			default:
				ct_error("T=1: Unknown S block type 0x%02x",
					 T1_S_TYPE(pcb));
				goto resync;
			}

			slen = t1_build(t1, sdata, dad,
					T1_S_BLOCK | T1_S_RESPONSE |
					T1_S_TYPE(pcb), &tbuf, NULL);
		}

		/* Everything went just splendid */
		retries = t1->retries;
		continue;

	      resync:
		/* the number or resyncs is limited, too */
		if (resyncs == 0)
			goto error;
		resyncs--;
		t1->ns = 0;
		t1->nr = 0;
		slen = t1_build(t1, sdata, dad, T1_S_BLOCK | T1_S_RESYNC, NULL,
				NULL);
		t1->state = RESYNCH;
		continue;
	}

      done:
	return ct_buf_avail(&rbuf);

      error:
	t1->state = DEAD;
	return -1;
}

static int t1_resynchronize(ifd_protocol_t * p, int nad)
{
	t1_state_t *t1 = (t1_state_t *) p;
	unsigned char block[4];
	unsigned int retries = 3;

	if (p->reader && p->reader->device)
		ifd_device_flush(p->reader->device);

	while (retries--) {
		t1->ns = 0;
		t1->nr = 0;

		block[0] = nad;
		block[1] = T1_S_BLOCK | T1_S_RESYNC;
		block[2] = 0;
		t1_compute_checksum(t1, block, 3);

		if (t1_xcv(t1, block, 4, sizeof(block)) != 4) {
			ifd_debug(1, "fatal: transmit/receive failed");
			break;
		}

		if (!t1_verify_checksum(t1, block, 4)) {
			ifd_debug(1, "checksum failed");
			continue;
		}

		if (block[1] == (T1_S_BLOCK | T1_S_RESPONSE | T1_S_RESYNC))
			return 0;
	}

	t1->state = DEAD;
	return -1;
}

static unsigned t1_block_type(unsigned char pcb)
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

static unsigned int t1_seq(unsigned char pcb)
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

static unsigned int t1_build(t1_state_t * t1, unsigned char *block,
			     unsigned char dad, unsigned char pcb,
			     ct_buf_t * bp, size_t * lenp)
{
	unsigned int len;

	len = bp ? ct_buf_avail(bp) : 0;
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
 * Protocol struct
 */
struct ifd_protocol_ops ifd_protocol_t1 = {
	IFD_PROTOCOL_T1,	/* id */
	"T=1",			/* name */
	sizeof(t1_state_t),	/* size */
	t1_init,		/* init */
	t1_release,		/* release */
	t1_set_param,		/* set_param */
	t1_get_param,		/* get_param */
	t1_resynchronize,	/* resynchronize */
	t1_transceive,		/* transceive */
	NULL,			/* sync_read */
	NULL,			/* sync_write */
};

/*
 * Build/verify checksum
 */
static unsigned int t1_compute_checksum(t1_state_t * t1, unsigned char *data,
					size_t len)
{
	return len + t1->checksum(data, len, data + len);
}

static int t1_verify_checksum(t1_state_t * t1, unsigned char *rbuf, size_t len)
{
	unsigned char csum[2];
	int m, n;

	m = len - t1->rc_bytes;
	n = t1->rc_bytes;

	if (m < 0)
		return 0;

	t1->checksum(rbuf, m, csum);
	if (!memcmp(rbuf + m, csum, n))
		return 1;

	return 0;
}

/*
 * Send/receive block
 */
static int t1_xcv(t1_state_t * t1, unsigned char *block, size_t slen,
		  size_t rmax)
{
	ifd_protocol_t *prot = &t1->base;
	unsigned int rlen, timeout;
	int n, m;

	if (ct_config.debug >= 3)
		ifd_debug(3, "sending %s", ct_hexdump(block, slen));

	n = ifd_send_command(prot, block, slen);
	if (n < 0)
		return n;

	/* Maximum amount of data we'll receive - some devices
	 * such as the eToken need this. If you request more, it'll
	 * just barf */
	rlen = 3 + t1->ifsd + t1->rc_bytes;

	/* timeout. For now our WTX treatment is very dumb */
	timeout = t1->timeout + 1000 * t1->wtx;
	t1->wtx = 0;

	if (t1->block_oriented) {
		/* Note - Linux USB seems to have an off by one error, you
		 * actually need the + 1 to get the RC byte */
		rlen++;
		if (rlen < rmax)
			rmax = rlen;

		/* Get the response en bloc */
		n = ifd_recv_response(prot, block, rmax, timeout);
		if (n >= 0) {
			m = block[2] + 3 + t1->rc_bytes;
			if (m < n)
				n = m;
		}
	} else {
		/* Get the header */
		if (ifd_recv_response(prot, block, 3, timeout) < 0)
			return -1;

		n = block[2] + t1->rc_bytes;
		if (n + 3 > rmax || block[2] >= 254) {
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

int t1_negotiate_ifsd(ifd_protocol_t * proto, unsigned int dad, int ifsd)
{
	t1_state_t *t1 = (t1_state_t *) proto;
	ct_buf_t sbuf;
	unsigned char sdata[T1_BUFFER_SIZE];
	unsigned int slen;
	unsigned int retries;
	size_t snd_len;
	int n;
	unsigned char snd_buf[1], pcb;

	retries = t1->retries;

	/* S-block IFSD request */
	snd_buf[0] = ifsd;
	snd_len = 1;

	/* Initialize send/recv buffer */
	ct_buf_set(&sbuf, (void *)snd_buf, snd_len);

	while (1) {
		/* Build the block */
		slen =
		    t1_build(t1, sdata, dad, T1_S_BLOCK | T1_S_IFS, &sbuf,
			     NULL);

		if ((n = t1_xcv(t1, sdata, slen, sizeof(sdata))) < 0) {
			ifd_debug(1, "fatal: transmit/receive failed");
			t1->state = DEAD;
			goto error;
		}

		if (!t1_verify_checksum(t1, sdata, n)) {
			ifd_debug(1, "checksum failed");
			if (retries == 0)
				goto error;
			continue;
		}
		pcb = sdata[1];
		if (t1_block_type(pcb) == T1_S_BLOCK &&
		    T1_S_TYPE(pcb) == T1_S_IFS && T1_S_IS_RESPONSE(pcb)) {
			if (sdata[LEN] != 1 || sdata[DATA] != ifsd)
				goto error;
			break;
		}
		if (retries == 0)
			goto error;
	}

	return n;

      error:
	t1_resynchronize(proto, dad);
	return -1;
}
