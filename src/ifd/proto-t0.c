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

	unsigned int	max_nulls;
} t0_params_t;

#define t0_params(r)		((t0_params_t *) ((r)->proto_state))

enum {
	IDLE, SENDING, RECEIVING, CONFUSED
};

static int	t0_xcv(ifd_device_t *, t0_params_t *, ifd_apdu_t *);
static int	t0_send(ifd_device_t *, ifd_buf_t *, int);
static int	t0_recv(ifd_device_t *, ifd_buf_t *, int, long);
static int	t0_resynch(ifd_reader_t *);

/*
 * Set default T=1 protocol parameters
 */
static void
t0_set_defaults(t0_params_t *t0)
{
	t0->timeout = 1000;
	t0->state = IDLE;
	t0->max_nulls = 50;
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
	ifd_apdu_t	tpdu;
	unsigned char	sdata[5];
	unsigned int	cla, cse, lc, le;

	if (t0->state != IDLE) {
		if (t0_resynch(reader) < 0)
			return -1;
		t0->state = IDLE;
	}

	if (apdu->snd_len < 4 || apdu->rcv_len < 2)
		return -1;

	/* Copy APDU */
	tpdu = *apdu;

	/* Get class byte */
	cla = ((unsigned char *) apdu->rcv_buf)[0];

	/* Check the APDU case */
	cse = ifd_apdu_case(&tpdu, &lc, &le);

	switch (cse) {
	case IFD_APDU_CASE_1:
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

		if (t0_xcv(dev, t0, &tpdu) < 0)
			return -1;

		/* Can this happen? */
		if (tpdu.rcv_len != 2)
			return -1;

		/* Case 4 APDU - check whether we should
		 * try to get the response */
		if (cse == IFD_APDU_CASE_4S) {
			unsigned char	*sw;

			sw = (unsigned char *) tpdu.rcv_buf;

			sdata[0] = cla;
			sdata[1] = 0xC0;
			sdata[2] = 0x00;
			sdata[3] = 0x00;
			sdata[4] = le;

			if (sw[0] == 0x61) {
				/* additional length info */
				if (sw[1] != 0 && sw[1] < le)
					sdata[4] = sw[1];
			} else if ((sw[0] & 0xF0) == 0x60) {
				/* Command not accepted, do not
				 * retrieve response
				 */
				goto done;
			}

			/* Transmit a Get Response command */
			tpdu.snd_buf = sdata;
			tpdu.snd_len = 5;
		} else {
			goto done;
		}
	}

	t0->state = RECEIVING;
	tpdu.rcv_len = le + 2;
	if (t0_xcv(dev, t0, &tpdu) < 0)
		return -1;

done:	t0->state = IDLE;
	apdu->rcv_len = tpdu.rcv_len;
	return 0;
}

static int
t0_xcv(ifd_device_t *dev, t0_params_t *t0, ifd_apdu_t *apdu)
{
	ifd_buf_t	sbuf, rbuf;
	unsigned char	sdata[5];
	unsigned int	ins, lc, le;
	unsigned int	null_count = 0;

	/* Initialize send/recv buffers.
	 * The receive buffer is initialized to exactly the number of
	 * bytes we expect to receive (le + status word)
	 */
	if (apdu->snd_len <= 5) {
		 /* The APDU may be short (no Le), hence the need to
		  * clear the buffer */
		memset(sdata, 0, sizeof(sdata));
		memcpy(sdata, apdu->snd_buf, apdu->snd_len);

		/* Extract lc and le */
		lc = 0;
		le = sdata[4];

		/* Depending on the APDU case, an Le of 0 may
		 * mean 256 in fact.
		 */
		if (le == 0 && apdu->rcv_len >= 258)
			le = 256;

		t0->state = RECEIVING;
	} else {
		memcpy(sdata, apdu->snd_buf, 5);

		/* Fill send buffer */
		lc = apdu->snd_len - 5;
		le = 0;

		t0->state = SENDING;
	}

	/* Set up the send buffer */
	ifd_buf_init(&sbuf, apdu->snd_buf, lc);

	/* Set up the receive buffer for Le + status word */
	if (le + 2 > apdu->rcv_len) {
		ifd_error("%s: recv buffer too small (le=%u)",
				__FUNCTION__, le);
		t0->state = IDLE;
		return -1;
	}
	ifd_buf_init(&rbuf, apdu->rcv_buf, le + 2);

	/* Get the INS */
	ins = sdata[1];

	if (ifd_device_send(dev, sdata, 5) < 0)
		goto failed;

	while (1) {
		unsigned char	byte;
		int		count;

		if (ifd_device_recv(dev, &byte, 1, t0->timeout) < 0)
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
			 || t0_recv(dev, &rbuf, 1, t0->timeout) < 0)
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
			if (t0_send(dev, &sbuf, count) < 0)
				goto failed;
		} else {
			if (t0_recv(dev, &rbuf, count, t0->timeout) < 0)
				goto failed;
		}
	}

	apdu->rcv_len = ifd_buf_avail(&rbuf);
	return apdu->rcv_len;

failed:	t0->state = CONFUSED;
	return -1;
}

int
t0_send(ifd_device_t *dev, ifd_buf_t *bp, int count)
{
	int	n;

	if (count < 0)
		count = ifd_buf_avail(bp);
	n = ifd_device_send(dev, ifd_buf_head(bp), count);
	if (n >= 0)
		ifd_buf_get(bp, NULL, n);
	return n;
}

int
t0_recv(ifd_device_t *dev, ifd_buf_t *bp, int count, long timeout)
{
	int	n;

	if (count < 0)
		count = ifd_buf_tailroom(bp);
	n = ifd_device_recv(dev, ifd_buf_tail(bp), count, timeout);
	if (n >= 0)
		ifd_buf_put(bp, NULL, count);
	return n;
}

int
t0_resynch(ifd_reader_t *reader)
{
	return -1;
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

