/*
 * Protocol selection
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>

struct ifd_protocol_info {
	struct ifd_protocol_info *next;
	struct ifd_protocol_ops *ops;
};

static struct ifd_protocol_info *list = NULL;

/*
 * Register a protocol
 */
int ifd_protocol_register(struct ifd_protocol_ops *ops)
{
	struct ifd_protocol_info *info, **ptr;

	info = (struct ifd_protocol_info *)calloc(1, sizeof(*info));
	if (!info) {
		ct_error("out of memory");
		return IFD_ERROR_NO_MEMORY;
	}
	info->ops = ops;

	for (ptr = &list; *ptr; ptr = &(*ptr)->next) ;
	*ptr = info;
	return 0;
}

/*
 * Look up protocol based on its ID
 */
static struct ifd_protocol_ops *ifd_protocol_by_id(int id)
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
ifd_protocol_t *ifd_protocol_select(ifd_reader_t * reader, int nslot,
				    int preferred)
{
	const ifd_driver_t *drv;
	ifd_slot_t *slot = &reader->slot[nslot];
	unsigned char *atr, TDi;
	unsigned int supported = 0;
	int def_proto = -1, n, len;

	ifd_debug(1, "atr=%s", ct_hexdump(slot->atr, slot->atr_len));

	/* FIXME: use ifd_atr_parse() instead */
	atr = slot->atr;
	len = slot->atr_len;
	if (len < 2)
		return NULL;

	/* Ignore hysterical bytes */
	len -= atr[1] & 0x0f;

	n = 2;
	do {
		int prot;

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

	ifd_debug(1, "default T=%d, supported protocols=0x%x",
		  def_proto, supported);

	if (preferred >= 0
	    && preferred != def_proto && (supported & (1 << preferred))) {
		/* XXX perform PTS */
		ifd_debug(1, "protocol selection not supported");
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
 * Force the protocol driver to resynchronize
 */
int ifd_protocol_resynchronize(ifd_protocol_t * p, int nad)
{
	ifd_debug(1, "called.");
	if (!p || !p->ops || !p->ops->resynchronize)
		return IFD_ERROR_NOT_SUPPORTED;

	return p->ops->resynchronize(p, nad);
}

/*
 * Protocol transceive
 */
int ifd_protocol_transceive(ifd_protocol_t * p, int dad, const void *sbuf,
			    size_t slen, void *rbuf, size_t rlen)
{
	int rc;

	if (!p || !p->ops || !p->ops->transceive)
		return IFD_ERROR_NOT_SUPPORTED;

	ifd_debug(1, "cmd: %s", ct_hexdump(sbuf, slen));
	rc = p->ops->transceive(p, dad, sbuf, slen, rbuf, rlen);

	if (rc >= 0)
		ifd_debug(1, "resp:%s", ct_hexdump(rbuf, rc));
	else
		ifd_debug(1, "transceive error: %s", ct_strerror(rc));

	return rc;
}

/*
 * Read/write synchronous ICCs
 */
int ifd_protocol_read_memory(ifd_protocol_t * p, int slot, unsigned short addr,
			     unsigned char *rbuf, size_t rlen)
{
	int rc;

	if (!p || !p->ops || !p->ops->sync_read)
		return IFD_ERROR_NOT_SUPPORTED;

	ifd_debug(1, "read %u@%04x (%s)", (unsigned int)rlen, addr,
		  p->ops->name);
	rc = p->ops->sync_read(p, slot, addr, rbuf, rlen);

	if (rc >= 0)
		ifd_debug(1, "resp:%s", ct_hexdump(rbuf, rc));

	return rc;
}

int ifd_protocol_write_memory(ifd_protocol_t * p, int slot, unsigned short addr,
			      const unsigned char *sbuf, size_t slen)
{
	int rc;

	if (!p || !p->ops || !p->ops->sync_write)
		return IFD_ERROR_NOT_SUPPORTED;

	ifd_debug(1, "write %u@%04x (%s):%s",
		  (unsigned int)slen, addr,
		  p->ops->name, ct_hexdump(sbuf, slen));
	rc = p->ops->sync_write(p, slot, addr, sbuf, slen);

	ifd_debug(1, "resp = %d", rc);
	return rc;
}

/*
 * Create new protocol object
 */
ifd_protocol_t *ifd_protocol_new(int id, ifd_reader_t * reader,
				 unsigned int dad)
{
	struct ifd_protocol_ops *ops;
	ifd_protocol_t *p;

	if (reader == NULL)
		return NULL;

	if (!(ops = ifd_protocol_by_id(id))) {
		ct_error("unknown protocol id %d", id);
		return NULL;
	}

	p = (ifd_protocol_t *) calloc(1, ops->size);
	if (!p) {
		ct_error("out of memory");
		return p;
	}
	p->reader = reader;
	p->ops = ops;
	p->dad = dad;

	if (ops->init && ops->init(p) < 0) {
		ct_error("Protocol initialization failed");
		ifd_protocol_free(p);
		return NULL;
	}

	return p;
}

/*
 * Set a protocol specific parameter
 */
int ifd_protocol_set_parameter(ifd_protocol_t * p, int type, long value)
{
	if (!p || !p->ops || !p->ops->set_param)
		return -1;
	return p->ops->set_param(p, type, value);
}

int ifd_protocol_get_parameter(ifd_protocol_t * p, int type, long *value)
{
	if (!p || !p->ops || !p->ops->get_param)
		return -1;
	return p->ops->get_param(p, type, value);
}

/*
 * Free protocol object
 */
void ifd_protocol_free(ifd_protocol_t * p)
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

/*
 * List available protocols
 */
unsigned int ifd_protocols_list(const char **names, unsigned int max)
{
	struct ifd_protocol_info *info;
	unsigned int n;

	for (info = list, n = 0; info && n < max; info = info->next, n++) {
		names[n] = info->ops->name;
	}
	return n;
}
