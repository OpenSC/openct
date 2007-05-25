/*
 * PC/SC Lite IFD front-end for libopenctapi
 *
 * Mapping of CT-API / CT-BCS interface to the IFD Handler 2.0.
 * Getting/Setting IFD/Protocol/ICC parameters other than the ATR is not
 * supported. IFDH_MAX_READERS simultaneous readers are supported.
 *
 * This file was taken and modified from the Unix driver for
 * Towitoko smart card readers. Used and re-licensed as BSD with
 * a permission from the author.
 *
 * Copyright (C) 1998-2001, Carlos Prados <cprados@yahoo.com>
 * Copyright (C) 2003, Antti Tapaninen <aet@cc.hut.fi>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif
#ifdef DEBUG_IFDH
#include <syslog.h>
#endif
#ifdef __APPLE__
#include <PCSC/wintypes.h>
#include <PCSC/pcsclite.h>
#else
#include <wintypes.h>
#include <pcsclite.h>
#endif
#include <openct/openct.h>
#include "ctapi.h"		/* XXX: <openct/ctapi.h>? */
#define IFDHANDLERv2
#include "ifdhandler.h"

/* Maximum number of readers handled */
#define IFDH_MAX_READERS	OPENCT_MAX_READERS

/* Maximum number of slots per reader handled */
#define IFDH_MAX_SLOTS		1	/* XXX: OPENCT_MAX_SLOTS? */

typedef struct {
	DEVICE_CAPABILITIES device_capabilities;
	ICC_STATE icc_state;
	DWORD ATR_Length;
	PROTOCOL_OPTIONS protocol_options;
} IFDH_Context;

/* Matrix that stores conext information of all slots and readers */
static IFDH_Context *ifdh_context[IFDH_MAX_READERS][IFDH_MAX_SLOTS] = {
	{NULL},
	{NULL},
	{NULL},
	{NULL},
};

/* Mutexes for all readers */
#ifdef HAVE_PTHREAD
static pthread_mutex_t ifdh_context_mutex[IFDH_MAX_READERS] = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER
};
#endif

/* PC/SC Lite hotplugging base channel */
#define HOTPLUG_BASE_PORT	0x200000

RESPONSECODE IFDHCreateChannel(DWORD Lun, DWORD Channel)
{
	char ret;
	unsigned short ctn, pn, slot;
	RESPONSECODE rv;

	ctn = ((unsigned short)(Lun >> 16)) % IFDH_MAX_READERS;
	slot = ((unsigned short)(Lun & 0x0000FFFF)) % IFDH_MAX_SLOTS;

#ifdef HAVE_PTHREAD
	pthread_mutex_lock(&ifdh_context_mutex[ctn]);
#endif
	if (ifdh_context[ctn][slot] == NULL) {
		if (Channel >= HOTPLUG_BASE_PORT) {
			Channel -= HOTPLUG_BASE_PORT;
		}
		/* We don't care that much about IFDH CHANNELID handling */
		if (Channel > IFDH_MAX_READERS) {
			pn = 0;
		} else {
			pn = ((Channel == 0) ? 0 : Channel - 1);
		}
		ret = CT_init(ctn, pn);

		if (ret == OK) {
			/* Initialize context of the all slots in this reader */
			for (slot = 0; slot < IFDH_MAX_SLOTS; slot++) {
				ifdh_context[ctn][slot] = (IFDH_Context *)
				    malloc(sizeof(IFDH_Context));

				if (ifdh_context[ctn][slot] != NULL)
					memset(ifdh_context[ctn][slot], 0,
					       sizeof(IFDH_Context));
			}
			rv = IFD_SUCCESS;
		} else {
			rv = IFD_COMMUNICATION_ERROR;
		}
	} else {
		/* Assume that IFDHCreateChannel is being called for another
		   already initialized slot in this same reader, and return Success */
		rv = IFD_SUCCESS;
	}
#ifdef HAVE_PTHREAD
	pthread_mutex_unlock(&ifdh_context_mutex[ctn]);
#endif
#ifdef DEBUG_IFDH
	syslog(LOG_INFO, "IFDH: IFDHCreateChannel(Lun=0x%X, Channel=0x%X)=%d",
	       Lun, Channel, rv);
#endif
	return rv;
}

