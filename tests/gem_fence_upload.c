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
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "ioctl_wrappers.h"

#define OBJECT_SIZE (1024*1024) /* restricted to 1MiB alignment on i915 fences */

static double elapsed(const struct timeval *start,
		      const struct timeval *end)
{
	return (end->tv_sec - start->tv_sec) + 1e-6*(end->tv_usec - start->tv_usec);
}

static void performance(void)
{
	int n, loop, count;
	int fd, num_fences;
	double linear[2], tiled[2];

	fd = drm_open_any();

	num_fences = gem_available_fences(fd);
	igt_require(num_fences > 0);

	for (count = 2; count < 4*num_fences; count *= 2) {
		struct timeval start, end;
		uint32_t handle[count];
		void *ptr[count];

		for (n = 0; n < count; n++) {
			handle[n] = gem_create(fd, OBJECT_SIZE);
			ptr[n] = gem_mmap(fd, handle[n], OBJECT_SIZE, PROT_READ | PROT_WRITE);
			igt_assert(ptr[n]);
		}

		gettimeofday(&start, NULL);
		for (loop = 0; loop < 1024; loop++) {
			for (n = 0; n < count; n++)
				memset(ptr[n], 0, OBJECT_SIZE);
		}
		gettimeofday(&end, NULL);

		linear[count != 2] = count * loop / elapsed(&start, &end);
		printf("Upload rate for %d linear surfaces:	%7.3fMiB/s\n",
		       count, linear[count != 2]);

		for (n = 0; n < count; n++)
			gem_set_tiling(fd, handle[n], I915_TILING_X, 1024);

		gettimeofday(&start, NULL);
		for (loop = 0; loop < 1024; loop++) {
			for (n = 0; n < count; n++)
				memset(ptr[n], 0, OBJECT_SIZE);
		}
		gettimeofday(&end, NULL);

		tiled[count != 2] = count * loop / elapsed(&start, &end);
		printf("Upload rate for %d tiled surfaces:	%7.3fMiB/s\n",
		       count, tiled[count != 2]);

		for (n = 0; n < count; n++) {
			munmap(ptr[n], OBJECT_SIZE);
			gem_close(fd, handle[n]);
		}

	}

	errno = 0;
	igt_assert(linear[1] > 0.75 * linear[0]);
	igt_assert(tiled[1] > 0.75 * tiled[0]);
}

igt_main
{
	igt_skip_on_simulation();

	igt_subtest("performance")
		performance();
}
