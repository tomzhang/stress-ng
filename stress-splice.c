/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_SPLICE)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

static size_t opt_splice_bytes = DEFAULT_SPLICE_BYTES;
static bool set_splice_bytes = false;

void stress_set_splice_bytes(const char *optarg)
{
	set_splice_bytes = true;
	opt_splice_bytes = (size_t)get_uint64_byte(optarg);
	check_range("splice-bytes", opt_splice_bytes,
		MIN_SPLICE_BYTES, MAX_SPLICE_BYTES);
}

/*
 *  stress_splice
 *	stress copying of /dev/zero to /dev/null
 */
int stress_splice(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fd_in, fd_out, fds[2];

	if (!set_splice_bytes) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_splice_bytes = MAX_SPLICE_BYTES;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_splice_bytes = MIN_SPLICE_BYTES;
	}

	(void)instance;

	if (pipe(fds) < 0) {
		pr_failed_err(name, "pipe");
		return EXIT_FAILURE;
	}

	if ((fd_in = open("/dev/zero", O_RDONLY)) < 0) {
		(void)close(fds[0]);
		(void)close(fds[1]);
		pr_failed_err(name, "open");
		return EXIT_FAILURE;
	}
	if ((fd_out = open("/dev/null", O_WRONLY)) < 0) {
		(void)close(fd_in);
		(void)close(fds[0]);
		(void)close(fds[1]);
		pr_failed_err(name, "open");
		return EXIT_FAILURE;
	}

	do {
		int ret;

		ssize_t bytes;

		bytes = splice(fd_in, NULL, fds[1], NULL, opt_splice_bytes, SPLICE_F_MOVE);
		if (bytes < 0)
			break;

		ret = splice(fds[0], NULL, fd_out, NULL, opt_splice_bytes, SPLICE_F_MOVE);
		if (ret < 0)
			break;

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
	(void)close(fd_out);
	(void)close(fd_in);
	(void)close(fds[0]);
	(void)close(fds[1]);

	return EXIT_SUCCESS;
}

#endif
