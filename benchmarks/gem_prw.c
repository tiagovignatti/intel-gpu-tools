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

static uint64_t elapsed(const struct timespec *start,
                        const struct timespec *end)
{
	return 1000000000ULL*(end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec);
}

int main(int argc, char **argv)
{
	int fd = drm_open_driver(DRIVER_INTEL);
	int domain = I915_GEM_DOMAIN_GTT;
	enum dir { READ, WRITE } dir = READ;
	void *buf = malloc(OBJECT_SIZE);
	uint32_t handle;
	int reps = 13;
	int c, size;

	while ((c = getopt (argc, argv, "D:d:r:")) != -1) {
		switch (c) {
		case 'd':
			if (strcmp(optarg, "cpu") == 0)
				domain = I915_GEM_DOMAIN_CPU;
			else if (strcmp(optarg, "gtt") == 0)
				domain = I915_GEM_DOMAIN_GTT;
			break;
		case 'D':
			if (strcmp(optarg, "read") == 0)
				dir = READ;
			else if (strcmp(optarg, "write") == 0)
				dir = WRITE;
			else
				abort();
			break;

		case 'r':
			reps = atoi(optarg);
			if (reps < 1)
				reps = 1;
			break;

		default:
			break;
		}
	}

	handle = gem_create(fd, OBJECT_SIZE);
	for (size = 1; size <= OBJECT_SIZE; size <<= 1) {
		igt_stats_t stats;
		int n;

		igt_stats_init_with_size(&stats, reps);

		for (n = 0; n < reps; n++) {
			struct timespec start, end;

			gem_set_domain(fd, handle, domain, domain);

			clock_gettime(CLOCK_MONOTONIC, &start);
			if (dir == READ)
				gem_read(fd, handle, 0, buf, size);
			else
				gem_write(fd, handle, 0, buf, size);
			clock_gettime(CLOCK_MONOTONIC, &end);

			igt_stats_push(&stats, elapsed(&start, &end));
		}

		printf("%7.3f\n", igt_stats_get_trimean(&stats)/1000);
		igt_stats_fini(&stats);
	}

	return 0;
}
