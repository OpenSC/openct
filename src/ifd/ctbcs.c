/*
 * Build Extended CTBCS APDUs for those readers that
 * support them (such as Kobil Kaan).
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ctbcs.h"

/*
 * Start building CTBCS apdu
 */
void ctbcs_begin(ct_buf_t * bp, unsigned int ins, unsigned int p1,
		 unsigned int p2)
{
	ct_buf_putc(bp, 0x20);
	ct_buf_putc(bp, ins);
	ct_buf_putc(bp, p1);
	ct_buf_putc(bp, p2);
	ct_buf_putc(bp, 0);
}

/*
 * Finish CTBCS apdu
 */
int ctbcs_finish(ct_buf_t * bp)
{
	unsigned int len;

	if (ct_buf_overrun(bp))
		return IFD_ERROR_BUFFER_TOO_SMALL;

	len = ct_buf_avail(bp);
	bp->base[4] = len - 5;	/* lc */
	return len;
}

/*
 * Output a string to the display
 */
int ctbcs_build_output(unsigned char *cmd, size_t size, const char *message)
{
	ct_buf_t buf;

	if (message == NULL)
		return IFD_ERROR_INVALID_ARG;

	ct_buf_init(&buf, cmd, size);
	ctbcs_begin(&buf, 0x17, 0x40, 0x00);
	ctbcs_add_message(&buf, message);
	return ctbcs_finish(&buf);
}

/*
 * Generic Verify APDU
 */
static int ctbcs_build_verify_apdu(unsigned char *cmd, size_t size,
				   unsigned char ins, unsigned char p1,
				   const char *prompt, unsigned int timeout,
				   const unsigned char *data, size_t data_len)
{
	ct_buf_t buf;

	if (!data || !data_len)
		return IFD_ERROR_INVALID_ARG;

	if (prompt == NULL)
		return IFD_ERROR_INVALID_ARG;
	ct_buf_init(&buf, cmd, size);
	ctbcs_begin(&buf, ins, p1, 0x00);

	ctbcs_add_timeout(&buf, timeout);
	ctbcs_add_message(&buf, prompt);

	ct_buf_putc(&buf, 0x52);
	ct_buf_putc(&buf, data_len);
	ct_buf_put(&buf, data, data_len);
	if (ct_buf_overrun(&buf))
		return IFD_ERROR_BUFFER_TOO_SMALL;

	cmd[4] = ct_buf_avail(&buf) - 5;	/* lc */
	return ct_buf_avail(&buf);
}

/*
 * Build Perform Verify APDU
 */
int ctbcs_build_perform_verify_apdu(unsigned char *cmd, size_t size,
				    unsigned int p1, const char *prompt,
				    unsigned int timeout,
				    const unsigned char *data, size_t data_len)
{
	return ctbcs_build_verify_apdu(cmd, size, 0x18, p1,
				       prompt, timeout, data, data_len);
}

/*
 * Build Modify Verify APDU
 */
int ctbcs_build_modify_verify_apdu(unsigned char *cmd, size_t size,
				   unsigned int p1, const char *prompt,
				   unsigned int timeout,
				   const unsigned char *data, size_t data_len)
{
	return ctbcs_build_verify_apdu(cmd, size, 0x19, p1,
				       prompt, timeout, data, data_len);
}

/*
 * Helper function add message/timeout arguments to command
 * buffer
 */
int ctbcs_add_timeout(ct_buf_t * bp, unsigned int timeout)
{
	if (!timeout)
		return 0;
	ct_buf_putc(bp, 0x80);
	ct_buf_putc(bp, 1);
	ct_buf_putc(bp, timeout);
	return ct_buf_avail(bp);
}

int ctbcs_add_message(ct_buf_t * bp, const char *message)
{
	int n;

	if (!message || !strcmp(message, "@"))
		return 0;

	if ((n = strlen(message)) > 32)
		n = 32;

	ct_buf_putc(bp, 0x50);
	ct_buf_putc(bp, n);
	ct_buf_put(bp, message, n);

	return ct_buf_avail(bp);
}
