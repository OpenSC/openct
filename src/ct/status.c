/*
 * Shared status file for OpenSC readers
 *
 * Copyright (C) 2003 Olaf Kirch <okir@suse.de>
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <openct/openct.h>
#include <openct/pathnames.h>
#include <openct/logging.h>

static const ct_info_t *ct_reader_info;
static unsigned int	ct_num_info;

static void *
ct_map_status(int flags, size_t *size)
{
	const char	*path = OPENCT_STATUS_PATH;
	struct stat	stb;
	int		fd, prot;
	void		*addr;

	if ((fd = open(path, flags)) < 0) {
		ct_error("unable to open %s: %m", path);
		return NULL;
	}

	if (fstat(fd, &stb) < 0) {
		ct_error("unable to stat %s: %m", path);
		goto done;
	}
	*size = stb.st_size;

	prot = PROT_READ;
	if ((flags & O_ACCMODE) == O_RDWR)
		prot |= PROT_WRITE;
	
	addr = mmap(0, *size, prot, MAP_SHARED, fd, 0);

done:	close(fd);
	return addr;
}

int
ct_status_clear(unsigned int count)
{
	int	fd;

	unlink(OPENCT_STATUS_PATH);
	if ((fd = open(OPENCT_STATUS_PATH, O_RDWR|O_CREAT, 0644)) < 0
	 || ftruncate(fd, count * sizeof(ct_info_t)) < 0
	 || fchmod(fd, 0644) < 0) {
		ct_error("cannot create %s: %m", OPENCT_STATUS_PATH);
		unlink(OPENCT_STATUS_PATH);
		if (fd >= 0)
			close(fd);
		return -1;
	}

	return 0;
}

int
ct_status(const ct_info_t **result)
{
	if (ct_reader_info == NULL) {
		size_t	size;

		ct_reader_info = (ct_info_t *) ct_map_status(O_RDONLY, &size);
		if (ct_reader_info == NULL)
			return -1;
		ct_num_info = size / sizeof(ct_info_t);
	}

	*result = ct_reader_info;
	return ct_num_info;
}

ct_info_t *
ct_status_alloc_slot(unsigned int *num)
{
	ct_info_t	*info;
	size_t		size;
	unsigned int	n, max;

	info = (ct_info_t *) ct_map_status(O_RDWR, &size);
	if (info == NULL)
		return NULL;

	max = size / sizeof(ct_info_t);
	if (*num == (unsigned int) -1) {
		/* find a free slot */
		for (n = 0; n < max; n++, info++) {
			if (info[n].ct_pid == 0
			 || (kill(info[n].ct_pid, 0) < 0 && errno == ESRCH)) {
				*num = n;
				break;
			}
		}
	} else if (*num >= max) {
		munmap(info, size);
		return NULL;
	}

	memset(&info[*num], 0, sizeof(ct_info_t));
	info[*num].ct_pid = getpid();
	return info + *num;
}

int
ct_status_update(ct_info_t *status)
{
	if (msync(status, sizeof(*status), MS_SYNC) < 0) {
		ct_error("msync: %m");
		return -1;
	}

	return 0;
}
