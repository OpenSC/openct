/*
 * Utility functions
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 */

#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

unsigned int
ifd_count_bits(unsigned int word)
{
	static unsigned int bcount[16] = {
		0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4
	};
	unsigned int num;

	for (num = 0; word; word >>= 4)
		num += bcount[word & 0xF];
	return num;
}

void
ifd_revert_bits(unsigned char *data, size_t len)
{
	unsigned char j, k, c, d;

	while (len--) {
		c = *data;
		for (j = 1, k = 0x80, d = 0; k != 0; j <<= 1, k >>= 1) {
			if (c & j)
				d |= k;
		}
		*data++ = d ^ 0xFF;
	}
}

#ifndef timersub
# define timersub(a, b, result)						      \
  do {									      \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;			      \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;			      \
    if ((result)->tv_usec < 0) {					      \
      --(result)->tv_sec;						      \
      (result)->tv_usec += 1000000;					      \
    }									      \
  } while (0)
#endif

long
ifd_time_elapsed(struct timeval *then)
{
	struct timeval	now, delta;

	gettimeofday(&now, NULL);
	timersub(&now, then, &delta);
	return delta.tv_sec * 1000 + (delta.tv_usec % 1000);
}

/*
 * Spawn an ifdhandler
 */
int
ifd_spawn_handler(const char *driver, const char *device, int idx)
{
	const char	*argv[16];
	char		reader[16], debug[10];
	int		argc, n;
	pid_t		pid;

	ifd_debug(1, "driver=%s, device=%s, index=%d",
			driver, device, idx);

	if ((pid = fork()) < 0) {
		ct_error("fork failed: %m");
		return 0;
	}

	if (pid != 0) {
		/* We're the parent process. The child process should
		 * call daemon(), causing the process to exit
		 * immediately after allocating a slot in the status
		 * file. We wait for it here to make sure USB devices
		 * don't claim a slot reserved for another device */
		waitpid(pid, NULL, 0);
		return 1;
	}

	argc = 0;
	argv[argc++] = ct_config.ifdhandler;

	if (idx >= 0) {
		snprintf(reader, sizeof(reader), "-r%u", idx);
		argv[argc++] = reader;
	} else {
		argv[argc++] = "-H";
	}

	if (ct_config.debug) {
		if ((n = ct_config.debug) > 6)
			n = 6;
		debug[n+1] = '\0';
		while (n--)
			debug[n+1] = 'd';
		debug[0] = '-';
		argv[argc++] = debug;
	}

	argv[argc++] = driver;
	if (device)
		argv[argc++] = device;
	argv[argc] = NULL;

	n = getdtablesize();
	while (--n > 2)
		close(n);

	execv(ct_config.ifdhandler, (char **) argv);
	ct_error("failed to execute %s: %m", ct_config.ifdhandler);
	exit(1);
}
