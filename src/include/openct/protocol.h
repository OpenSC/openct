/*
 * Protocol for communication between application and
 * resource manager
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#ifndef IFDMGR_PROTOCOL_H
#define IFDMGR_PROTOCOL_H

/*
 * A protocol message from client to server
 * consists of
 *  -	command byte
 *  -	unit byte
 *  -	optional data, TLV encoded
 */

#define IFD_CMD_STATUS		0x00

#define IFD_UNIT_ICC1		0x00
#define IFD_UNIT_ICC2		0x01
#define IFD_UNIT_ICC3		0x02
#define IFD_UNIT_ICC4		0x03
#define IFD_UNIT_CT		0x10
#define IFD_UNIT_DISPLAY	0x11
#define IFD_UNIT_KEYPAD		0x12

/*
 * TLV items.
 *
 * Tag types are encoded in the highest two bits:
 *  00		integer
 *  01		ASCII string (must include NUL byte)
 *  10		opaque
 *  11		RFU
 */
#define IFD_TAG_CARD_STATUS	0x00	/* IFD_CARD_* byte */
#define IFD_TAG_READER_NAME	0x40	/* ASCII string */
#define IFD_TAG_READER_UNITS	0x80	/* list if IFD_UNIT_* bytes */

#define IFD_TAG_MAX		0xFF


#endif /* IFDMGR_PROTOCOL_H */
