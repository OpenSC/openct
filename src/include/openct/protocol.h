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

#define CT_CMD_STATUS		0x00
#define CT_CMD_LOCK		0x01	/* prevent concurrent access */
#define CT_CMD_UNLOCK		0x02
#define CT_CMD_RESET		0x10
#define CT_CMD_REQUEST_ICC	0x11
#define CT_CMD_EJECT_ICC	0x12
#define CT_CMD_TRANSACT		0x20	/* transceive APDU */

#define CT_UNIT_ICC1		0x00
#define CT_UNIT_ICC2		0x01
#define CT_UNIT_ICC3		0x02
#define CT_UNIT_ICC4		0x03
#define CT_UNIT_READER		0x10
#define CT_UNIT_DISPLAY		0x11
#define CT_UNIT_KEYPAD		0x12

/*
 * TLV items.
 */
#define CT_TAG_READER_NAME	0x00	/* ASCII string */
#define CT_TAG_READER_UNITS	0x01	/* list CT_UNIT_* bytes */
#define CT_TAG_CARD_STATUS	0x02	/* IFD_CARD_* byte */
#define CT_TAG_ATR		0x03	/* Answer to reset */
#define CT_TAG_LOCK		0x04
#define CT_TAG_TIMEOUT		0x80
#define CT_TAG_MESSAGE		0x81
#define CT_TAG_LOCKTYPE		0x82


#endif /* IFDMGR_PROTOCOL_H */