RESPONSECODE IFDHCloseChannel(DWORD Lun)
{
	char ret;
	unsigned short ctn, slot;
	RESPONSECODE rv;

	ctn = ((unsigned short)(Lun >> 16)) % IFDH_MAX_READERS;
	slot = ((unsigned short)(Lun & 0x0000FFFF)) % IFDH_MAX_SLOTS;

	ret = CT_close(ctn);

	if (ret == OK) {
#ifdef HAVE_PTHREAD
		pthread_mutex_lock(&ifdh_context_mutex[ctn]);
#endif
		/* Free context of the all slots in this reader */
		for (slot = 0; slot < IFDH_MAX_SLOTS; slot++) {
			if (ifdh_context[ctn][slot] != NULL) {
				free(ifdh_context[ctn][slot]);
				ifdh_context[ctn][slot] = NULL;
			}
		}
#ifdef HAVE_PTHREAD
		pthread_mutex_unlock(&ifdh_context_mutex[ctn]);
#endif
		rv = IFD_SUCCESS;
	} else {
		rv = IFD_COMMUNICATION_ERROR;
	}
#ifdef DEBUG_IFDH
	syslog(LOG_INFO, "IFDH: IFDHCloseChannel(Lun=0x%X)=%d", Lun, rv);
#endif
	return rv;
}

RESPONSECODE
IFDHGetCapabilities(DWORD Lun, DWORD Tag, PDWORD Length, PUCHAR Value)
{
	unsigned short ctn, slot;
	RESPONSECODE rv;

	ctn = ((unsigned short)(Lun >> 16)) % IFDH_MAX_READERS;
	slot = ((unsigned short)(Lun & 0x0000FFFF)) % IFDH_MAX_SLOTS;

#ifdef HAVE_PTHREAD
	pthread_mutex_lock(&ifdh_context_mutex[ctn]);
#endif
	switch (Tag) {
	case TAG_IFD_ATR:
		(*Length) = ifdh_context[ctn][slot]->ATR_Length;
		memcpy(Value, ifdh_context[ctn][slot]->icc_state.ATR,
		       (*Length));
		rv = IFD_SUCCESS;
		break;

	case TAG_IFD_SLOTS_NUMBER:
		(*Length) = 1;
		(*Value) = IFDH_MAX_SLOTS;
		rv = IFD_SUCCESS;
		break;

	case TAG_IFD_SIMULTANEOUS_ACCESS:
		(*Length) = 1;
		(*Value) = IFDH_MAX_READERS;
		rv = IFD_SUCCESS;
		break;

	default:
		(*Length) = 0;
		rv = IFD_ERROR_TAG;
	}
#ifdef HAVE_PTHREAD
	pthread_mutex_unlock(&ifdh_context_mutex[ctn]);
#endif
#ifdef DEBUG_IFDH
	syslog(LOG_INFO, "IFDH: IFDHGetCapabilities (Lun=0x%X, Tag=0x%X)=%d",
	       Lun, Tag, rv);
#endif
	return rv;
}

RESPONSECODE
IFDHSetCapabilities(DWORD Lun, DWORD Tag, DWORD Length, PUCHAR Value)
{
#ifdef DEBUG_IFDH
#if 0
	syslog(LOG_INFO, "IFDH: IFDHSetCapabilities (Lun=%X, Tag=%X)=%d", Lun,
	       Tag, IFD_NOT_SUPPORTED);
#endif
#endif
	return IFD_NOT_SUPPORTED;
}

