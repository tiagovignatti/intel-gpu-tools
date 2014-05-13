/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <fcntl.h>
#include <limits.h>

#include "drmtest.h"
#include "ioctl_wrappers.h"
#include "igt_aux.h"


#define FD_ARR_SZ 100
int fd_arr[FD_ARR_SZ];

igt_simple_main
{
	int fd, i;
	struct rlimit rlim;
	unsigned nofile_rlim;
	FILE *file_max = fopen("/proc/sys/fs/file-max", "r");
	igt_assert(fscanf(file_max, "%u", &nofile_rlim) == 1);
	fclose(file_max);

	igt_info("System limit for open files is %u\n", nofile_rlim);

	igt_assert(getrlimit(RLIMIT_NOFILE, &rlim) == 0);
	rlim.rlim_cur = nofile_rlim;
	rlim.rlim_max = nofile_rlim;
	igt_assert(setrlimit(RLIMIT_NOFILE, &rlim) == 0);

	fd = drm_open_any();

	igt_assert(open("/dev/null", O_RDONLY) >= 0);

	igt_fork(n, 1) {
		igt_drop_root();

		for (i = 0; ; i++) {
			int tmp_fd = open("/dev/null", O_RDONLY);
			uint32_t handle;

			if (tmp_fd >= 0 && i < FD_ARR_SZ)
				fd_arr[i] = tmp_fd;

			handle = __gem_create(fd, 4096);
			if (handle)
				gem_close(fd, handle);


			if (tmp_fd < 0) {
				/* Ensure we actually hit the failure path ... */
				igt_assert(handle == 0);
				igt_info("fd exhaustion after %i rounds.\n", i);
				break;
			}
		}

		/* The child will free all the fds when exiting, so no need to
		 * clean up to mess to ensure that the parent can at least run
		 * the exit handlers. */
	}

	igt_waitchildren();

	close(fd);
}
