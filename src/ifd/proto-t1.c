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

typedef struct t1_apdu {
	unsigned char	nad;
	unsigned char	pcb;
	unsigned char	snd_len;
	unsigned char *	snd_buf;
	unsigned char	rcv_len;
	unsigned char *	rcv_buf;
} t1_apdu_t;

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
} t1_data_t;

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
	SENDING, RECEIVING
};

static unsigned	int	t1_block_type(unsigned char);
static unsigned int	t1_seq(unsigned char);
static unsigned	int	t1_build(ifd_apdu_t *, t1_data_t *,
				unsigned char, ifd_buf_t *);
static void		t1_compute_checksum(t1_data_t *, ifd_apdu_t *);
static int		t1_verify_checksum(t1_data_t *, ifd_apdu_t *);
static int		t1_xcv(t1_data_t *, ifd_apdu_t *);

/*
 * Set default T=1 protocol parameters
 */
static void
t1_set_defaults(t1_data_t *t1)
{
	t1->timeout = 3000;
	t1->ifsc    = 32;
	t1->ifsd    = 32;
	t1->nr	    = 0;
	t1->ns	    = 0;
}

/*
 * Attach t1 protocol
 */
static int
t1_init(ifd_protocol_t *prot)
{
	t1_data_t	*t1 = (t1_data_t *) prot;

	t1_set_defaults(t1);

	t1->rc_bytes = 2;
	t1->checksum = csum_crc_compute;

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
	t1_data_t	*t1 = (t1_data_t *) prot;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		t1->timeout = value;
		break;
	default:
		ifd_error("Unsupported parameter %d", type);
		return -1;
	}

	return 0;
}

