/**
 * @file
 * Internal driver API.
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef OPENCT_DRIVER_H
#define OPENCT_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <openct/device.h>

/**
 * Driver operations.
 */
struct ifd_driver_ops {
	int		default_protocol;

	/**
	 * Open the reader.
	 *
	 * This function opens the device named @a name and initialize the reader
	 * structure. The slots' dad should be set here.
	 *
	 * OpenCT calls the open function once at startup.
	 *
	 * Called by: ifd_open.
	 * @return Error code <0 if failure.
	 */
	int		(*open)(ifd_reader_t *reader, const char *name);
	/**
	 * Close the reader.
	 *
	 * This function frees all the resources allocated by *open. If made possible by
	 * the hardware, the reader and all its slots must be powered off.
	 *
	 * The device will be closed by ifd_close, and must <b>not</b> be closed by this
	 * function.
	 *
	 * OpenCT currently never calls ifd_close, so the close function if not used.
	 *
	 * Called by: ifd_close.
	 * @return Error code <0 if failure.
	 */
	int		(*close)(ifd_reader_t * reader);
	/**
	 * Change the communication protocol parity.
	 *
	 * This function is only used for asynchronous smart cards with serial readers,
	 * and only if the reader doesn't auto-detect the protocol parity.
	 *
	 * Depending on the smart card communication protocol convention (either direct
	 * or inverse, see ISO-7816-3, 6.4), the communication between the reader and the
	 * smart card uses odd or even parity. If most readers handle smart card protocol
	 * convention transparently, the most simple ones map the smart card lines
	 * directly on the serial port or require the user to configure the reader
	 * according smart card communication protocol parity. For those readers, OpenCT
	 * will try both parities (starting with even) when reading the ATR.
	 *
	 * This function, if available, configures the reader and/or the serial port
	 * according to the smart card communication protocol parity. It is used by
	 * ifd_card_request only.
	 *
	 * Accepted values for @a parity are
	 *
	 * @arg @c IFD_SERIAL_PARITY_ODD   (1)
	 * @arg @c IFD_SERIAL_PARITY_EVEN  (2)
	 *
	 * Called by: ifd_card_request.
	 * @return Error code <0 if failure.
	 */
	int		(*change_parity)(ifd_reader_t *reader, int parity);
	/**
	 * Change the communication protocol speed.
	 *
	 * It is not clear if this function was intended to change the communication
	 * speed between the host and the reader, the reader and the smart card, or
	 * both, as it is currently not used (ifd_set_speed is never called).
	 *
	 * Called by: ifd_set_speed.
	 * @return Error code <0 if failure.
	 */
	int		(*change_speed)(ifd_reader_t *reader, unsigned int speed);
	/**
	 * Activate the smart card reader.
	 *
	 * This function performs all necessary operations to activate the smart card
	 * reader. After activation, the reader must be able to accept card status
	 * commands (card_status).
	 *
	 * Reader activation is currently performed once at startup, before calling
	 * card_status in response to CT_CMD_STATUS (is it really needed, as the reader
	 * was activated at startup ?), and when probing for synchronous cards.
	 *
	 * Called by: ifd_activate.
	 * @return Error code <0 if failure.
	 */
	int		(*activate)(ifd_reader_t *reader);
	/**
	 * Deactivate the smart card reader.
	 *
	 * Reader deactivation is currently performed only when probing for synchronous
	 * cards. OpenCT deactivates and reactivates the reader before trying to access
	 * the card.
	 *
	 * Called by: ifd_deactivate.
	 * @return Error code <0 if failure.
	 */
	int		(*deactivate)(ifd_reader_t *reader);
	/**
	 * Fetch the card status.
	 *
	 * This function returns the current card status for a given slot. status is a
	 * bitfield which currently accepts the following flags:
	 *
	 * @arg @c IFD_CARD_PRESENT        (1)     A card is present in the slot.
	 * @arg @c IFD_CARD_STATUS_CHANGED (2)     The slot status has changed since last call.
	 *
	 * Beside using IFD_CARD_STATUS_CHANGED, OpenCT checks if the card status has
	 * changed by comparing the current status value with the previous status value.
	 * If the value of the IFD_CARD_PRESENT has changed, OpenCT will set the
	 * IFD_CARD_STATUS_CHANGED <b>on the client side</b>.
	 *
	 * On the server side, setting IFD_CARD_STATUS_CHANGED will also set atr_len to 0
	 * for the given slot. As atr_len is only checked in ifd_protocol_select, and as
	 * that function is only called in ifd_card_request right after assigning a value
	 * to atr_len, this is currently pointless and a reader driver should <b>not</b> set
	 * IFD_CARD_STATUS_CHANGED if its only source of information is a change of
	 * the IFD_CARD_PRESENT flag between the current call and the previous one.
	 *
	 * Called by: ifd_card_status.
	 * @return Error code <0 if failure.
	 */
	int		(*card_status)(ifd_reader_t *reader, int slot, int *status);
	/**
	 * Reset the card and fetch the ATR.
	 *
	 * This function resets the smart card and reads the Answer To Reset (ATR). The
	 * ATR will be stored in the slot structure by OpenCT, so the driver <b>must not</b>
	 * store the ATR itself in the slot structure.
	 *
	 * When resetting the card, the driver is allowed to select a protocol. If no
	 * protocol is selected when this function returns (assuming it returns
	 * successfully), OpenCT will try to select the protocol automatically (see
	 * set_protocol).
	 *
	 * Called by: ifd_card_request.
	 * @return Error code <0 if failure, length of the ATR if success.
	 */
	int		(*card_reset)(ifd_reader_t *reader, int slot, void *atr, size_t atr_len);
	/**
	 * Prompts the insertion of a smart card in the reader and performs a card reset.
	 *
	 * This function prompts the user to insert a smart card in the reader. For card
	 * readers equipped with a display, the optional message message should be
	 * displayed to the user. The optional parameter timeout, if not equal to zero,
	 * gives the prompt timeout in seconds.
	 *
	 * If a card is inserted before the timeout elapses, the card is reset and the
	 * ATR is fetched. See card_reset for more information.
	 *
	 * Called by: ifd_card_request
	 * @return Error code <0 if failure, length of the ATR if success.
	 */
	int		(*card_request)(ifd_reader_t *reader, int slot,
				time_t timeout, const char *message,
				void *atr, size_t atr_len);
	/**
	 * Eject the smart card.
	 *
	 * This function ejects the smart card or prompt the user to remove the card by
	 * displaying the message @a message. The optional parameter @a timeout, if not equal
	 * to zero, gives the card removal timeout in seconds.
	 *
	 * It is not clear if this function should deactivate the smart card first, or
	 * if the caller must deactivate the card before calling card_eject.
	 *
	 * Called by: ifd_card_eject.
	 * @return Error code <0 if failure.
	 */
	int		(*card_eject)(ifd_reader_t *reader, int slot,
				time_t timeout, const char *message);
	/**
	 * Output a message on the reader's display.
	 *
	 * Called by: ifd_output.
	 * @return Error code <0 if failure.
	 */
	int		(*output)(ifd_reader_t *reader, const char *message);
	/**
	 * Request the PIN and have the card verify it.
	 *
	 * This function prompts the user to enter his PIN code and have the smart card
	 * verify the code. The optional parameter @a timeout, if not equal to zero, gives
	 * the PIN request timeout.
	 *
	 * The two bytes status (SW1-SW2) is copied in the @a resp buffer. If the status
	 * indicates a known error (IFD_ERROR_USER_TIMEOUT, IFD_ERROR_USER_ABORT,
	 * IFD_ERROR_PIN_MISMATCH) the error code is returned. Otherwise the length
	 * of the response (2) is returned.
	 *
	 * Called by: ifd_perform_verify.
	 * @return Error code <0 if failure, size of the response (2) if success or unknown error.
	 */
	int		(*perform_verify)(ifd_reader_t *reader,
			       	int slot, unsigned int timeout, const char *prompt,
				const unsigned char *data, size_t data_len,
				unsigned char *resp, size_t resp_len);
	/**
	 * Send an APDU using the currently selected protocol.
	 *
	 * This function is called by all non-transparent protocols to send an APDU to a
	 * device. The device address is set by the @a dad parameter which is one of
	 *
	 * @arg @c IFD_DAD_HOST    (0)     Host (PC) - what's the point ?
	 * @arg @c IFD_DAD_IFD     (1)     The APDU is sent to the reader
	 * @arg @c IFD_DAD_ICC1    (2)     The APDU is sent to the first smart card
	 * @arg @c IFD_DAD_ICC2    (3)     The APDU is sent to the second smart card
	 *
	 * The @a dad parameter is currently unused by all drivers, even the Kaan driver
	 * which takes care to initialize it correctly in the open function.
	 *
	 * Called by: ifd_send_command.
	 * @return Error code <0 if failure, number of bytes sent if success.
	 */
	int		(*send)(ifd_reader_t *reader,
				unsigned int dad,
				const unsigned char *buffer,
				size_t len);
	/**
	 * Receive an APDU response using the currently selected protocol.
	 *
	 * This function is called by all non-transparent protocols to receive an APDU
	 * response from a device. The device address is set by the @a dad parameter which
	 * takes the same values as for the send function.
	 *
	 * The @a dad parameter is currently unused by all drivers, even the Kaan driver
	 * which takes care to initialize it correctly in the open function.
	 *
	 * Called by: ifd_recv_response.
	 * @return Error code <0 if failure, number of bytes received if success.
	 */
	int		(*recv)(ifd_reader_t *reader,
				unsigned int dad,
				unsigned char *buffer,
				size_t len,
				long timeout);
	/**
	 * Select the smart card communication protocol.
	 *
	 * The smart card communication protocol is selected by OpenCT when the card is
	 * reset (either by reset_card or request_card), and can be overriden by the
	 * user by sending a CT_CMD_SET_PROTOCOL command.
	 *
	 * OpenCT doesn't perform a PPS exchange with the card itself, but selects the
	 * first protocol offered by the ATR at reset time by calling set_protocol. If
	 * the reader performs the PPS exchange with the card, the selected protocol can
	 * be set manually by the driver (by setting slot->proto) and set_protocol will
	 * not be called.
	 *
	 * It is not clear what the side effects of CT_CMD_SET_PROTOCOL (should it reset
	 * the card ?) should be and if that function is used at all by applications.
	 * Isn't the default protocol good enough ? I would have thought that
	 * set_protocol was meant to be called only right after reset, to select the
	 * protocol and instruct the driver to perform a PPS exchange.
	 *
	 * Called by: ifd_set_protocol, ifd_select_protocol.
	 * @return Error code <0 if failure.
	 */
	int		(*set_protocol)(ifd_reader_t *reader, int slot, int protocol);
	/**
	 * Transceive a protocol data unit.
	 *
	 * This function is used by the transparent protocol, when the driver and/or the
	 * reader accepts raw PDUs directly. When using the transparent protocol, the
	 * PDUs sent by the client are forwarded directly to the driver without being
	 * mangled by OpenCT. If the driver and/or the reader can't accept the PDUs
	 * directly (for instance when the reader doesn't support Case 4 APDUs in T=0
	 * protocol), the driver should instanciate the appropriate protocol handler
	 * to pre-process the PDUs. In that case, the send and recv functions will be
	 * used instead of the transparent function.
	 *
	 * Called by: trans_transceive at proto-trans.c
	 * @return Error code <0 if failure, size of the response if success.
	 */
	int		(*transparent)(ifd_reader_t *reader, int slot,
				const void *sbuf, size_t slen,
				void *rbuf, size_t rlen);
	/**
	 * Read data from a synchronous smart card.
	 *
	 * This function writes @a rlen bytes to the smart card, starting at address @a addr.
	 * The @a proto parameter specifies the desired communication protocol. The driver
	 * must set the protocol before reading data, or return an error if the protocol
	 * is not supported.
	 *
	 * Called by: sync_read at proto-sync.c, sync_write at proto-sync.c.
	 * @return Error code <0 if failure, number of bytes read if success.
	 */
	int		(*sync_read)(ifd_reader_t *reader, int slot, int proto,
				unsigned short addr,
				unsigned char *rbuf, size_t rlen);
	/**
	 * Write data to a synchronous smart card.
	 *
	 * This function reads @a slen bytes from the smart card, starting at address @a addr.
	 * The @a proto parameter specifies the desired communication protocol. The driver
	 * must set the protocol before writing data, or return an error if the protocol
	 * is not supported.
	 *
	 * Called by: sync_write at proto-sync.c.
	 * @return Error code <0 if failure, number of bytes written if success.
	 */
	int		(*sync_write)(ifd_reader_t *reader, int slot, int proto,
				unsigned short addr,
				const unsigned char *sbuf, size_t slen);

