/*
 * Implementation of T=0
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
	long		timeout;
	unsigned int	max_nulls;
} t0_data_t;

enum {
	IDLE, SENDING, RECEIVING, CONFUSED
};

static int	t0_xcv(ifd_protocol_t *, ifd_apdu_t *);
static int	t0_send(ifd_protocol_t *, ifd_buf_t *, int);
static int	t0_recv(ifd_protocol_t *, ifd_buf_t *, int, long);
static int	t0_resynch(t0_data_t *);

/*
 * Set default T=1 protocol parameters
 */
static void
t0_set_defaults(t0_data_t *t0)
{
	t0->state = IDLE;
	t0->timeout = 1000;
	t0->max_nulls = 50;
}

/*
 * Attach t0 protocol
 */
static int
t0_init(ifd_protocol_t *prot)
{
	t0_set_defaults((t0_data_t *) prot);
	return 0;
}

/*
 * Detach t0 protocol
 */
static void
t0_release(ifd_protocol_t *prot)
{
	/* NOP */
}

/*
 * Get/set parmaters for T1 protocol
 */
static int
t0_set_param(ifd_protocol_t *prot, int type, long value)
{
	t0_data_t	*t0 = (t0_data_t *) prot;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		t0->timeout = value;
		break;
	default:
		ifd_error("Unsupported parameter %d", type);
		return -1;
	}

	return 0;
}

static int
t0_get_param(ifd_protocol_t *prot, int type, long *result)
{
	t0_data_t	*t0 = (t0_data_t *) prot;
	long		value;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		value = t0->timeout;
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
t0_transceive(ifd_protocol_t *prot, int dad, ifd_apdu_t *apdu)
{
	t0_data_t	*t0 = (t0_data_t *) prot;
	ifd_apdu_t	tpdu;
	unsigned char	sdata[5];
	unsigned int	cla, cse, lc, le;

	if (t0->state != IDLE) {
		if (t0_resynch(t0) < 0)
			return -1;
		t0->state = IDLE;
	}

	if (apdu->snd_len < 4 || apdu->rcv_len < 2)
		return -1;

	/* Copy APDU */
	tpdu = *apdu;

	/* Get class byte */
	cla = ((unsigned char *) tpdu.snd_buf)[0];

	/* Check the APDU case */
	cse = ifd_apdu_case(&tpdu, &lc, &le);

	switch (cse) {
	case IFD_APDU_CASE_1:
		/* Include a NUL lc byte */
		memcpy(sdata, tpdu.snd_buf, 4);
		sdata[4] = 0;
		tpdu.snd_buf = sdata;
		tpdu.snd_len = 5;
		break;
	case IFD_APDU_CASE_2S:
	case IFD_APDU_CASE_3S:
		break;
	case IFD_APDU_CASE_4S:
		/* Strip off the Le byte */
		tpdu.snd_len--;
		break;
	default:
		/* We don't handle ext APDUs */
		return -1;
	}

	if (le + 2 > tpdu.rcv_len) {
		ifd_error("t0_transceive: recv buffer too small");
		return -1;
	}

	if (lc) {
		t0->state = SENDING;
		tpdu.rcv_len = 2;

		if (t0_xcv(prot, &tpdu) < 0)
			return -1;

		/* Can this happen? */
		if (tpdu.rcv_len != 2)
			return -1;

		/* Case 4 APDU - check whether we should
		 * try to get the response */
		if (cse == IFD_APDU_CASE_4S) {
			unsigned char	*sw;

			sw = (unsigned char *) tpdu.rcv_buf;

			if (sw[0] == 0x61 && sw[1]) {
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
			tpdu.snd_buf = sdata;
			tpdu.snd_len = 5;
			tpdu.rcv_len = le + 2;

			sdata[0] = cla;
			sdata[1] = 0xC0;
			sdata[2] = 0x00;
			sdata[3] = 0x00;
			sdata[4] = le;
		} else {
			goto done;
		}
	}

	t0->state = RECEIVING;
	tpdu.rcv_len = le + 2;
	if (t0_xcv(prot, &tpdu) < 0)
		return -1;

done:	t0->state = IDLE;
	apdu->rcv_len = tpdu.rcv_len;
	return 0;
}

static int
t0_xcv(ifd_protocol_t *prot, ifd_apdu_t *apdu)
{
	t0_data_t	*t0 = (t0_data_t *) prot;
	ifd_buf_t	sbuf, rbuf;
	unsigned int	null_count = 0;
	unsigned int	ins;

	/* Set up the send buffer */
	ifd_buf_set(&sbuf, apdu->snd_buf, apdu->snd_len);
	ifd_buf_init(&rbuf, apdu->rcv_buf, apdu->rcv_len);

	/* Get the INS */
	ins = sbuf.base[1];

	if (t0_send(prot, &sbuf, 5) < 0)
		goto failed;

	while (1) {
		unsigned char	byte;
		int		count;

		if (ifd_recv_response(prot, &byte, 1, t0->timeout) < 0)
			goto failed;

		/* Null byte to extend wait time */
		if (byte == 0x60) {
			if (++null_count > t0->max_nulls)
				goto failed;
			continue;
		}

		/* ICC sends SW1 SW2 */
		if ((byte & 0xF0) == 0x60 || (byte & 0xF0) == 0x90) {
			/* Store SW1, then get SW2 and store it */
			if (!ifd_buf_put(&rbuf, &byte, 1)
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
			ifd_debug("%s: unexpected byte 0x%02x",
					__FUNCTION__, byte);
			return -1;
		}

		if (t0->state == SENDING) {
			if (t0_send(prot, &sbuf, count) < 0)
				goto failed;
		} else {
			if (t0_recv(prot, &rbuf, count, t0->timeout) < 0)
				goto failed;
			if (ifd_buf_tailroom(&rbuf) == 0)
				break;
		}
	}

	apdu->rcv_len = ifd_buf_avail(&rbuf);
	return apdu->rcv_len;

failed:	t0->state = CONFUSED;
	return -1;
}

int
t0_send(ifd_protocol_t *prot, ifd_buf_t *bp, int count)
{
	int	n, avail;

	avail = ifd_buf_avail(bp);
	if (count < 0)
		count = avail;
	if (count > avail || !avail)
		return -1;
	n = ifd_send_command(prot, ifd_buf_head(bp), count);
	if (n >= 0)
		ifd_buf_get(bp, NULL, count);
	return n;
}

int
t0_recv(ifd_protocol_t *prot, ifd_buf_t *bp, int count, long timeout)
{
	int	n;

	if (count < 0)
		count = ifd_buf_tailroom(bp);
	n = ifd_recv_response(prot, ifd_buf_tail(bp), count, timeout);
	if (n >= 0)
		ifd_buf_put(bp, NULL, count);
	return n;
}

int
t0_resynch(t0_data_t *t0)
{
	return -1;
}

/*
 * Protocol struct
 */
struct ifd_protocol_ops	ifd_protocol_t0 = {
	IFD_PROTOCOL_T0,
	"T=0",
	sizeof(t0_data_t),
	t0_init,
	t0_release,
	t0_set_param,
	t0_get_param,
	t0_transceive,
};

