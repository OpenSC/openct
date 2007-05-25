/*
 * libifd configuration handling
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>

#include <openct/conf.h>
#include <openct/buffer.h>

struct ct_config ct_config = {
	0,			/* debug */
	1,			/* autoload */
	1,			/* hotplug */
	0,			/* suppress_errors */
	OPENCT_IFDHANDLER_PATH,	/* ifdhandler */
	OPENCT_MODULES_PATH,	/* modules_dir */
	NULL,			/* driver_modules_dir */
	NULL,			/* protocol_modules_dir */
	OPENCT_SOCKET_PATH,	/* socket_dir */
};

#define issepa(c)	(strchr("=;,{}", (c)) != NULL)

enum {
	GROUP_BEGIN = '{',
	GROUP_END = '}',
	COMMA = ',',
	SEMICOLON = ';',
	EQUALS = '=',
	END_OF_FILE = -1
};

static const char *config_filename = NULL;
static ct_buf_t config_buf;
static int config_fd = -1;
static int config_line = 0;
static ifd_conf_node_t config_top;

static int conf_parse_group(ifd_conf_node_t *, char);
static void conf_dump(ifd_conf_node_t *, int);
static ifd_conf_node_t *conf_add_node(ifd_conf_node_t *, const char *);

static int get_token(char **);
static int skipws(void);
static int ateof(void);

/*
 * Parse the ifd config file
 */
int ifd_config_parse(const char *filename)
{
	char buffer[512];
	int rc;

	if ((config_filename = filename) == NULL)
		config_filename = OPENCT_CONF_PATH;

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

	config_top.name = "<config>";
	rc = conf_parse_group(&config_top, END_OF_FILE);

	close(config_fd);
	config_fd = -1;

	if (ct_config.debug > 2)
		conf_dump(&config_top, 0);

	return rc;
}

/*
 * Parse list of statements
 */
static int conf_parse_group(ifd_conf_node_t * group, char closing)
{
	ifd_conf_node_t *node;
	char *token;
	int rc = 0;

	while (1) {
		if (ateof()) {
			if (closing == (char)END_OF_FILE)
				break;
			ct_error("%s:%u: unexpected end of file",
				 config_filename, config_line);
			return -1;
		}

		if ((rc = get_token(&token)) < 0)
			break;

		/* Check if this is the closing group character; if
		 * so, return. No other separators allowed here */
		if (*token == closing)
			break;
		if (issepa(*token))
			goto unexpected;

		node = conf_add_node(group, token);

		if ((rc = get_token(&token)) < 0)
			break;

		/* Get the value - the following are valid
		 *   name = value;
		 *   name value { ... };
		 *   name { ... };
		 *   value, value, ...
		 */
		if (*token == EQUALS) {
			/* name = value case */
			if ((rc = get_token(&token)) < 0)
				break;
		}

		if (!issepa(*token)) {
			node->value = strdup(token);

			/* Get the next token */
			if ((rc = get_token(&token)) < 0)
				break;
		} else if (*token == GROUP_BEGIN || *token == COMMA) {
			/* Do-nothing cases:
			 *      name { ... }
			 *      foo, bar, baz, ...
			 */
		} else {
			/* everything else illegal here */
			goto unexpected;
		}

		if (*token == GROUP_BEGIN) {
			/* Parse the group, then get the next
			 * token */
			if ((rc = conf_parse_group(node, GROUP_END)) < 0
			    || (rc = get_token(&token)) < 0)
				break;
		}

		if (*token != SEMICOLON && *token != COMMA)
			goto unexpected;
	}

	return rc;

      unexpected:
	ct_error("%s: line %d: unexpected token \"%s\"",
		 config_filename, config_line, token);
	return -1;
}

/*
 * Debugging - dump the config tree
 */
static void conf_dump(ifd_conf_node_t * node, int indent)
{
	for (; node; node = node->next) {
		printf("%*.*s%s", indent, indent, "", node->name);
		if (node->value) {
			if (!node->children)
				printf(" =");
			printf(" %s", node->value);
		}
		if (node->children) {
			printf(" %c\n", GROUP_BEGIN);
			conf_dump(node->children, indent + 2);
			printf("%*.*s%c", indent, indent, "", GROUP_END);
		} else {
			printf("%c", SEMICOLON);
		}
		printf("\n");
	}
}

/*
 * Config node handling
 */
static ifd_conf_node_t *conf_add_node(ifd_conf_node_t * parent,
				      const char *name)
{
	ifd_conf_node_t **p, *node;

	node = (ifd_conf_node_t *) calloc(1, sizeof(*node));
	if (!node) {
		ct_error("out of memory");
		return NULL;
	}
	node->name = strdup(name);

	for (p = &parent->children; *p; p = &(*p)->next) ;
	*p = node;

	return node;
}

