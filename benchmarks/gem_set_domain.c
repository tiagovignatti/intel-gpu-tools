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

static double elapsed(const struct timespec *start,
		      const struct timespec *end)
{
	return (end->tv_sec - start->tv_sec) + 1e-9*(end->tv_nsec - start->tv_nsec);
}

int main(int argc, char **argv)
{
	int fd = drm_open_driver(DRIVER_INTEL);
	uint32_t cpu_write = 0;
	uint32_t gtt_write = 0;
	int reps = 13;
	int size = 1024*1024;
	uint32_t handle;
	int c, n;

	while ((c = getopt (argc, argv, "c:g:r:s:")) != -1) {
		switch (c) {
		case 'c':
			cpu_write = *optarg == 'w' ? I915_GEM_DOMAIN_CPU : 0;
			break;
		case 'g':
			gtt_write = *optarg == 'w' ? I915_GEM_DOMAIN_GTT : 0;
			break;

		case 'r':
			reps = atoi(optarg);
			if (reps < 1)
				reps = 1;
			break;

		case 's':
			size = atoi(optarg);
			if (size < 4096)
				size = 4096;
			break;

		default:
			break;
		}
	}

	fprintf(stderr, "size=%d, cpu=%d, gtt=%d\n", size, cpu_write, gtt_write);

	handle = gem_create(fd, size);
	gem_set_caching(fd, handle, I915_CACHING_NONE);

	for (n = 0; n < reps; n++) {
		struct timespec start, end;
		uint64_t count = 0;

		gem_set_domain(fd, handle,
				I915_GEM_DOMAIN_CPU,
				cpu_write);

		clock_gettime(CLOCK_MONOTONIC, &start);
		do {
			for (c = 0; c < 1000; c++) {
				gem_set_domain(fd, handle,
						I915_GEM_DOMAIN_GTT,
						gtt_write);
				gem_set_domain(fd, handle,
						I915_GEM_DOMAIN_CPU,
						cpu_write);
			}
			count += c;
			clock_gettime(CLOCK_MONOTONIC, &end);
		} while (end.tv_sec - start.tv_sec < 2);

		printf("%f\n", count / elapsed(&start, &end));
	}

	return 0;
}