RESPONSECODE
IFDHSetProtocolParameters(DWORD Lun, DWORD Protocol,
			  UCHAR Flags, UCHAR PTS1, UCHAR PTS2, UCHAR PTS3)
{
	char ret;
	unsigned short ctn, slot, lc, lr;
	UCHAR cmd[10], rsp[256], sad, dad;
	RESPONSECODE rv;

	ctn = ((unsigned short)(Lun >> 16)) % IFDH_MAX_READERS;
	slot = ((unsigned short)(Lun & 0x0000FFFF)) % IFDH_MAX_SLOTS;

#ifdef HAVE_PTHREAD
	pthread_mutex_lock(&ifdh_context_mutex[ctn]);
#endif
	if (ifdh_context[ctn][slot] != NULL) {
		cmd[0] = CTBCS_CLA_2;
		cmd[1] = CTBCS_INS_SET_INTERFACE_PARAM;
		cmd[2] = (UCHAR) (slot + 1);
		cmd[3] = 0x00;
		cmd[4] = 0x03;
		cmd[5] = CTBCS_TAG_TPP;
		cmd[6] = 0x01;
		cmd[7] = Protocol & 0xFF;

		lc = 8;

		dad = 0x01;
		sad = 0x02;
		lr = 256;

		ret = CT_data(ctn, &dad, &sad, lc, cmd, &lr, rsp);

		if (ret == OK) {
			rv = IFD_SUCCESS;
		} else {
			rv = IFD_ERROR_PTS_FAILURE;
		}
	} else {
		rv = IFD_ICC_NOT_PRESENT;
	}
#ifdef HAVE_PTHREAD
	pthread_mutex_unlock(&ifdh_context_mutex[ctn]);
#endif
#ifdef DEBUG_IFDH
	syslog(LOG_INFO,
	       "IFDH: IFDHSetProtocolParameters (Lun=0x%X, Protocol=%d, Flags=0x%02X, PTS1=0x%02X, PTS2=0x%02X, PTS3=0x%02X)=%d",
	       Lun, Protocol, Flags, PTS1, PTS2, PTS3, rv);
#endif
	return rv;
}

