/*
 * Implementation of T=0
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
	long timeout;
	unsigned int block_oriented;
	unsigned int max_nulls;
} t0_state_t;

enum {
	IDLE, SENDING, RECEIVING, CONFUSED
};

static int t0_xcv(ifd_protocol_t *, const void *, size_t, void *, size_t);
static int t0_send(ifd_protocol_t *, ct_buf_t *, int);
static int t0_recv(ifd_protocol_t *, ct_buf_t *, int, long);
static int t0_resynch(t0_state_t *);

/*
 * Set default T=1 protocol parameters
 */
static void t0_set_defaults(t0_state_t * t0)
{
	t0->state = IDLE;
	t0->timeout = 2000;
	t0->max_nulls = 800;
}

/*
 * Attach t0 protocol
 */
static int t0_init(ifd_protocol_t * prot)
{
	t0_set_defaults((t0_state_t *) prot);
	return 0;
}

/*
 * Detach t0 protocol
 */
static void t0_release(ifd_protocol_t * prot)
{
	/* NOP */
}

/*
 * Get/set parmaters for T1 protocol
 */
static int t0_set_param(ifd_protocol_t * prot, int type, long value)
{
	t0_state_t *t0 = (t0_state_t *) prot;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		t0->timeout = value;
		break;
	case IFD_PROTOCOL_BLOCK_ORIENTED:
		t0->block_oriented = value;
		break;
	default:
		ct_error("Unsupported parameter %d", type);
		return -1;
	}

	return 0;
}

