/*
 * Copyright © 2010 Intel Corporation
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
 *    Eric Anholt <eric@anholt.net>
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"

#define OBJECT_SIZE 16384

static double elapsed(const struct timeval *start,
		      const struct timeval *end,
		      int loop)
{
	return (1e6*(end->tv_sec - start->tv_sec) + (end->tv_usec - start->tv_usec))/loop;
}

int main(int argc, char **argv)
{
	struct timeval start, end;
	uint8_t *buf;
	uint32_t handle;
	int size = OBJECT_SIZE;
	int loop, i, tiling;
	int fd;

	if (argc > 1)
		size = atoi(argv[1]);
	if (size == 0) {
		fprintf(stderr, "Invalid object size specified\n");
		return 1;
	}

	buf = malloc(size);
	memset(buf, 0, size);
	fd = drm_open_any();

	handle = gem_create(fd, size);
	assert(handle);

	for (tiling = I915_TILING_NONE; tiling <= I915_TILING_Y; tiling++) {
		if (tiling != I915_TILING_NONE) {
			printf("\nSetting tiling mode to %s\n",
			       tiling == I915_TILING_X ? "X" : "Y");
			gem_set_tiling(fd, handle, tiling, 512);
		}

		if (tiling == I915_TILING_NONE) {
			/* CPU pwrite */
			gettimeofday(&start, NULL);
			for (loop = 0; loop < 1000; loop++)
				gem_write(fd, handle, 0, buf, size);
			gettimeofday(&end, NULL);
			printf("Time to pwrite %dk through the CPU:		%7.3fµs\n",
			       size/1024, elapsed(&start, &end, loop));

			/* CPU pread */
			gettimeofday(&start, NULL);
			for (loop = 0; loop < 1000; loop++)
				gem_read(fd, handle, 0, buf, size);
			gettimeofday(&end, NULL);
			printf("Time to pread %dk through the CPU:		%7.3fµs\n",
			       size/1024, elapsed(&start, &end, loop));
		}

		/* prefault into gtt */
		{
			uint32_t *ptr = gem_mmap(fd, handle, size, PROT_READ);
			int x = 0;

			for (i = 0; i < size/sizeof(*ptr); i++)
				x += ptr[i];

			munmap(ptr, size);
		}
		/* mmap read */
		gettimeofday(&start, NULL);
		for (loop = 0; loop < 1000; loop++) {
			uint32_t *ptr = gem_mmap(fd, handle, size, PROT_READ);
			int x = 0;

			for (i = 0; i < size/sizeof(*ptr); i++)
				x += ptr[i];

			munmap(ptr, size);
		}
		gettimeofday(&end, NULL);
		printf("Time to read %dk through a GTT map:		%7.3fµs\n",
		       size/1024, elapsed(&start, &end, loop));

		/* mmap write */
		gettimeofday(&start, NULL);
		for (loop = 0; loop < 1000; loop++) {
			uint32_t *ptr = gem_mmap(fd, handle, size, PROT_READ | PROT_WRITE);

			for (i = 0; i < size/sizeof(*ptr); i++)
				ptr[i] = i;

			munmap(ptr, size);
		}
		gettimeofday(&end, NULL);
		printf("Time to write %dk through a GTT map:		%7.3fµs\n",
		       size/1024, elapsed(&start, &end, loop));

		/* mmap read */
		gettimeofday(&start, NULL);
		for (loop = 0; loop < 1000; loop++) {
			uint32_t *ptr = gem_mmap(fd, handle, size, PROT_READ);
			int x = 0;

			for (i = 0; i < size/sizeof(*ptr); i++)
				x += ptr[i];

			munmap(ptr, size);
		}
		gettimeofday(&end, NULL);
		printf("Time to read %dk (again) through a GTT map:	%7.3fµs\n",
		       size/1024, elapsed(&start, &end, loop));

		if (tiling == I915_TILING_NONE) {
			/* GTT pwrite */
			gettimeofday(&start, NULL);
			for (loop = 0; loop < 1000; loop++)
				gem_write(fd, handle, 0, buf, size);
			gettimeofday(&end, NULL);
			printf("Time to pwrite %dk through the GTT:		%7.3fµs\n",
			       size/1024, elapsed(&start, &end, loop));

			/* GTT pread */
			gettimeofday(&start, NULL);
			for (loop = 0; loop < 1000; loop++)
				gem_read(fd, handle, 0, buf, size);
			gettimeofday(&end, NULL);
			printf("Time to pread %dk through the GTT:		%7.3fµs\n",
			       size/1024, elapsed(&start, &end, loop));

			/* GTT pwrite, including clflush */
			gettimeofday(&start, NULL);
			for (loop = 0; loop < 1000; loop++) {
				gem_write(fd, handle, 0, buf, size);
				gem_sync(fd, handle);
			}
			gettimeofday(&end, NULL);
			printf("Time to pwrite %dk through the GTT (clflush):	%7.3fµs\n",
			       size/1024, elapsed(&start, &end, loop));

			/* GTT pread, including clflush */
			gettimeofday(&start, NULL);
			for (loop = 0; loop < 1000; loop++) {
				gem_sync(fd, handle);
				gem_read(fd, handle, 0, buf, size);
			}
			gettimeofday(&end, NULL);
			printf("Time to pread %dk through the GTT (clflush):	%7.3fµs\n",
			       size/1024, elapsed(&start, &end, loop));

			/* partial writes */
			printf("Now partial writes.\n");
			size /= 4;

			/* partial GTT pwrite, including clflush */
			gettimeofday(&start, NULL);
			for (loop = 0; loop < 1000; loop++) {
				gem_write(fd, handle, 0, buf, size);
				gem_sync(fd, handle);
			}
			gettimeofday(&end, NULL);
			printf("Time to pwrite %dk through the GTT (clflush):	%7.3fµs\n",
			       size/1024, elapsed(&start, &end, loop));

			/* partial GTT pread, including clflush */
			gettimeofday(&start, NULL);
			for (loop = 0; loop < 1000; loop++) {
				gem_sync(fd, handle);
				gem_read(fd, handle, 0, buf, size);
			}
			gettimeofday(&end, NULL);
			printf("Time to pread %dk through the GTT (clflush):	%7.3fµs\n",
			       size/1024, elapsed(&start, &end, loop));
		}

	}

	gem_close(fd, handle);
	close(fd);

	return 0;
}
