/*
 * Implementation of the Gemplus Block Protocol.
 * This is a simpified version of T=1. The major
 * difference is that any command sends just *one*
 * block of data to the reader, and receives *one*
 * block only.
 *
 * Beware, entirely untested!
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
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

	unsigned int timeout, wtx;
	unsigned int retries;
} gbp_state_t;

#define GBP_I_BLOCK		0x00
#define GBP_R_BLOCK		0x80
#define GBP_S_BLOCK		0xC0
#define GBP_MORE_BLOCKS		0x20

/* I block */
#define GBP_I_SEQ_SHIFT		6

/* R block */
#define GBP_IS_ERROR(pcb)	((pcb) & 0x0F)
#define GBP_EDC_ERROR		0x01
#define GBP_OTHER_ERROR		0x02
#define GBP_R_SEQ_SHIFT		4

/* S block stuff */
#define GBP_S_IS_RESPONSE(pcb)	((pcb) & GBP_S_RESPONSE)
#define GBP_S_TYPE(pcb)		((pcb) & 0x0F)
#define GBP_S_RESPONSE		0x20
#define GBP_S_RESYNC		0x00
#define GBP_S_IFS		0x01
#define GBP_S_ABORT		0x02
#define GBP_S_WTX		0x03

#define GBP_BUFFER_SIZE		(3 + 256 + 2)

/* internal state, do not mess with it. */
/* should be != DEAD after reset/init */
enum {
	ALIVE, RESYNCH, DEAD
};

static unsigned int gbp_block_type(unsigned char);
static unsigned int gbp_seq(unsigned char);
static unsigned int gbp_build(gbp_state_t *, unsigned char *,
			      unsigned char, ct_buf_t *);
static unsigned int gbp_compute_checksum(gbp_state_t *,
					 unsigned char *, size_t);
static int gbp_verify_checksum(gbp_state_t *, unsigned char *, size_t);
static int gbp_xcv(gbp_state_t *, unsigned char *, size_t, size_t);

/*
 * Set default GBP protocol parameters
 */
static void gbp_set_defaults(gbp_state_t * gp)
{
	gp->retries = 3;
	/* This timeout is rather insane, but we need this right now
	 * to support cryptoflex keygen */
	gp->timeout = 20000;
	gp->ns = 0;
}

/*
 * Attach GBP protocol
 */
static int gbp_init(ifd_protocol_t * prot)
{
	gbp_state_t *gp = (gbp_state_t *) prot;

	gbp_set_defaults(gp);

	/* If the device is attached through USB etc, assume the
	 * device will do the framing for us */
	if (prot->reader->device->type != IFD_DEVICE_TYPE_SERIAL)
		gp->block_oriented = 1;
	return 0;
}

/*
 * Detach gp protocol
 */
static void gbp_release(ifd_protocol_t * prot)
{
	/* NOP */
}

/*
 * Get/set parmaters for T1 protocol
 */
static int gbp_set_param(ifd_protocol_t * prot, int type, long value)
{
	gbp_state_t *gp = (gbp_state_t *) prot;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		gp->timeout = value;
		break;
	case IFD_PROTOCOL_BLOCK_ORIENTED:
		gp->block_oriented = value;
		break;
	default:
		ct_error("Unsupported parameter %d", type);
		return -1;
	}

	return 0;
}

