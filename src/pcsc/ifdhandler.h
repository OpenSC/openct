#ifndef PCSCLITE_IFDHANDLER_H
#define PCSCLITE_IFDHANDLER_H

/* from pcsc-lite 1.1.1 src/ifdhandler.h */

#include <wintypes.h>

        typedef struct _SCARD_IO_HEADER
        {
                DWORD Protocol;
                DWORD Length;
        }
        SCARD_IO_HEADER, *PSCARD_IO_HEADER;

	/*
	 * The list of tags should be alot more but this is all I use in the
	 * meantime 
	 */

#define TAG_IFD_ATR			0x0303
#define TAG_IFD_SLOTNUM                 0x0180
#define TAG_IFD_SLOTS_NUMBER            0x0FAE
#define TAG_IFD_SIMULTANEOUS_ACCESS     0x0FAF

	/*
	 * End of tag list 
	 */

	/*
	 * IFD Handler version number enummerations 
	 */
#define IFD_HVERSION_1_0               0x00010000
#define IFD_HVERSION_2_0               0x00020000
	/*
	 * End of version number enummerations 
	 */

	/*
	 * List of defines available to ifdhandler 
	 */

#define IFD_POWER_UP			500
#define IFD_POWER_DOWN			501
#define IFD_RESET			502

#define IFD_NEGOTIATE_PTS1		1
#define IFD_NEGOTIATE_PTS2		2
#define IFD_NEGOTIATE_PTS3              4

#define	IFD_SUCCESS			0
#define IFD_ERROR_TAG			600
#define IFD_ERROR_SET_FAILURE		601
#define IFD_ERROR_VALUE_READ_ONLY	602
#define IFD_ERROR_PTS_FAILURE		605
#define IFD_ERROR_NOT_SUPPORTED		606
#define IFD_PROTOCOL_NOT_SUPPORTED	607
#define IFD_ERROR_POWER_ACTION		608
#define IFD_ERROR_SWALLOW		609
#define IFD_ERROR_EJECT			610
#define IFD_ERROR_CONFISCATE		611
#define IFD_COMMUNICATION_ERROR		612
#define IFD_RESPONSE_TIMEOUT		613
#define IFD_NOT_SUPPORTED		614
#define IFD_ICC_PRESENT			615
#define IFD_ICC_NOT_PRESENT		616

#endif /* PCSCLITE_IFDHANDLER_H */
