/*
 * Defines CT-API functions and returns
 *
 * Copyright (C) 1998, David Corcoran
 */

#ifndef OPENCT_CTAPI_H
#define OPENCT_CTAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_APDULEN	1040

	char CT_init(unsigned short Ctn,	/* Terminal Number */
		     unsigned short pn	/* Port Number */
	    );

	char CT_close(unsigned short Ctn	/* Terminal Number */
	    );

	char CT_data(unsigned short ctn,	/* Terminal Number */
		     unsigned char *dad,	/* Destination */
		     unsigned char *sad,	/* Source */
		     unsigned short lc,	/* Length of command */
		     unsigned char *cmd,	/* Command/Data Buffer */
		     unsigned short *lr,	/* Length of Response */
		     unsigned char *rsp	/* Response */
	    );

#define OK		0	/* Success */
#define ERR_INVALID	-1	/* Invalid Data */
#define ERR_CT		-8	/* CT Error */
#define ERR_TRANS	-10	/* Transmission Error */
#define ERR_MEMORY	-11	/* Memory Allocate Error */
#define ERR_HOST	-127	/* Abort by Host/OS */
#define ERR_HTSI	-128	/* HTSI Error */

	enum {
		CTAPI_DAD_ICC1 = 0,
		CTAPI_DAD_CT = 1,
		CTAPI_DAD_HOST = 2,
		CTAPI_DAD_ICC2 = 3
	};

/*
 * CT-BCS commands
 */
#define CTBCS_CLA			0x20
#define CTBCS_CLA_2			0x80
#define CTBCS_INS_RESET			0x11
#define CTBCS_INS_REQUEST_ICC		0x12
#define CTBCS_INS_STATUS		0x13
#define CTBCS_INS_EJECT_ICC		0x15
#define CTBCS_INS_INPUT			0x16
#define CTBCS_INS_OUTPUT		0x17
#define CTBCS_INS_PERFORM_VERIFICATION	0x18
#define CTBCS_INS_MODIFY_VERIFICATION	0x19
#define CTBCS_INS_SET_INTERFACE_PARAM	0x60

/*
 * CT-BCS functional units (P1 byte)
 */
#define CTBCS_UNIT_CT			0x00
#define CTBCS_UNIT_INTERFACE1		0x01
#define CTBCS_UNIT_INTERFACE2		0x02
#define CTBCS_UNIT_DISPLAY		0x40
#define CTBCS_UNIT_KEYPAD		0x50

/*
 * P2 parameter for Reset CT: data to be returned
 */
#define CTBCS_P2_RESET_NO_RESP		0x00	/* Return no data */
#define CTBCS_P2_RESET_GET_ATR		0x01	/* Return complete ATR */
#define CTBCS_P2_RESET_GET_HIST		0x02	/* Return historical bytes */

/*
 * P2 parameter for Request ICC: data to be returned
 */
#define CTBCS_P2_REQUEST_NO_RESP	0x00	/* Return no data */
#define CTBCS_P2_REQUEST_GET_ATR	0x01	/* Return complete ATR */
#define CTBCS_P2_REQUEST_GET_HIST	0x02	/* Return historical bytes */

/*
 * P2 parameter for Get status: TAG of data object to return
 */
#define CTBCS_P2_STATUS_MANUFACTURER	0x46	/* Return manufacturer DO */
#define CTBCS_P2_STATUS_ICC		0x80	/* Return ICC DO */

/*
 * P2 parameter for Input
 */
#define CTBCS_P2_INPUT_ECHO		0x01	/* Echo input on display */
#define CTBCS_P2_INPUT_ASTERISKS	0x02	/* Echo input as asterisks */

/*
 * Tags for paramaters to input, output et al.
 */
#define CTBCS_TAG_PROMPT		0x50
#define CTBCS_TAG_VERIFY_CMD		0x52
#define CTBCS_TAG_TIMEOUT		0x80
#define CTBCS_TAG_TPP			0x22
#define CTBCS_TAG_TPC			0x45

/*
 * PIN command control flags
 */
#define CTBCS_PIN_CONTROL_LEN_SHIFT	4
#define CTBCS_PIN_CONTROL_LEN_MASK	0x0F
#define CTBCS_PIN_CONTROL_ENCODE_ASCII	0x01

/*
 * Status words returned by CTBCS
 */

#define CTBCS_SW_BAD_LENGTH		0x6700
#define CTBCS_SW_BAD_COMMAND		0x6900
#define CTBCS_SW_NOT_EXECUTABLE	0x6985
#define CTBCS_SW_BAD_PARAMS		0x6a00
#define CTBCS_SW_NOT_SUPPORTED		0x6a80
#define CTBCS_SW_INVALID_TLV		0x6a85
#define CTBCS_SW_BAD_LE		0x6c00
#define CTBCS_SW_BAD_INS		0x6d00
#define CTBCS_SW_BAD_CLASS		0x6e00
#define CTBCS_SW_BAD_ICC		0x6f00
#define CTBCS_SW_SUCCESS		0x9000

/*
 * Data returned by Get Status command
 */
#define CTBCS_DATA_STATUS_NOCARD	0x00	/* No card present */
#define CTBCS_DATA_STATUS_CARD		0x01	/* Card present */
#define CTBCS_DATA_STATUS_CARD_CONNECT	0x05	/* Card present */

#ifdef __cplusplus
}
#endif
#endif				/* OPENCT_CTAPI_H */
