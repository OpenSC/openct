/*
 * Implementation of T=0
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <sys/time.h>
#include <sys/poll.h>
#include <unistd.h>
#include <stdlib.h>

#include "internal.h"

typedef struct t0_parms {
	int		state;
	long		timeout;
	unsigned int	wwt;		/* wait time */
} t0_params_t;

#define t0_params(r)		((t0_params_t *) ((r)->proto_state))

enum {
	IDLE, SENDING, RECEIVING, CONFUSED
};

static unsigned	int	t0_block_type(unsigned char);
static unsigned int	t0_seq(unsigned char);
static unsigned	int	t0_build(ifd_apdu_t *, t0_params_t *,
				unsigned char, ifd_buf_t *);
static void		t0_compute_checksum(t0_params_t *, ifd_apdu_t *);
static int		t0_verify_checksum(t0_params_t *, ifd_apdu_t *);
static int		t0_xcv(ifd_reader_t *, ifd_apdu_t *);

/*
 * Set default T=1 protocol parameters
 */
static void
t0_set_defaults(t0_params_t *t1)
{
	t0->state = IDLE;
}

/*
 * Attach t1 protocol
 */
static int
t0_attach(ifd_reader_t *reader)
{
	t0_params_t	*t0;

	t0 = (t0_params_t *) calloc(0, sizeof(*t0));
	if (t0 == NULL) {
		ifd_error("Out of memory");
		return -1;
	}

	t0_set_defaults(t0);

	reader->proto = &ifd_protocol_t0;
	reader->proto_state = t0;

	return 0;
}

/*
 * Detach t1 protocol
 */
static void
t0_detach(ifd_reader_t *reader)
{
	if (reader->proto_state)
		free(reader->proto_state);
	reader->proto_state = NULL;
	reader->proto = NULL;
}

/*
 * Get/set parmaters for T1 protocol
 */
static int
t0_set_param(ifd_reader_t *reader, int type, long value)
{
	t0_params_t	*p = (t0_params_t *) reader->proto_state;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		p->timeout = value;
		break;
	default:
		ifd_error("Unsupported parameter %d", type);
		return -1;
	}

	return 0;
}

static int
t0_get_param(ifd_reader_t *reader, int type, long *result)
{
	t0_params_t	*p = (t0_params_t *) reader->proto_state;
	long value;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		value = p->timeout;
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
 * Send an APDU through T=0
 */
static int
t0_transceive(ifd_reader_t *reader, unsigned char nad, ifd_apdu_t *apdu)
{
	ifd_device_t	*dev = reader->device;
	t0_params_t	*t0 = t0_params(reader);
	ifd_apdu_t	block;
	ifd_buf_t	sbuf, rbuf, tbuf;
	unsigned char	sdata[5], rdata[T0_BUFFER_SIZE];
	unsigned int	cla, ins, lc, le;
	long		timeout;

	if (t0->state != IDLE) {
		if (t0_resynch(reader) < 0)
			return -1;
		t0->state = IDLE;
	}

	if (apdu->snd_len < 4)
		return -1;

	/* Initialize send/recv buffer */
	ifd_buf_init(&sbuf, apdu->snd_buf, apdu->snd_len);
	ifd_buf_init(&rbuf, apdu->rcv_buf, apdu->rcv_len);

	/* Send APDU may be short (no Le) */
	if (apdu->snd_len < 5) {
		ifd_buf_init(&sbuf, sdata, 5);
		memcpy(sdata, apdu->snd_buf, 4);
		sdata[4] = 0;
	}

	if (ifd_buf_avail(&sbuf) == 5) {
		t0->state = RECEIVING;
	} else  {
		t0->state = SENDING;
	}

	/* Get the INS */
	ins = apdu->snd_buf[1];

	if (t0_send(dev, &sbuf, 5) < 0)
		goto failed;

	/* Per character timeout is WWT * etu, in milliseconds
	 * (etu is microseconds) */
	timeout = t0->wwt * dev->etu / 1000;

	while (1) {
		unsigned char	byte;
		int		n;

		if (ifd_device_recv(dev, &byte, 1, timeout) < 0)
			goto failed;

		/* ACK */
		if ((byte ^ ins) & 0xFE == 0) {
			if (t0->state == SENDING) {
				if (t0_send(dev, &sbuf, ifd_buf_avail(&sbuf)) < 0)
					goto failed;
			} else
		}

	}

done:	return 0;
}

static unsigned
t0_block_type(unsigned char pcb)
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
t0_seq(unsigned char pcb)
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
t0_build(ifd_apdu_t *apdu, t0_params_t *t1, unsigned char pcb, ifd_buf_t *bp)
{
	unsigned int	len;

	len = ifd_buf_avail(bp);
	if (len > t0->ifsc) {
		pcb |= T1_MORE_BLOCKS;
		len = t0->ifsc;
	}

	/* Add the sequence number */
	switch (t0_block_type(pcb)) {
	case T1_R_BLOCK:
		pcb |= t0->nr << T1_R_SEQ_SHIFT;
		break;
	case T1_I_BLOCK:
		pcb |= t0->ns << T1_I_SEQ_SHIFT;
		break;
	}

	apdu->snd_len = 3 + len;

	/* apdu->snd_buf[0] is already set to NAD */
	apdu->snd_buf[1] = pcb;
	apdu->snd_buf[2] = len;
	memcpy(apdu->snd_buf, bp->base + bp->head, len);

	t0_compute_checksum(t1, apdu);

	return len;
}

/*
 * Protocol struct
 */
ifd_protocol_t	ifd_protocol_t0 = {
	IFD_PROTOCOL_T0,
	"T=0",
	t0_attach,
	t0_detach,
	t0_set_param,
	t0_get_param,
	t0_transceive,
};

/*
 * Build/verify checksum
 */
void
t0_compute_checksum(t0_params_t *t1, ifd_apdu_t *apdu)
{
	apdu->snd_len += t0->checksum(apdu->snd_buf, apdu->snd_len,
				apdu->snd_buf + apdu->snd_len);
}

int
t0_verify_checksum(t0_params_t *t1, ifd_apdu_t *apdu)
{
	unsigned char	csum[2];
	int		m, n;

	m = apdu->rcv_len - t0->rc_bytes;
	n = t0->rc_bytes;

	if (m < 0)
		return 0;

	t0->checksum(apdu->rcv_buf, m, csum);
	return !memcmp(apdu->rcv_buf + m, csum, n);
}

/*
 * Send/receive block
 */
int
t0_xcv(ifd_reader_t *reader, ifd_apdu_t *apdu)
{
	ifd_device_t	*dev = reader->device;
	t0_params_t	*t1 = t0_params(reader);
	struct timeval	now, end;
	unsigned int	n;
	long		timeout;

	if (dev->ops->transceive)
		return ifd_device_transceive(dev, apdu, t0->timeout);

	/* We need to do it the hard way... */
	if (ifd_device_send(dev, apdu->snd_buf, apdu->snd_len) < 0)
		return -1;

	gettimeofday(&end, 0);
	end.tv_sec  += t0->timeout / 1000;
	end.tv_usec += t0->timeout % 1000;

	/* Get the header */
	if (ifd_device_recv(dev, apdu->rcv_buf, 3, t0->timeout) < 0)
		return -1;

	n = apdu->rcv_buf[2] + t0->rc_bytes;
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