RESPONSECODE IFDHPowerICC(DWORD Lun, DWORD Action, PUCHAR Atr, PDWORD AtrLength)
{
	char ret;
	unsigned short ctn, slot, lc, lr;
	UCHAR cmd[5], rsp[256], sad, dad;
	RESPONSECODE rv;

	ctn = ((unsigned short)(Lun >> 16)) % IFDH_MAX_READERS;
	slot = ((unsigned short)(Lun & 0x0000FFFF)) % IFDH_MAX_SLOTS;

#ifdef HAVE_PTHREAD
	pthread_mutex_lock(&ifdh_context_mutex[ctn]);
#endif
	if (ifdh_context[ctn][slot] != NULL) {
		if (Action == IFD_POWER_UP) {
			cmd[0] = CTBCS_CLA;
			cmd[1] = CTBCS_INS_REQUEST_ICC;
			cmd[2] = (UCHAR) (slot + 1);
			cmd[3] = CTBCS_P2_REQUEST_GET_ATR;
			cmd[4] = 0x00;

			dad = 0x01;
			sad = 0x02;
			lr = 256;
			lc = 5;

			ret = CT_data(ctn, &dad, &sad, 5, cmd, &lr, rsp);

			if ((ret == OK) && (lr >= 2)) {
				ifdh_context[ctn][slot]->ATR_Length =
				    (DWORD) lr - 2;
				memcpy(ifdh_context[ctn][slot]->icc_state.ATR,
				       rsp, lr - 2);

				(*AtrLength) = (DWORD) lr - 2;
				memcpy(Atr, rsp, lr - 2);

				rv = IFD_SUCCESS;
			} else {
				rv = IFD_COMMUNICATION_ERROR;
			}
		} else if (Action == IFD_POWER_DOWN) {
			cmd[0] = CTBCS_CLA;
			cmd[1] = CTBCS_INS_EJECT_ICC;
			cmd[2] = (UCHAR) (slot + 1);
			cmd[3] = 0x00;
			cmd[4] = 0x00;

			dad = 0x01;
			sad = 0x02;
			lr = 256;
			lc = 5;

			ret = CT_data(ctn, &dad, &sad, 5, cmd, &lr, rsp);

			if (ret == OK) {
				ifdh_context[ctn][slot]->ATR_Length = 0;
				memset(ifdh_context[ctn][slot]->icc_state.ATR,
				       0, MAX_ATR_SIZE);

				(*AtrLength) = 0;
				rv = IFD_SUCCESS;
			} else {
				rv = IFD_COMMUNICATION_ERROR;
			}
		} else if (Action == IFD_RESET) {
			cmd[0] = CTBCS_CLA;
			cmd[1] = CTBCS_INS_RESET;
			cmd[2] = (UCHAR) (slot + 1);
			cmd[3] = CTBCS_P2_RESET_GET_ATR;
			cmd[4] = 0x00;

			dad = 0x01;
			sad = 0x02;
			lr = 256;
			lc = 5;

			ret = CT_data(ctn, &dad, &sad, 5, cmd, &lr, rsp);

			if ((ret == OK) && (lr >= 2)) {
				ifdh_context[ctn][slot]->ATR_Length =
				    (DWORD) lr - 2;
				memcpy(ifdh_context[ctn][slot]->icc_state.ATR,
				       rsp, lr - 2);

				(*AtrLength) = (DWORD) lr - 2;
				memcpy(Atr, rsp, lr - 2);

				rv = IFD_SUCCESS;
			} else {
				rv = IFD_ERROR_POWER_ACTION;
			}
		} else {
			rv = IFD_NOT_SUPPORTED;
		}
	} else {
		rv = IFD_ICC_NOT_PRESENT;
	}
#ifdef HAVE_PTHREAD
	pthread_mutex_unlock(&ifdh_context_mutex[ctn]);
#endif
#ifdef DEBUG_IFDH
	syslog(LOG_INFO, "IFDH: IFDHPowerICC (Lun=0x%X, Action=0x%X)=%d", Lun,
	       Action, rv);
#endif
	return rv;
}

RESPONSECODE
IFDHTransmitToICC(DWORD Lun, SCARD_IO_HEADER SendPci,
		  PUCHAR TxBuffer, DWORD TxLength,
		  PUCHAR RxBuffer, PDWORD RxLength, PSCARD_IO_HEADER RecvPci)
{
	char ret;
	unsigned short ctn, slot, lc, lr;
	UCHAR sad, dad;
	RESPONSECODE rv;

	ctn = ((unsigned short)(Lun >> 16)) % IFDH_MAX_READERS;
	slot = ((unsigned short)(Lun & 0x0000FFFF)) % IFDH_MAX_SLOTS;

#ifdef HAVE_PTHREAD
	pthread_mutex_lock(&ifdh_context_mutex[ctn]);
#endif
	if (ifdh_context[ctn][slot] != NULL) {
#ifdef HAVE_PTHREAD
		pthread_mutex_unlock(&ifdh_context_mutex[ctn]);
#endif
		dad = (UCHAR) ((slot == 0) ? 0x00 : slot + 1);
		sad = 0x02;
		lr = (unsigned short)(*RxLength);
		lc = (unsigned short)TxLength;

		ret = CT_data(ctn, &dad, &sad, lc, TxBuffer, &lr, RxBuffer);

		if (ret == OK) {
			(*RxLength) = lr;
			rv = IFD_SUCCESS;
		} else {
			(*RxLength) = 0;
			rv = IFD_COMMUNICATION_ERROR;
		}
	} else {
#ifdef HAVE_PTHREAD
		pthread_mutex_unlock(&ifdh_context_mutex[ctn]);
#endif
		rv = IFD_ICC_NOT_PRESENT;
	}
#ifdef DEBUG_IFDH
	syslog(LOG_INFO, "IFDH: IFDHTransmitToICC (Lun=0x%X, Tx=%u, Rx=%u)=%d",
	       Lun, TxLength, (*RxLength), rv);
#endif
	return rv;
}