static int t0_get_param(ifd_protocol_t * prot, int type, long *result)
{
	t0_state_t *t0 = (t0_state_t *) prot;
	long value;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		value = t0->timeout;
		break;
	case IFD_PROTOCOL_BLOCK_ORIENTED:
		value = t0->block_oriented;
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
 * Send an APDU through T=0
 */
static int t0_transceive(ifd_protocol_t * prot, int dad, const void *sbuf,
			 size_t slen, void *rbuf, size_t rlen)
{
	t0_state_t *t0 = (t0_state_t *) prot;
	ifd_iso_apdu_t iso;
	unsigned char sdata[5];
	unsigned int cla, cse, lc, le;
	int rc;

	if (t0->state != IDLE) {
		if (t0_resynch(t0) < 0)
			return -1;
		t0->state = IDLE;
	}

	if (slen < 4 || rlen < 2)
		return -1;

	/* Check the APDU case etc */
	if ((rc = ifd_iso_apdu_parse(sbuf, slen, &iso)) < 0)
		return rc;

	cse = iso.cse;
	cla = iso.cla;
	lc = iso.lc;
	le = iso.le;

	switch (cse) {
	case IFD_APDU_CASE_1:
		/* Include a NUL lc byte */
		memcpy(sdata, sbuf, 4);
		sdata[4] = 0;
		sbuf = sdata;
		slen = 5;
		break;
	case IFD_APDU_CASE_2S:
	case IFD_APDU_CASE_3S:
		break;
	case IFD_APDU_CASE_4S:
		/* Strip off the Le byte */
		slen--;
		break;
	default:
		/* We don't handle ext APDUs */
		return -1;
	}

	/*
	   if (le + 2 > slen) {
	   ct_error("t0_transceive: recv buffer too small");
	   return -1;
	   }
	 */

	if (lc) {
		t0->state = SENDING;
		if ((rc = t0_xcv(prot, sbuf, slen, rbuf, 2)) < 0)
			return rc;

		/* Can this happen? */
		if (rc != 2)
			return IFD_ERROR_COMM_ERROR;

		/* Case 4 APDU - check whether we should
		 * try to get the response */
		if (cse == IFD_APDU_CASE_4S) {
			unsigned char *sw;

			sw = (unsigned char *)rbuf;

			if (sw[0] == 0x61) {
				/* additional length info */
				if (sw[1] != 0 && sw[1] < le)
					le = sw[1];
			} else if ((sw[0] & 0xF0) == 0x60) {
				/* Command not accepted, do not
				 * retrieve response
				 */
				goto done;
			}

			/* Transmit a Get Response command */
			sdata[0] = cla;
			sdata[1] = 0xC0;
			sdata[2] = 0x00;
			sdata[3] = 0x00;
			sdata[4] = le;

			t0->state = RECEIVING;
			rc = t0_xcv(prot, sdata, 5, rbuf, le + 2);
		}
	} else {
		t0->state = RECEIVING;
		rc = t0_xcv(prot, sbuf, slen, rbuf, le + 2);
	}

      done:t0->state = IDLE;
	return rc;
}

static int t0_xcv(ifd_protocol_t * prot, const void *sdata, size_t slen,
		  void *rdata, size_t rlen)
{
	t0_state_t *t0 = (t0_state_t *) prot;
	ct_buf_t sbuf, rbuf;
	unsigned int null_count = 0;
	unsigned int ins;

	/* Let the driver handle any chunking etc */
	if (t0->block_oriented) {
		int rc;

		if ((rc = ifd_send_command(prot, sdata, slen)) >= 0)
			rc = ifd_recv_response(prot, rdata, rlen, t0->timeout);
		return rc;
	}

	/* Set up the send buffer */
	ct_buf_set(&sbuf, (void *)sdata, slen);
	ct_buf_init(&rbuf, rdata, rlen);

	/* Get the INS */
	ins = sbuf.base[1];

	if (t0_send(prot, &sbuf, 5) < 0)
		goto failed;

	while (1) {
		unsigned char byte;
		int count;

		if (ifd_recv_response(prot, &byte, 1, t0->timeout) < 0)
			goto failed;

		/* Null byte to extend wait time */
		if (byte == 0x60) {
			usleep(100000);
			if (++null_count > t0->max_nulls)
				goto failed;
			continue;
		}

		/* ICC sends SW1 SW2 */
		if ((byte & 0xF0) == 0x60 || (byte & 0xF0) == 0x90) {
			/* Store SW1, then get SW2 and store it */
			if (ct_buf_put(&rbuf, &byte, 1) < 0
			    || t0_recv(prot, &rbuf, 1, t0->timeout) < 0)
				goto failed;

			break;
		}

		/* Send/receive data.
		 * ACK byte means transfer everything in one go,
		 * ~ACK means do it octet by octet.
		 * SCEZ masks off using 0xFE, but the Towitoko
		 * driver uses 0x0E.
		 * Do we need to make this configurable?
		 */
		if (((byte ^ ins) & 0xFE) == 0) {
			/* Send/recv as much as we can */
			count = -1;
		} else if (((~byte ^ ins) & 0xFE) == 0) {
			count = 1;
		} else {
			ifd_debug(2, "unexpected byte 0x%02x", byte);
			return -1;
		}

		if (t0->state == SENDING) {
			if (t0_send(prot, &sbuf, count) < 0)
				goto failed;
		} else {
			if (t0_recv(prot, &rbuf, count, t0->timeout) < 0)
				goto failed;
			if (ct_buf_tailroom(&rbuf) == 0)
				break;
		}
	}

	return ct_buf_avail(&rbuf);

      failed:t0->state = CONFUSED;
	return -1;
}

static int t0_send(ifd_protocol_t * prot, ct_buf_t * bp, int count)
{
	int n, avail;

	avail = ct_buf_avail(bp);
	if (count < 0)
		count = avail;
	if (count > avail || !avail)
		return -1;
	n = ifd_send_command(prot, ct_buf_head(bp), count);
	if (n >= 0)
		ct_buf_get(bp, NULL, count);
	return n;
}

static int t0_recv(ifd_protocol_t * prot, ct_buf_t * bp, int count,
		   long timeout)
{
	int n;

	if (count < 0)
		count = ct_buf_tailroom(bp);
	n = ifd_recv_response(prot, ct_buf_tail(bp), count, timeout);
	if (n >= 0)
		ct_buf_put(bp, NULL, count);
	return n;
}

static int t0_resynch(t0_state_t * t0)
{
	return -1;
}

/*
 * Protocol struct
 */
struct ifd_protocol_ops ifd_protocol_t0 = {
	IFD_PROTOCOL_T0,	/* id */
	"T=0",			/* name */
	sizeof(t0_state_t),	/* size */
	t0_init,		/* init */
	t0_release,		/* release */
	t0_set_param,		/* set_param */
	t0_get_param,		/* get_param */
	NULL,			/* resynchronize */
	t0_transceive,		/* transceive */
	NULL,			/* sync_read */
	NULL,			/* sync_write */
};