	/*Support for transparent access to "escape" */
	int		(*escape)(ifd_reader_t *reader, int slot,
				const void *sbuf, const size_t slen,
				void *rbuf, size_t rlen);

	/**
	 * Execute before command.
	 *
	 * Provides a chance to setup device to accept new commands.
	 *
	 * @return Error code <0 if failure, 0 if success.
	 */
	int (*before_command) (ifd_reader_t *);

	/**
	 * Execute after command.
	 *
	 * Provides a chance to setup device to accept events.
	 *
	 * @return Error code <0 if failure, 0 if success.
	 */
	int (*after_command) (ifd_reader_t *);

	/**
	 * Get event fd.
	 *
	 * This will allow the mainloop to wait for event instead of polling.
	 * May be NULL if unsupported.
	 *
	 * @return fd.
	 */
	int (*get_eventfd) (ifd_reader_t *, short *events);

	/**
	 * Event callback.
	 *
	 * Will be called if an event is set.
	 * May be NULL if unsupported.
	 *
	 * @return Error code <0 if failure, 0 if success.
	 */
	int (*event) (ifd_reader_t *, int *status, size_t status_size);

	/**
	 * Error callback.
	 *
	 * Will be called if an error is set on event fd.
	 * May be NULL if unsupported.
	 *
	 * @return Error code <0 if failure, 0 if success. If reader
	 * should be freed, return an error.
	 */
	int (*error) (ifd_reader_t *);
};

extern void		ifd_driver_register(const char *,
				struct ifd_driver_ops *);
extern const ifd_driver_t *ifd_driver_get(const char *);
extern int		ifd_driver_add_id(const char *, const char *);
extern const char *	ifd_driver_for_id(ifd_devid_t *);

/*
 * Protocol handling for card terminal drivers
 */
extern ifd_protocol_t *	ifd_protocol_select(ifd_reader_t *, int, int);
extern int		ifd_protocol_transceive(ifd_protocol_t *proto,
				int dad,
				const void *, size_t,
				void *, size_t);
extern int		ifd_protocol_resynchronize(ifd_protocol_t *, int);

#ifdef __cplusplus
}
#endif

#endif /* OPENCT_DRIVER_H */