static int
t1_get_param(ifd_protocol_t *prot, int type, long *result)
{
	t1_data_t	*t1 = (t1_data_t *) prot;
	long value;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		value = t1->timeout;
		break;
	default:
		ifd_error("Unsupported parameter %d", type);
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
t1_transceive(ifd_protocol_t *prot, unsigned char nad, ifd_apdu_t *apdu)
{
	t1_data_t	*t1 = (t1_data_t *) prot;
	ifd_apdu_t	block;
	ifd_buf_t	sbuf, rbuf, tbuf;
	unsigned char	sdata[T1_BUFFER_SIZE], rdata[T1_BUFFER_SIZE];
	unsigned int	retries, last_send = 0;

	if (apdu->snd_len == 0)
		return -1;

	t1->state = SENDING;
	retries = t1->retries;

	/* Initialize send/recv buffer */
	ifd_buf_init(&sbuf, apdu->snd_buf, apdu->snd_len);
	ifd_buf_init(&rbuf, apdu->rcv_buf, apdu->rcv_len);

	block.snd_buf = sdata;
	block.snd_len = 0;
	block.rcv_buf = rdata;
	block.rcv_len = sizeof(rdata);

	sdata[0] = nad;

	/* Send the first block */
	last_send = t1_build(&block, t1, T1_I_BLOCK, &sbuf);

	while (1) {
		unsigned char	pcb;
		int		n;

		retries--;

		if ((n = t1_xcv(t1, &block)) < 0) {
			if (retries == 0)
				return -1;
			t1_build(&block, t1, T1_R_BLOCK | T1_OTHER_ERROR, NULL);
			continue;
		}

		if (!t1_verify_checksum(t1, &block)) {
			if (retries == 0)
				return -1;
			t1_build(&block, t1, T1_R_BLOCK | T1_EDC_ERROR, NULL);
			continue;
		}

		pcb = rdata[1];
		switch (t1_block_type(pcb)) {
		case T1_R_BLOCK:
			if (T1_IS_ERROR(pcb) && retries == 0)
				return -1;

			if (t1->state == RECEIVING) {
				t1_build(&block, t1, T1_R_BLOCK, NULL);
				continue;
			}

			/* If the card terminal requests the next
			 * sequence number, it received the previous
			 * block successfully */
			if (t1_seq(pcb) != t1->ns) {
				ifd_buf_get(&sbuf, NULL, last_send);
				last_send = 0;
				t1->ns ^= 1;
			}

			/* If there's no data available, the ICC
			 * shouldn't be asking for more */
			if (ifd_buf_avail(&sbuf) == 0)
				return -1;

			last_send = t1_build(&block, t1, T1_I_BLOCK, &sbuf);
			break;

		case T1_I_BLOCK:
			/* The first I-block sent by the ICC indicates
			 * the last block we sent was received successfully. */
			if (t1->state == SENDING) {
				ifd_buf_get(&sbuf, NULL, last_send);
				last_send = 0;
				t1->ns ^= 1;
			}

			t1->state = RECEIVING;

			/* If the block sent by the card doesn't match
			 * what we expected it to send, reply with
			 * an R block */
			if (t1_seq(pcb) != t1->nr) {
				t1_build(&block, t1,
						T1_R_BLOCK | T1_OTHER_ERROR,
						NULL);
				continue;
			}

			t1->nr ^= 1;

			if (ifd_buf_put(&rbuf, rdata + 3, n) < 0)
				return -1;

			if ((pcb & T1_MORE_BLOCKS) == 0)
				goto done;

			t1_build(&block, t1, T1_R_BLOCK, NULL);
			break;

		case T1_S_BLOCK:
			if (T1_S_IS_RESPONSE(pcb))
				return -1;

			ifd_buf_init(&tbuf, rdata, sizeof(rdata));

			switch (T1_S_TYPE(pcb)) {
			case T1_S_RESYNC:
				t1_set_defaults(t1);
				break;
			case T1_S_ABORT:
				return -1;
			case T1_S_IFS:
				if (rdata[3] == 0)
					return -1;
				t1->ifsc = rdata[3];
				ifd_buf_put(&tbuf, &rdata[3], 1);
				break;
			case T1_S_WTX:
				/* We don't handle the wait time extension
				 * yet */
				/* t1->wtx = rdata[3]; */
				ifd_buf_put(&tbuf, &rdata[3], 1);
				break;
			default:
				ifd_error("T=1: Unknown S block type");
				return -1;
			}

			t1_build(&block, t1,
				T1_S_BLOCK | T1_S_RESPONSE | T1_S_TYPE(pcb),
				&tbuf);
		}

		/* Everything went just splendid */
		retries = t1->retries;
	}

done:	return 0;
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
t1_build(ifd_apdu_t *apdu, t1_data_t *t1, unsigned char pcb, ifd_buf_t *bp)
{
	unsigned int	len;

	len = ifd_buf_avail(bp);
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

	apdu->snd_len = 3 + len;

	/* apdu->snd_buf[0] is already set to NAD */
	apdu->snd_buf[1] = pcb;
	apdu->snd_buf[2] = len;
	memcpy(apdu->snd_buf, bp->base + bp->head, len);

	t1_compute_checksum(t1, apdu);

	return len;
}

/*
 * Protocol struct
 */
struct ifd_protocol_ops	ifd_protocol_t1 = {
	IFD_PROTOCOL_T1,
	"T=1",
	sizeof(t1_data_t),
	t1_init,
	t1_release,
	t1_set_param,
	t1_get_param,
	t1_transceive,
};

/*
 * Build/verify checksum
 */
void
t1_compute_checksum(t1_data_t *t1, ifd_apdu_t *apdu)
{
	apdu->snd_len += t1->checksum(apdu->snd_buf, apdu->snd_len,
				apdu->snd_buf + apdu->snd_len);
}

int
t1_verify_checksum(t1_data_t *t1, ifd_apdu_t *apdu)
{
	unsigned char	csum[2];
	int		m, n;

	m = apdu->rcv_len - t1->rc_bytes;
	n = t1->rc_bytes;

	if (m < 0)
		return 0;

	t1->checksum(apdu->rcv_buf, m, csum);
	return !memcmp(apdu->rcv_buf + m, csum, n);
}

/*
 * Send/receive block
 */
int
t1_xcv(t1_data_t *t1, ifd_apdu_t *apdu)
{
	ifd_device_t	*dev = t1->base.device;
	struct timeval	now, end;
	unsigned int	n;
	long		timeout;

	if (dev->ops->transceive)
		return ifd_device_transceive(dev, apdu, t1->timeout);

	/* We need to do it the hard way... */
	if (ifd_device_send(dev, apdu->snd_buf, apdu->snd_len) < 0)
		return -1;

	gettimeofday(&end, 0);
	end.tv_sec  += t1->timeout / 1000;
	end.tv_usec += t1->timeout % 1000;

	/* Get the header */
	if (ifd_device_recv(dev, apdu->rcv_buf, 3, t1->timeout) < 0)
		return -1;

	n = apdu->rcv_buf[2] + t1->rc_bytes;
	if (n + 3 > apdu->rcv_len || apdu->rcv_buf[2] >= 254) {
		ifd_error("receive buffer too small");
		return -1;
	}

	/* Now get the rest */
	gettimeofday(&now, 0);
	timeout = (end.tv_usec - now.tv_usec) / 1000
		+ (end.tv_sec - now.tv_sec) * 1000;
	if (timeout <= 0) {
		ifd_error("Timed out while talking to %s", dev->name);
		return -1;
	}

	if (ifd_device_recv(dev, apdu->rcv_buf + 3, n, timeout) < 0)
		return -1;

	return apdu->rcv_buf[2];
}

