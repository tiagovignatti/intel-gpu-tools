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

static double elapsed(const struct timespec *start,
		const struct timespec *end)
{
	return (end->tv_sec - start->tv_sec) + 1e-9*(end->tv_nsec - start->tv_nsec);
}

int main(int argc, char **argv)
{
	int fd = drm_open_driver(DRIVER_INTEL);
	enum map {CPU, GTT, WC} map = CPU;
	enum dir {READ, WRITE, CLEAR, FAULT} dir = READ;
	int tiling = I915_TILING_NONE;
	struct timespec start, end;
	void *buf = malloc(OBJECT_SIZE);
	uint32_t handle;
	void *ptr, *src, *dst;
	int reps = 1;
	int loops;
	int c;

	while ((c = getopt (argc, argv, "m:d:r:t:")) != -1) {
		switch (c) {
		case 'm':
			if (strcmp(optarg, "cpu") == 0)
				map = CPU;
			else if (strcmp(optarg, "gtt") == 0)
				map = GTT;
			else if (strcmp(optarg, "wc") == 0)
				map = WC;
			else
				abort();
			break;

		case 'd':
			if (strcmp(optarg, "read") == 0)
				dir = READ;
			else if (strcmp(optarg, "write") == 0)
				dir = WRITE;
			else if (strcmp(optarg, "clear") == 0)
				dir = CLEAR;
			else if (strcmp(optarg, "fault") == 0)
				dir = FAULT;
			else
				abort();
			break;

		case 't':
			if (strcmp(optarg, "x") == 0)
				tiling = I915_TILING_X;
			else if (strcmp(optarg, "y") == 0)
				tiling = I915_TILING_Y;
			else if (strcmp(optarg, "none") == 0)
				tiling = I915_TILING_NONE;
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
	switch (map) {
	case CPU:
		ptr = gem_mmap__cpu(fd, handle, 0, OBJECT_SIZE, PROT_WRITE);
		gem_set_domain(fd, handle, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
		break;
	case GTT:
		ptr = gem_mmap__gtt(fd, handle, OBJECT_SIZE, PROT_WRITE);
		gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
		break;
	case WC:
		ptr = gem_mmap__wc(fd, handle, 0, OBJECT_SIZE, PROT_WRITE);
		gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
		break;
	default:
		abort();
	}

	gem_set_tiling(fd, handle, tiling, 512);

	if (dir == READ) {
		src = ptr;
		dst = buf;
	} else {
		src = buf;
		dst = ptr;
	}

	clock_gettime(CLOCK_MONOTONIC, &start);
	switch (dir) {
	case CLEAR:
	case FAULT:
		memset(dst, 0, OBJECT_SIZE);
		break;
	default:
		memcpy(dst, src, OBJECT_SIZE);
		break;
	}
	clock_gettime(CLOCK_MONOTONIC, &end);

	loops = 2 / elapsed(&start, &end);
	while (reps--) {
		clock_gettime(CLOCK_MONOTONIC, &start);
		for (c = 0; c < loops; c++) {
			int page;

			switch (dir) {
			case CLEAR:
				memset(dst, 0, OBJECT_SIZE);
				break;
			case FAULT:
				munmap(ptr, OBJECT_SIZE);
				switch (map) {
				case CPU:
					ptr = gem_mmap__cpu(fd, handle, 0, OBJECT_SIZE, PROT_WRITE);
					break;
				case GTT:
					ptr = gem_mmap__gtt(fd, handle, OBJECT_SIZE, PROT_WRITE);
					break;
				case WC:
					ptr = gem_mmap__wc(fd, handle, 0, OBJECT_SIZE, PROT_WRITE);
					break;
				default:
					abort();
				}
				for (page = 0; page < OBJECT_SIZE; page += 4096) {
					uint32_t *x = (uint32_t *)ptr + page/4;
					__asm__ __volatile__("": : :"memory");
					page += *x; /* should be zero! */
				}
				break;
			default:
				memcpy(dst, src, OBJECT_SIZE);
				break;
			}
		}
		clock_gettime(CLOCK_MONOTONIC, &end);
		printf("%7.3f\n", OBJECT_SIZE / elapsed(&start, &end) * loops / (1024*1024));
	}

	return 0;
}