static ifd_conf_node_t *conf_find_node(ifd_conf_node_t * node, const char *name)
{
	unsigned int len;

	if (!name)
		return node;
	while (*name == '.')
		name++;
	if (!*name)
		return node;

	len = strcspn(name, ".");

	for (node = node->children; node; node = node->next) {
		if (!strncmp(node->name, name, len)
		    && node->name[len] == '\0')
			return conf_find_node(node, name + len);
	}

	return NULL;
}

int ifd_conf_get_string(const char *name, char **result)
{
	return ifd_conf_node_get_string(&config_top, name, result);
}

int ifd_conf_get_bool(const char *name, unsigned int *result)
{
	return ifd_conf_node_get_bool(&config_top, name, result);
}

int ifd_conf_get_integer(const char *name, unsigned int *result)
{
	return ifd_conf_node_get_integer(&config_top, name, result);
}

int ifd_conf_get_string_list(const char *name, char **list, size_t max)
{
	return ifd_conf_node_get_string_list(&config_top, name, list, max);
}

int ifd_conf_get_nodes(const char *name, ifd_conf_node_t ** list, size_t max)
{
	return ifd_conf_node_get_nodes(&config_top, name, list, max);
}

int
ifd_conf_node_get_string(ifd_conf_node_t * node,
			 const char *name, char **result)
{
	if (!(node = conf_find_node(node, name))
	    || !node->value)
		return -1;

	*result = node->value;
	return 0;
}

int
ifd_conf_node_get_integer(ifd_conf_node_t * node,
			  const char *name, unsigned int *result)
{
	if (!(node = conf_find_node(node, name))
	    || !node->value)
		return -1;

	*result = strtoul(node->value, NULL, 0);
	return 0;
}

int
ifd_conf_node_get_bool(ifd_conf_node_t * node,
		       const char *name, unsigned int *result)
{
	const char *v;

	if (!(node = conf_find_node(node, name))
	    || !(v = node->value))
		return -1;

	if (!strcmp(v, "0")
	    || !strcmp(v, "off")
	    || !strcmp(v, "no")) {
		*result = 0;
	} else if (!strcmp(v, "1")
		   || !strcmp(v, "on")
		   || !strcmp(v, "yes")) {
		*result = 1;
	} else {
		return -1;
	}

	return 0;
}

int
ifd_conf_node_get_string_list(ifd_conf_node_t * node,
			      const char *name, char **list, size_t max)
{
	unsigned int j = 0;

	if (!(node = conf_find_node(node, name)))
		return -1;

	for (node = node->children; node; node = node->next) {
		if (list && j < max)
			list[j] = node->name;
		j += 1;
	}

	return j;
}

int
ifd_conf_node_get_nodes(ifd_conf_node_t * node,
			const char *name, ifd_conf_node_t ** list, size_t max)
{
	unsigned int j = 0;

	for (node = node->children; node; node = node->next) {
		if (strcmp(node->name, name))
			continue;
		if (list && j < max)
			list[j] = node;
		j += 1;
	}

	return j;
}

/*
 * Tokenizer
 */
static int get_token(char **tok)
{
	static char buffer[512];
	unsigned int m, n, copy, retry = 1;
	char *s;

	/* consume initial white space */
	if (skipws() < 0)
		return -1;

      again:
	s = (char *)ct_buf_head(&config_buf);
	n = ct_buf_avail(&config_buf);

	if (n && issepa(*s)) {
		m = 1;
	} else {
		for (m = 0; !isspace((int)s[m]) && !issepa(s[m]) && m < n;
		     m++) ;
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

	if (m == 0)
		return -1;

	if ((copy = m) >= sizeof(buffer))
		copy = sizeof(buffer) - 1;
	memcpy(buffer, s, copy);
	buffer[copy] = '\0';
	ct_buf_get(&config_buf, NULL, m);

	ifd_debug(5, "ifd_config_parse: token=\"%s\"", buffer);

	*tok = buffer;
	return 0;
}

/*
 * Check if we're at the end of the file
 */
static int ateof(void)
{
	int retry = 1;

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
static int skipws(void)
{
	unsigned int m, n, in_comment = 0;
	char *s;

      again:
	s = (char *)ct_buf_head(&config_buf);
	n = ct_buf_avail(&config_buf);

	for (m = 0; m < n; m++, s++) {
		if (*s == '#') {
			in_comment = 1;
		} else if (!in_comment && !isspace((int)*s)) {
			break;
		} else if (*s == '\n') {
			config_line++;
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
