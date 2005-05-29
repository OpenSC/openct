/*
 * Build Extended CTBCS APDUs for those readers that
 * support them (such as Kobil Kaan).
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#ifndef IFD_CTBCS_H
#define IFD_CTBCS_H

extern int ctbcs_build_output(unsigned char *cmd, size_t size,
			      const char *message);
extern int ctbcs_build_perform_verify_apdu(unsigned char *cmd, size_t size,
					   unsigned int slot,
					   const char *prompt,
					   unsigned int timeout,
					   const unsigned char *data,
					   size_t data_len);
extern int ctbcs_build_modify_verify_apdu(unsigned char *cmd, size_t size,
					  unsigned int dest, const char *prompt,
					  unsigned int timeout,
					  const unsigned char *data,
					  size_t data_len);

extern void ctbcs_begin(ct_buf_t *, unsigned int, unsigned int, unsigned int);
extern int ctbcs_finish(ct_buf_t *);
extern int ctbcs_add_message(ct_buf_t *, const char *);
extern int ctbcs_add_timeout(ct_buf_t *, unsigned int);

#endif				/* IFD_CTBCS_H */
