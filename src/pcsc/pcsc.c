/*
 * PCSC frontend for libifd
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <string.h>
#include <openct/core.h>
#include <pcsclite.h>
#include <wintypes.h>
#include <ifdhandler.h>

#define lun_reader(x)	((x) >> 16)
#define lun_slot(x)	((x) & 0xffff)

static ifd_reader_t *	IFDH_lun_to_reader(DWORD, ifd_slot_t **);

/*
 * Since all driver management is performed by libifd,
 * we ignore the channel key here
 */
RESPONSECODE
IFDHCreateChannel(DWORD lun, DWORD channel)
{
	ifd_reader_t	*reader;

	if (!(reader = IFDH_lun_to_reader(lun, NULL))
	 || ifd_activate(reader) < 0)
		return IFD_COMMUNICATION_ERROR;
	return IFD_SUCCESS;
}

/*
 * Close the channel
 */
RESPONSECODE
IFDHCloseChannel(DWORD lun)
{
	ifd_reader_t	*reader;

	if (!(reader = IFDH_lun_to_reader(lun, NULL))
	 && ifd_deactivate(reader) < 0)
		return IFD_COMMUNICATION_ERROR;

	return IFD_SUCCESS;
}

/*
 * Get device capabilities. Oh these windows typedefs
 */
RESPONSECODE
IFDHGetCapabilities(DWORD lun, DWORD tag, PDWORD length, PUCHAR value)
{
	ifd_reader_t	*reader;
	ifd_slot_t	*slot;

	if (!(reader = IFDH_lun_to_reader(lun, &slot)))
		return IFD_ICC_NOT_PRESENT;

	switch (tag) {
	case TAG_IFD_ATR:
		if (slot->atr_len == 0)
			return IFD_ICC_NOT_PRESENT;
		if (*length > slot->atr_len)
			*length = slot->atr_len;
		memcpy(value, slot->atr, *length);
		break;

	case TAG_IFD_SLOTS_NUMBER:
	case TAG_IFD_SIMULTANEOUS_ACCESS:
		*length = 1;
		*value = reader->nslots;
		break;

	default:
		*length = 0;
		return IFD_ERROR_TAG;
	}

	return IFD_SUCCESS;
}

/*
 * Set driver capabilities
 */
RESPONSECODE
IFDHSetCapabilities (DWORD lun, DWORD tag, DWORD length, PUCHAR value)
{
	return IFD_NOT_SUPPORTED;
}

/*
 * Set protocol parameters (aka protocol type selection
 */
RESPONSECODE
IFDHSetProtocolParameters(DWORD lun, DWORD protocol,
			  UCHAR flags, UCHAR pts1, UCHAR pts2, UCHAR pts3)
{
	return IFD_NOT_SUPPORTED;
}

/*
 * Power up the ICC
 */
RESPONSECODE
IFDHPowerICC(DWORD lun, DWORD action, PUCHAR atr, PDWORD atr_len)
{
	ifd_reader_t	*reader;
	unsigned int	nslot;
	int		rc;

	if (!(reader = IFDH_lun_to_reader(lun, NULL)))
		return IFD_ICC_NOT_PRESENT;

	nslot = lun_slot(lun);

	switch (action) {
	case IFD_POWER_UP:
		rc = ifd_card_request(reader, nslot, 0, NULL, atr, *atr_len);
		if (rc < 0)
			return IFD_ICC_NOT_PRESENT;
		*atr_len = rc;
		break;

	case IFD_POWER_DOWN:
#if 0
		rc = ifd_card_eject(reader, nslot);
		if (rc < 0)
			return IFD_COMMUNICATION_ERROR;
#endif
		break;

	case IFD_RESET:
		rc = ifd_card_reset(reader, nslot, atr, *atr_len);
		if (rc < 0)
			return IFD_ICC_NOT_PRESENT;
		*atr_len = rc;
		break;

	default:
		return IFD_ERROR_POWER_ACTION;
	}

	return IFD_SUCCESS;
}

/*
 * Send APDU to card
 */
RESPONSECODE
IFDHTransmitToICC(DWORD lun, SCARD_IO_HEADER sendPci,
		  PUCHAR txBuffer, DWORD txLength,
		  PUCHAR rxBuffer, PDWORD rxLength,
		  PSCARD_IO_HEADER recvPci)
{
	ifd_reader_t	*reader;
	unsigned int	nslot;
	ifd_apdu_t	apdu;

	if (!(reader = IFDH_lun_to_reader(lun, NULL))) {
		*rxLength = 0;
		return IFD_ICC_NOT_PRESENT;
	}

	apdu.snd_buf = txBuffer;
	apdu.snd_len = txLength;
	apdu.rcv_buf = rxBuffer;
	apdu.rcv_len = *rxLength;

	nslot = lun_slot(lun);
	if (ifd_card_command(reader, nslot, &apdu) < 0) {
		*rxLength = 0;
		return IFD_COMMUNICATION_ERROR;
	}

	*rxLength = apdu.rcv_len;
	return IFD_SUCCESS;
}

/*
 * Client sends APDU to CT
 * It's not entirely clear what the average IFD handler is supposed
 * to do with this. The towitoko driver seems to interpret this
 * as a CTBCS message...
 * For now, we just drop it.
 */
RESPONSECODE
IFDHControl(DWORD lun, PUCHAR txBuffer, DWORD txLength,
		       PUCHAR rxBuffer, PDWORD rxLength)
{
	return IFD_NOT_SUPPORTED;
}

/*
 * Detect ICC presence
 */
RESPONSECODE
IFDHICCPresence(DWORD lun)
{
	ifd_reader_t	*reader;
	int		status;

	if (!(reader = IFDH_lun_to_reader(lun, NULL)))
		return IFD_ICC_NOT_PRESENT;

	if (ifd_card_status(reader, lun_slot(lun), &status) < 0)
		return IFD_COMMUNICATION_ERROR;

	return status? IFD_ICC_PRESENT : IFD_ICC_NOT_PRESENT;
}

/*
 * Map "lun" to reader and slot
 */
ifd_reader_t *
IFDH_lun_to_reader(DWORD lun, ifd_slot_t **slot)
{
	ifd_reader_t	*reader;
	unsigned int	nslot;

	if (!(reader = ifd_reader_by_index(lun_reader(lun))))
		return NULL;

	nslot = lun_slot(lun);
	if (nslot >= reader->nslots)
		return NULL;
	if (slot)
		*slot = reader->slot + nslot;
	return reader;
}
