/*
 * Protocol for communication between application and
 * resource manager
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_PROTOCOL_H
#define OPENCT_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

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
#define CT_CMD_OUTPUT		0x13
#define CT_CMD_PERFORM_VERIFY	0x14
#define CT_CMD_CHANGE_PIN	0x15
#define CT_CMD_MEMORY_READ	0x16
#define CT_CMD_MEMORY_WRITE	0x17
#define CT_CMD_INPUT		0x18
#define CT_CMD_TRANSACT_OLD	0x20	/* transceive APDU */
#define CT_CMD_TRANSACT		0x21	/* transceive APDU */
#define CT_CMD_SET_PROTOCOL	0x22

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
#define CT_TAG_CARD_RESPONSE	0x05	/* Card response to VERIFY etc */
#define CT_TAG_TIMEOUT		0x80
#define CT_TAG_MESSAGE		0x81
#define CT_TAG_LOCKTYPE		0x82
#define CT_TAG_PIN_DATA		0x83	/* CTBCS verify APDU */
#define CT_TAG_CARD_REQUEST	0x84
#define CT_TAG_ADDRESS		0x85
#define CT_TAG_DATA		0x86
#define CT_TAG_COUNT		0x87
#define CT_TAG_PROTOCOL		0x88

#define __CT_TAG_LARGE		0x40

#ifdef __cplusplus
}
#endif

#endif /* OPENCT_PROTOCOL_H */
