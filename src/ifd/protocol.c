/*
 * Protocol selection
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <string.h>
#include "internal.h"

struct ifd_protocol_info {
	struct ifd_protocol_info *next;
	struct ifd_protocol_ops	*ops;
};

static struct ifd_protocol_info	*list = NULL;

/*
 * Register a protocol
 */
void
ifd_protocol_register(struct ifd_protocol_ops *ops)
{
	struct ifd_protocol_info *info, **ptr;

	info = (struct ifd_protocol_info *) calloc(1, sizeof(*info));
	info->ops = ops;

	for (ptr = &list; *ptr; ptr = &(*ptr)->next)
		;
	*ptr = info;
}

/*
 * Look up protocol based on its ID
 */
static struct ifd_protocol_ops *
ifd_protocol_by_id(int id)
{
	struct ifd_protocol_info *info;

	for (info = list; info; info = info->next) {
		if (info->ops->id == id)
			return info->ops;
	}

	/* Autoload protocols defined in external modules? */

	return NULL;
}

/*
 * Select a protocol
 */
ifd_protocol_t *
ifd_protocol_select(ifd_reader_t *reader, int nslot, int preferred)
{
	const ifd_driver_t *drv;
	ifd_slot_t	*slot = &reader->slot[nslot];
	unsigned char	*atr, TDi;
	unsigned int	supported = 0;
	int		def_proto = -1, n, len;

	IFD_DEBUG("atr=%s", ifd_hexdump(slot->atr, slot->atr_len));

	atr = slot->atr;
	len = slot->atr_len;
	if (len < 2)
		return NULL;

	/* Ignore hysterical bytes */
	len -= atr[1] & 0x0f;

	n = 2;
	do {
		int	prot;

		TDi = atr[n - 1];
		if (n != 2) {
			prot = TDi & 0x0f;
			supported |= (1 << prot);
			if (def_proto < 0)
				def_proto = prot;
		}

		n += ifd_count_bits(TDi & 0xF0);
	} while (n < len && (TDi & 0x80));

	if (supported == 0)
		supported |= 0x01;
	if (def_proto < 0)
		def_proto = IFD_PROTOCOL_T0;

	IFD_DEBUG("default T=%d, supported protocols=0x%x",
			def_proto, supported);

	if (preferred >= 0
	 && preferred != def_proto
	 && (supported & (1 << preferred))) {
		/* XXX perform PTS */
		ifd_debug("protocol selection not supported");
	}

	if ((drv = reader->driver) && drv->ops && drv->ops->set_protocol) {
		if (drv->ops->set_protocol(reader, nslot, def_proto) < 0)
			return NULL;
	} else {
		slot->proto = ifd_protocol_new(def_proto, reader, slot->dad);
	}

	return slot->proto;
}

/*
 * Protocol transceive
 */
int
ifd_protocol_transceive(ifd_protocol_t *p, int dad, ifd_apdu_t *apdu)
{
	if (!p || !p->ops || !p->ops->transceive)
		return -1;

	IFD_DEBUG("cmd: %s", ifd_hexdump(apdu->snd_buf, apdu->snd_len));
	return p->ops->transceive(p, dad, apdu);
}

/*
 * Create new protocol object
 */
ifd_protocol_t *
ifd_protocol_new(int id, ifd_reader_t *reader, unsigned int dad)
{
	struct ifd_protocol_ops *ops;
	ifd_protocol_t *p;

	if (reader == NULL)
		return NULL;
	
	if (!(ops = ifd_protocol_by_id(id))) {
		ifd_error("unknown protocol id %d", id);
		return NULL;
	}

	p = (ifd_protocol_t *) calloc(1, ops->size);
	p->reader = reader;
	p->ops = ops;
	p->dad = dad;

	if (ops->init(p) < 0) {
		ifd_error("Protocol initialization failed");
		ifd_protocol_free(p);
		return NULL;
	}

	return p;
}

/*
 * Set a protocol specific parameter
 */
int
ifd_protocol_set_parameter(ifd_protocol_t *p, int type, long value)
{
	if (!p || !p->ops || !p->ops->set_param)
		return -1;
	return p->ops->set_param(p, type, value);
}

/*
 * Free protocol object
 */
void
ifd_protocol_free(ifd_protocol_t *p)
{
	if (p->ops) {
		if (p->ops->release)
			p->ops->release(p);
		memset(p, 0, p->ops->size);
	} else {
		memset(p, 0, sizeof(*p));
	}
	free(p);
}
