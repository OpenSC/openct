/*
 * libifd configuration handling
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "internal.h"

static const char *	config_filename = NULL;
static ct_buf_t	config_buf;
static int		config_fd = -1;
static int		config_line = 0;

static int		parse_reader(void);
static int		parse_hotplug(void);
static int		parse_expect(const char *);
static int		get_string(char *, size_t);
static int		get_integer(int *);
static int		get_token(char **);
static int		skipws(void);
static int		ateof(void);
static int		err_unexpected_keyword(const char *, const char *);

/*
 * Parse the ifd config file
 */
int
ct_config_parse(const char *filename)
{
	char	kwd[16], buffer[512];
	int	rc;

	if ((config_filename = filename) == NULL)
		config_filename = OPENCT_CONFIG_PATH;

	/* If config file doesn't exist, quietly sneak out of here */
	if ((config_fd = open(config_filename, O_RDONLY)) < 0) {
		if (errno == ENOENT)
			return 0;
		ct_error("Unable to open %s: %m", filename);
		return -1;
	}

	/* Init parse buffer. */
	ct_buf_init(&config_buf, buffer, sizeof(buffer));
	config_line = 1;

	/* TBD - parse config file */
	/* Stuff that should go into this file:
	    -	configuration of readers not capable of hotplug
	    -	debug level
	    -	...
	 */
	while (!ateof()) {
		if ((rc = get_string(kwd, sizeof(kwd))) < 0)
			break;
		if (!strcasecmp(kwd, "reader")) {
			rc = parse_reader();
		} else if (!strcasecmp(kwd, "hotplug")) {
			rc = parse_hotplug();
		} else if (!strcasecmp(kwd, "debug")) {
			rc = get_integer(&ct_config.debug);
			if (rc >= 0)
				rc = parse_expect(";");
		} else {
			rc = err_unexpected_keyword(kwd, NULL);
		}

		if (rc < 0)
			break;
	}

	close(config_fd);
	config_fd = -1;

	return rc;
}

/*
 * Parse static reader config
 * "reader <driver> <device> [options];"
 */
int
parse_reader(void)
{
	ifd_reader_t	*reader;
	char		driver[32], device[128], *tok;

	if (get_string(driver, sizeof(driver)) < 0
	 || get_string(device, sizeof(device)) < 0)
		return -1;
	
	while (1) {
		if (get_token(&tok) < 0)
			return -1;
		if (*tok == ';')
			break;
		return err_unexpected_keyword(tok, NULL);
	}

	if ((reader = ifd_open(driver, device)) != 0) {
		ifd_attach(reader);
	}

	return 0;
}

/*
 * Parse hotplug ID
 * "hotplug <driver> id1 id2 id3 ...;"
 */
int
parse_hotplug(void)
{
	char		driver[32], *tok;

	if (get_string(driver, sizeof(driver)) < 0)
		return -1;

	while (1) {
		if (get_token(&tok) < 0)
			return -1;
		if (*tok == ';')
			break;
		ifd_driver_add_id(tok, driver);
	}
	return 0;
}

/*
 * Check that the next token is indeed what we expect
 */
int
parse_expect(const char *expect)
{
	char	*tok;

	if (get_token(&tok) < 0)
		return -1;
	if (strcasecmp(tok, expect)) {
		err_unexpected_keyword(tok, expect);
		return -1;
	}
	return 0;
}

/*
 * Helper functions to get various tokens
 */
int
get_string(char *buf, size_t size)
{
	char	*tok;

	if (get_token(&tok) < 0)
		return -1;
	if (strlen(tok) >= size)
		tok[size-1] = '\0';
	strcpy(buf, tok);
	return 0;
}

int
get_integer(int *value)
{
	char	*tok, *end;

	if (get_token(&tok) < 0)
		return -1;
	*value = strtol(tok, &end, 0);
	if (*end) {
		ct_error("%s: line %d: "
			  "expected integer, got \"%s\"",
			  tok);
		return -1;
	}
	return 0;
}

/*
 * Tokenizer
 */
int
get_token(char **tok)
{
	static char	buffer[512];
	unsigned int	m, n, copy, retry = 1;
	char		*s;

	/* consume initial white space */
	if (skipws() < 0)
		return -1;

again:	s = (char *) ct_buf_head(&config_buf);
	n = ct_buf_avail(&config_buf);

	if (*s == ';') {
		m = 1;
	} else {
		for (m = 0; !isspace(s[m]) && s[m] != ';' && m < n; m++)
			;
	}

	/* If we hit the end of the buffer while scanning
	 * for white space, read more data and try 
	 * again */
	if (m >= n && retry) {
		if (ct_buf_read(&config_buf, config_fd) < 0) {
			ct_error("%s: error while reading file: %m",
					config_filename);
			return -1;
		}
		retry = 0;
		goto again;
	}

	if ((copy = m) >= sizeof(buffer))
		copy = sizeof(buffer)-1;
	memcpy(buffer, s, copy);
	buffer[copy] = '\0';
	ct_buf_get(&config_buf, NULL, m);

	if (ct_config.debug > 4)
		ct_debug("ct_config_parse: token=\"%s\"\n", buffer);
	*tok = buffer;
	return 0;
}

/*
 * Check if we're at the end of the file
 */
int
ateof(void)
{
	int	retry = 1;

again:
	if (skipws() < 0)
		return -1;

	if (ct_buf_avail(&config_buf) == 0) {
		if (!retry)
			return 1;

		if (ct_buf_read(&config_buf, config_fd) < 0) {
			ct_error("%s: error while reading file: %m",
					config_filename);
			return -1;
		}
		retry = 0;
		goto again;
	}

	return 0;
}

/*
 * Eat initial white space from buffer
 */
int
skipws(void)
{
	unsigned int	m, n, in_comment = 0;
	char		*s;

again:
	s = (char *) ct_buf_head(&config_buf);
	n = ct_buf_avail(&config_buf);

	for (m = 0; m < n; m++, s++) {
		if (*s == '#') {
			in_comment = 1;
		} else if (!in_comment && !isspace(*s)) {
			break;
		} else if (*s == '\n') {
			in_comment = 0;
		}
	}

	ct_buf_get(&config_buf, NULL, m);
	if (in_comment) {
		if (ct_buf_read(&config_buf, config_fd) < 0) {
			ct_error("%s: error while reading file: %m",
					config_filename);
			return -1;
		}
		goto again;
	}

	return 0;
}

/*
 * Complain about syntax errors
 */
int
err_unexpected_keyword(const char *kwd, const char *expect)
{
	ct_error("%s: line %d: unexpected keyword %s%s%s",
		config_filename, config_line, kwd,
		expect? ", expected ": "", expect? expect : "");
	return -1;
}

