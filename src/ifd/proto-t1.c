/*
 * Implementation of T=1
 */

#include <sys/time.h>
#include <sys/poll.h>
#include <unistd.h>
#include <stdlib.h>

#include "internal.h"

struct t1_apdu {
	unsigned char	nad;
	unsigned char	pcb;
	unsigned char	snd_len;
	unsigned char *	snd_buf;
	unsigned char	rcv_len;
	unsigned char *	rcv_buf;
};

struct t1_parms {
	unsigned char	ns;

	unsigned int	recv_timeout;

	unsigned int	rc_bytes;

	unsigned int	(*rc)(const unsigned char *, size_t, unsigned char *);
	int		(*send)(ifd_reader_t *, const unsigned char *, size_t);
	int		(*recv)(ifd_reader_t *, unsigned char *, size_t);
};

static int	t1_chrdev_send(ifd_reader_t *, const unsigned char *, size_t);
static int	t1_chrdev_recv(ifd_reader_t *, unsigned char *, size_t);


/*
 * Helper functions
 */
static inline struct t1_parms *
t1_parms(ifd_reader_t *reader)
{
	return (struct t1_parms *) reader->proto_state;
}

static long
t1_timeout(ifd_reader_t *reader)
{
	return t1_parms(reader)->recv_timeout;
}

static unsigned int
t1_rc_bytes(ifd_reader_t *reader)
{
	return t1_parms(reader)->rc_bytes;
}

/*
 * Attach t1 protocol
 */
static int
t1_attach(ifd_reader_t *reader)
{
	struct t1_parms *parms;

	parms = (struct t1_parms *) calloc(0, sizeof(*parms));
	if (parms == NULL) {
		ifd_error("Out of memory");
		return -1;
	}

	parms->recv_timeout = 3000;

	parms->rc_bytes = 2;
	parms->rc = csum_crc_compute;

	if (reader->device->flags & IFD_DEVICE_BLKIO) {
		//parms->send = t1_blkdev_send;
		//parms->recv = t1_blkdev_recv;
	} else {
		parms->send = t1_chrdev_send;
		parms->recv = t1_chrdev_recv;
	}

	reader->proto = &t1_protocol;
	reader->proto_state = parms;

	return 0;
}

/*
 * Detach t1 protocol
 */
static void
t1_detach(ifd_reader_t *reader)
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
t1_set_param(ifd_reader_t *reader, int type, long value)
{
	struct t1_parms *p = (struct t1_parms *) reader->proto_state;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		p->recv_timeout = value;
		break;
	default:
		ifd_error("Unsupported parameter %d", type);
		return -1;
	}

	return 0;
}

static int
t1_get_param(ifd_reader_t *reader, int type, long *result)
{
	struct t1_parms *p = (struct t1_parms *) reader->proto_state;
	long value;

	switch (type) {
	case IFD_PROTOCOL_RECV_TIMEOUT:
		value = p->recv_timeout;
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
t1_send(ifd_reader_t *reader, ifd_apdu_t *apdu)
{
}

/*
 * Protocol struct
 */
ifd_protocol_t	t1_protocol = {
	IFD_PROTOCOL_T1,
	"T=1",
	t1_attach,
	t1_detach,
	t1_set_param,
	t1_get_param,
	t1_send,
};

/*
 * Send/receive block for character oriented devices
 * such as serials ports
 */
int
t1_chrdev_send(ifd_reader_t *reader, const unsigned char *buf, size_t len)
{
	ifd_device_t *dev = reader->device;
	int	n;

	while (len) {
		n = ifd_device_send(dev, buf, len);
		if (n < 0) {
			ifd_error("Error while talking to %s: %m", dev->name);
			return -1;
		}
		if (n == 0) {
			ifd_error("Device %s disconnected", dev->name);
			return -1;
		}

		buf += n;
		len -= n;
	}

	return 0;
}

int
t1_chrdev_recv(ifd_reader_t *reader, unsigned char *buf, size_t size)
{
	ifd_device_t	*dev = reader->device;
	struct timeval	now, end;
	unsigned int	have, want;
	int		n;
	long		timeout;

	timeout = t1_timeout(reader);
	gettimeofday(&end, 0);
	end.tv_sec  += timeout / 1000;
	end.tv_usec += timeout % 1000;

	want = 3;
	have = 0;
	while (want) {
		gettimeofday(&now, 0);
		timeout = (end.tv_usec - now.tv_usec) / 1000
		        + (end.tv_sec - now.tv_sec) * 1000;

		if (timeout <= 0) {
			ifd_error("Timed out while talking to %s", dev->name);
			return -1;
		}

		if (have + want > size) {
			ifd_error("Buffer too small for reply");
			return -1;
		}

		n = ifd_device_recv(dev, buf + have, want, timeout);
		if (n < 0) {
			ifd_error("Error while talking to %s: %m", dev->name);
			return -1;
		}
		if (n == 0) {
			ifd_error("Device %s disconnected", dev->name);
			return -1;
		}

		have += n;
		want -= n;

		/* need to read apdu itself + checksum */
		if (want == 0 && have == 3)
			want = buf[2] + t1_rc_bytes(reader);
	}

	return have;
}