static int gbp_get_param(ifd_protocol_t * prot, int type, long *result)
{
	gbp_state_t *gp = (gbp_state_t *) prot;
	long value;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		value = gp->timeout;
		break;
	case IFD_PROTOCOL_BLOCK_ORIENTED:
		value = gp->block_oriented;
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
 * Send an APDU through GBP
 */
static int gbp_transceive(ifd_protocol_t * prot, int dad, const void *snd_buf,
			  size_t snd_len, void *rcv_buf, size_t rcv_len)
{
	gbp_state_t *gp = (gbp_state_t *) prot;
	ct_buf_t sbuf, rbuf;
	unsigned char sdata[GBP_BUFFER_SIZE];
	unsigned int slen, retries, resyncs;
	unsigned char send_seq;

	if (snd_len == 0 || snd_len > 255) {
		ct_error("GBP: invalid packet length %u\n", snd_len);
		return -1;
	}

	retries = gp->retries;
	resyncs = 3;

	/* Initialize send/recv buffer */
	ct_buf_set(&sbuf, (void *)snd_buf, snd_len);
	ct_buf_init(&rbuf, rcv_buf, rcv_len);

	/* Send the first block */
	slen = gbp_build(gp, sdata, GBP_I_BLOCK, &sbuf);

	send_seq = gp->ns;
	if (gp->state == DEAD) {
		gp->ns = 0;
		slen = gbp_build(gp, sdata, GBP_S_BLOCK | GBP_S_RESYNC, NULL);
		gp->state = RESYNCH;
	}

	while (1) {
		unsigned char pcb;
		int n;

		if (retries-- == 0)
			goto resync;

		if ((n = gbp_xcv(gp, sdata, slen, sizeof(sdata))) < 0) {
			ifd_debug(1, "fatal: transmit/receive failed");
			gp->state = DEAD;
			goto error;
		}

		if (!gbp_verify_checksum(gp, sdata, n)) {
			ifd_debug(1, "checksum failed");
			slen =
			    gbp_build(gp, sdata, GBP_R_BLOCK | GBP_EDC_ERROR,
				      NULL);
			continue;
		}

		pcb = sdata[1];
		switch (gbp_block_type(pcb)) {
		case GBP_I_BLOCK:
			/* I-Block means "Information" - this is the response from
			 * the card. */
			if (gbp_seq(pcb) != send_seq) {
				/* If the block sent by the card doesn't match
				 * what we expected it to send, reply with
				 * an R block */
				slen = gbp_build(gp, sdata,
						 GBP_R_BLOCK | GBP_OTHER_ERROR,
						 NULL);
				continue;
			}

			/* Advance to next seq nr */
			gp->ns ^= 1;

			if (ct_buf_put(&rbuf, sdata + 3, sdata[2]) < 0)
				goto error;
			goto done;

		case GBP_R_BLOCK:
			/* R-Block means "Repeat" */
			if (gbp_seq(pcb) != gp->ns) {
				slen = gbp_build(gp, sdata,
						 GBP_R_BLOCK | GBP_OTHER_ERROR,
						 NULL);
				continue;
			}

			ifd_debug(1, "received R block%s%s",
				  (pcb & GBP_EDC_ERROR) ? ", EDC error" : "",
				  (pcb & GBP_OTHER_ERROR) ? ", other error" :
				  "");

			/* Retransmit block */
			slen = gbp_build(gp, sdata, GBP_I_BLOCK, &sbuf);
			break;

		case GBP_S_BLOCK:
			if (GBP_S_IS_RESPONSE(pcb) && gp->state == RESYNCH) {
				gp->state = ALIVE;
				resyncs = 3;
				retries = gp->retries;
				slen = gbp_build(gp, sdata, GBP_I_BLOCK, &sbuf);
				continue;
			}

			ifd_debug(1, "unexpected S block from reader\n");
			goto resync;
		}

		/* Everything went just splendid */
		retries = gp->retries;
		continue;

	      resync:
		/* the number or resyncs is limited, too */
		if (resyncs == 0)
			goto error;
		resyncs--;
		gp->ns = 0;
		slen = gbp_build(gp, sdata, GBP_S_BLOCK | GBP_S_RESYNC, NULL);
		gp->state = RESYNCH;
		continue;
	}

      done:
	return ct_buf_avail(&rbuf);

      error:
	gp->state = DEAD;
	return -1;
}

static int gbp_resynchronize(ifd_protocol_t * p, int nad)
{
	gbp_state_t *gp = (gbp_state_t *) p;
	unsigned char block[4];
	unsigned int retries = 3;

	if (p->reader && p->reader->device)
		ifd_device_flush(p->reader->device);

	while (retries--) {
		gp->ns = 0;

		block[0] = nad;
		block[1] = GBP_S_BLOCK | GBP_S_RESYNC;
		block[2] = 0;
		gbp_compute_checksum(gp, block, 3);

		if (gbp_xcv(gp, block, 4, sizeof(block)) != 4) {
			ifd_debug(1, "fatal: transmit/receive failed");
			break;
		}

		if (!gbp_verify_checksum(gp, block, 4)) {
			ifd_debug(1, "checksum failed");
			continue;
		}

		if (block[1] == (GBP_S_BLOCK | GBP_S_RESPONSE | GBP_S_RESYNC)) {
			gp->state = ALIVE;
			return 0;
		}
	}

	gp->state = DEAD;
	return -1;
}

static unsigned gbp_block_type(unsigned char pcb)
{
	switch (pcb & 0xC0) {
	case GBP_R_BLOCK:
		return GBP_R_BLOCK;
	case GBP_S_BLOCK:
		return GBP_S_BLOCK;
	default:
		return GBP_I_BLOCK;
	}
}

static unsigned int gbp_seq(unsigned char pcb)
{
	switch (pcb & 0xC0) {
	case GBP_R_BLOCK:
		return (pcb >> GBP_R_SEQ_SHIFT) & 1;
	case GBP_S_BLOCK:
		return 0;
	default:
		return (pcb >> GBP_I_SEQ_SHIFT) & 1;
	}
}

static unsigned int gbp_build(gbp_state_t * gp, unsigned char *block,
			      unsigned char pcb, ct_buf_t * bp)
{
	unsigned int len;

	len = bp ? ct_buf_avail(bp) : 0;

	/* Add the sequence number */
	switch (gbp_block_type(pcb)) {
	case GBP_R_BLOCK:
		pcb |= gp->ns << GBP_R_SEQ_SHIFT;
		break;
	case GBP_I_BLOCK:
		pcb |= gp->ns << GBP_I_SEQ_SHIFT;
		break;
	}

	block[0] = 0x42;
	block[1] = pcb;
	block[2] = len;

	if (len)
		memcpy(block + 3, ct_buf_head(bp), len);

	return gbp_compute_checksum(gp, block, len + 3);
}

/*
 * Protocol struct
 */
struct ifd_protocol_ops ifd_protocol_gbp = {
	IFD_PROTOCOL_GBP,	/* id */
	"GBP",			/* name */
	sizeof(gbp_state_t),	/* size */
	gbp_init,		/* init */
	gbp_release,		/* release */
	gbp_set_param,		/* set_param */
	gbp_get_param,		/* get_param */
	gbp_resynchronize,	/* resynchronize */
	gbp_transceive,		/* transceive */
	NULL,			/* sync_read */
	NULL,			/* sync_write */
};

/*
 * Build/verify checksum
 */
static unsigned int gbp_compute_checksum(gbp_state_t * gp, unsigned char *data,
					 size_t len)
{
	csum_lrc_compute(data, len, data + len);
	return len + 1;
}

static int gbp_verify_checksum(gbp_state_t * gp, unsigned char *rbuf,
			       size_t len)
{
	unsigned char csum;

	csum_lrc_compute(rbuf, len, &csum);
	return csum == 0;
}

/*
 * Send/receive block
 */
static int gbp_xcv(gbp_state_t * gp, unsigned char *block, size_t slen,
		   size_t rmax)
{
	ifd_protocol_t *prot = &gp->base;
	ifd_device_t *dev = prot->reader->device;
	unsigned int rlen, timeout;
	int n, m;

	if (ct_config.debug >= 3)
		ifd_debug(3, "sending %s", ct_hexdump(block, slen));

	n = ifd_device_send(dev, block, slen);
	if (n < 0)
		return n;

	rlen = 3 + 256 + 1;

	/* timeout. For now our WTX treatment is very dumb */
	timeout = gp->timeout + 1000 * gp->wtx;
	gp->wtx = 0;

	if (gp->block_oriented) {
		/* Note - Linux USB seems to have an off by one error, you
		 * actually need the + 1 to get the RC byte */
		rlen++;
		if (rlen < rmax)
			rmax = rlen;

		/* Get the response en bloc */
		n = ifd_device_recv(dev, block, rmax, timeout);
		if (n >= 0) {
			m = block[2] + 3 + 1;
			if (m < n)
				n = m;
		}
	} else {
		/* Get the header */
		if (ifd_device_recv(dev, block, 3, timeout) < 0)
			return -1;

		n = block[2] + 1;
		if (n + 3 > rmax || block[2] >= 254) {
			ct_error("receive buffer too small");
			return -1;
		}

		/* Now get the rest */
		if (ifd_device_recv(dev, block + 3, n, gp->timeout) < 0)
			return -1;

		n += 3;
	}

	if (n >= 0 && ct_config.debug >= 3)
		ifd_debug(3, "received %s", ct_hexdump(block, n));

	return n;
}