#ifdef IFDHANDLERv2

RESPONSECODE
IFDHControl(DWORD Lun, PUCHAR TxBuffer,
	    DWORD TxLength, PUCHAR RxBuffer, PDWORD RxLength)
{
	char ret;
	unsigned short ctn, slot, lc, lr;
	UCHAR sad, dad;
	RESPONSECODE rv;

	ctn = ((unsigned short)(Lun >> 16)) % IFDH_MAX_READERS;
	slot = ((unsigned short)(Lun & 0x0000FFFF)) % IFDH_MAX_SLOTS;

#ifdef HAVE_PTHREAD
	pthread_mutex_lock(&ifdh_context_mutex[ctn]);
#endif
	if (ifdh_context[ctn][slot] != NULL) {
#ifdef HAVE_PTHREAD
		pthread_mutex_unlock(&ifdh_context_mutex[ctn]);
#endif
		dad = 0x01;
		sad = 0x02;
		lr = (unsigned short)(*RxLength);
		lc = (unsigned short)TxLength;

		ret = CT_data(ctn, &dad, &sad, lc, TxBuffer, &lr, RxBuffer);

		if (ret == OK) {
			(*RxLength) = lr;
			rv = IFD_SUCCESS;
		} else {
			(*RxLength) = 0;
			rv = IFD_COMMUNICATION_ERROR;
		}
	} else {
#ifdef HAVE_PTHREAD
		pthread_mutex_unlock(&ifdh_context_mutex[ctn]);
#endif
		rv = IFD_ICC_NOT_PRESENT;
	}
#ifdef DEBUG_IFDH
	syslog(LOG_INFO, "IFDH: IFDHControl (Lun=0x%X, Tx=%u, Rx=%u)=%d", Lun,
	       TxLength, (*RxLength), rv);
#endif
	return rv;
}

#else

RESPONSECODE IFDHControl(DWORD Lun, DWORD dwControlCode,
			 PUCHAR TxBuffer, DWORD TxLength, PUCHAR RxBuffer,
			 DWORD RxLength, PDWORD pdwBytesReturned)
{
	/* FIXME */
}

#endif

RESPONSECODE IFDHICCPresence(DWORD Lun)
{
	char ret;
	unsigned short ctn, slot, lc, lr;
	UCHAR cmd[5], rsp[256], sad, dad;
	RESPONSECODE rv;

	ctn = ((unsigned short)(Lun >> 16)) % IFDH_MAX_READERS;
	slot = ((unsigned short)(Lun & 0x0000FFFF)) % IFDH_MAX_SLOTS;

	cmd[0] = CTBCS_CLA;
	cmd[1] = CTBCS_INS_STATUS;
	cmd[2] = CTBCS_UNIT_CT;
	cmd[3] = CTBCS_P2_STATUS_ICC;
	cmd[4] = 0x00;

	dad = 0x01;
	sad = 0x02;
	lc = 5;
	lr = 256;

	ret = CT_data(ctn, &dad, &sad, lc, cmd, &lr, rsp);

	if (ret == OK) {
		if (slot < lr - 2) {
			if (rsp[slot] == CTBCS_DATA_STATUS_NOCARD) {
				rv = IFD_ICC_NOT_PRESENT;
			} else {
				rv = IFD_ICC_PRESENT;
			}
		} else {
			rv = IFD_ICC_NOT_PRESENT;
		}
	} else {
		rv = IFD_COMMUNICATION_ERROR;
	}
#ifdef DEBUG_IFDH
	syslog(LOG_INFO, "IFDH: IFDHICCPresence (Lun=0x%X)=%d", Lun, rv);
#endif
	return rv;
}
