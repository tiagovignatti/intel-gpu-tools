/*
 * Copyright © 2011-2015 Intel Corporation
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

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>

#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "igt_aux.h"
#include "igt_stats.h"

#define OBJECT_SIZE (1<<23)

#define LOCAL_I915_EXEC_NO_RELOC (1<<11)
#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

static double elapsed(const struct timespec *start,
                        const struct timespec *end)
{
	return (end->tv_sec - start->tv_sec) + 1e-9*(end->tv_nsec - start->tv_nsec);
}

static void make_busy(int fd, uint32_t handle) 
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec;

	const uint32_t buf[] = {MI_BATCH_BUFFER_END};
	gem_write(fd, handle, 0, buf, sizeof(buf));

	memset(&gem_exec, 0, sizeof(gem_exec));
	gem_exec.handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&gem_exec;
	execbuf.buffer_count = 1;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	if (__gem_execbuf(fd, &execbuf)) {
		execbuf.flags = 0;
		gem_execbuf(fd, &execbuf);
	}
}

int main(int argc, char **argv)
{
	int fd = drm_open_driver(DRIVER_INTEL);
	int size = 0;
	int busy = 0;
	int reps = 13;
	int c, n, s;

	while ((c = getopt (argc, argv, "bs:r:")) != -1) {
		switch (c) {
		case 's':
			size = atoi(optarg);
			break;

		case 'r':
			reps = atoi(optarg);
			if (reps < 1)
				reps = 1;
			break;

		case 'b':
			busy = true;
			break;

		default:
			break;
		}
	}

	if (size == 0) {
		for (s = 4096; s <=  OBJECT_SIZE; s <<= 1) {
			igt_stats_t stats;

			igt_stats_init_with_size(&stats, reps);
			for (n = 0; n < reps; n++) {
				struct timespec start, end;
				uint64_t count = 0;

				clock_gettime(CLOCK_MONOTONIC, &start);
				do {
					for (c = 0; c < 1000; c++) {
						uint32_t handle;

						handle = gem_create(fd, s);
						gem_set_domain(fd, handle,
							       I915_GEM_DOMAIN_GTT,
							       I915_GEM_DOMAIN_GTT);
						if (busy)
							make_busy(fd, handle);
						gem_close(fd, handle);
					}
					count += c;
					clock_gettime(CLOCK_MONOTONIC, &end);
				} while (end.tv_sec - start.tv_sec < 2);

				igt_stats_push_float(&stats, count / elapsed(&start, &end));
			}
			printf("%f\n", igt_stats_get_trimean(&stats));
			igt_stats_fini(&stats);
		}
	} else {
		for (n = 0; n < reps; n++) {
			struct timespec start, end;
			uint64_t count = 0;

			clock_gettime(CLOCK_MONOTONIC, &start);
			do {
				for (c = 0; c < 1000; c++) {
					uint32_t handle;

					handle = gem_create(fd, size);
					gem_set_domain(fd, handle,
						       I915_GEM_DOMAIN_GTT,
						       I915_GEM_DOMAIN_GTT);
					if (busy)
						make_busy(fd, handle);
					gem_close(fd, handle);
				}
				count += c;
				clock_gettime(CLOCK_MONOTONIC, &end);
			} while (end.tv_sec - start.tv_sec < 2);

			printf("%f\n", count / elapsed(&start, &end));
		}
	}

	return 0;
}
