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

static uint32_t gem_create(int fd, int size)
{
	struct drm_i915_gem_create create;

	create.handle = 0;
	create.size = size;
	(void)drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);

	return create.handle;
}

static void *gem_mmap(int fd, uint32_t handle, int size, int prot)
{
	struct drm_i915_gem_mmap_gtt mmap_arg;
	void *ptr;

	mmap_arg.handle = handle;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_arg))
		return NULL;

	ptr = mmap(0, size, prot, MAP_SHARED, fd, mmap_arg.offset);
	if (ptr == MAP_FAILED)
		ptr = NULL;

	return ptr;
}

static int gem_write(int fd,
		     uint32_t handle, uint32_t offset,
		     const void *src, int length)
{
	struct drm_i915_gem_pwrite pwrite;

	pwrite.handle = handle;
	pwrite.offset = offset;
	pwrite.size = length;
	pwrite.data_ptr = (uintptr_t)src;
	return drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &pwrite);
}

static int gem_read(int fd,
		    uint32_t handle, uint32_t offset,
		    const void *src, int length)
{
	struct drm_i915_gem_pread pread;

	pread.handle = handle;
	pread.offset = offset;
	pread.size = length;
	pread.data_ptr = (uintptr_t)src;
	return drmIoctl(fd, DRM_IOCTL_I915_GEM_PREAD, &pread);
}

static void gem_set_tiling(int fd, uint32_t handle, int tiling)
{
	struct drm_i915_gem_set_tiling set_tiling;
	int ret;

	do {
		set_tiling.handle = handle;
		set_tiling.tiling_mode = tiling;
		set_tiling.stride = 512;

		ret = ioctl(fd, DRM_IOCTL_I915_GEM_SET_TILING, &set_tiling);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
}

static void gem_close(int fd, uint32_t handle)
{
	struct drm_gem_close close;

	close.handle = handle;
	(void)drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &close);
}

static double elapsed(const struct timeval *start,
		      const struct timeval *end,
		      int loop)
{
	return (1e6*(end->tv_sec - start->tv_sec) + (end->tv_usec - start->tv_usec))/loop;
}

int main(int argc, char **argv)
{
	struct timeval start, end;
	uint8_t buf[OBJECT_SIZE];
	uint32_t handle;
	int loop, i, tiling;
	int fd;

	fd = drm_open_any();

	handle = gem_create(fd, OBJECT_SIZE);
	assert(handle);

	for (tiling = I915_TILING_NONE; tiling <= I915_TILING_Y; tiling++) {
		if (tiling != I915_TILING_NONE) {
			printf("\nSetting tiling mode to %s\n",
			       tiling == I915_TILING_X ? "X" : "Y");
			gem_set_tiling(fd, handle, tiling);
		}

		if (tiling == I915_TILING_NONE) {
			/* CPU pwrite */
			gettimeofday(&start, NULL);
			for (loop = 0; loop < 1000; loop++)
				gem_write(fd, handle, 0, buf, sizeof(buf));
			gettimeofday(&end, NULL);
			printf("Time to pwrite 16k through the CPU:		%7.3fµs\n",
			       elapsed(&start, &end, loop));

			/* CPU pread */
			gettimeofday(&start, NULL);
			for (loop = 0; loop < 1000; loop++)
				gem_read(fd, handle, 0, buf, sizeof(buf));
			gettimeofday(&end, NULL);
			printf("Time to pread 16k through the CPU:		%7.3fµs\n",
			       elapsed(&start, &end, loop));
		}

		/* mmap read */
		gettimeofday(&start, NULL);
		for (loop = 0; loop < 1000; loop++) {
			volatile uint32_t *ptr = gem_mmap(fd, handle, OBJECT_SIZE, PROT_READ);
			int x = 0;

			for (i = 0; i < OBJECT_SIZE/sizeof(*ptr); i++)
				x += ptr[i];

			munmap((void *)ptr, OBJECT_SIZE);
		}
		gettimeofday(&end, NULL);
		printf("Time to read 16k through a GTT map:		%7.3fµs\n",
		       elapsed(&start, &end, loop));

		/* mmap write */
		gettimeofday(&start, NULL);
		for (loop = 0; loop < 1000; loop++) {
			volatile uint32_t *ptr = gem_mmap(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);

			for (i = 0; i < OBJECT_SIZE/sizeof(*ptr); i++)
				ptr[i] = i;

			munmap((void *)ptr, OBJECT_SIZE);
		}
		gettimeofday(&end, NULL);
		printf("Time to write 16k through a GTT map:		%7.3fµs\n",
		       elapsed(&start, &end, loop));

		/* mmap read */
		gettimeofday(&start, NULL);
		for (loop = 0; loop < 1000; loop++) {
			volatile uint32_t *ptr = gem_mmap(fd, handle, OBJECT_SIZE, PROT_READ);
			int x = 0;

			for (i = 0; i < OBJECT_SIZE/sizeof(*ptr); i++)
				x += ptr[i];

			munmap((void *)ptr, OBJECT_SIZE);
		}
		gettimeofday(&end, NULL);
		printf("Time to read 16k (again) through a GTT map:	%7.3fµs\n",
		       elapsed(&start, &end, loop));

		if (tiling == I915_TILING_NONE) {
			/* GTT pwrite */
			gettimeofday(&start, NULL);
			for (loop = 0; loop < 1000; loop++)
				gem_write(fd, handle, 0, buf, sizeof(buf));
			gettimeofday(&end, NULL);
			printf("Time to pwrite 16k through the GTT:		%7.3fµs\n",
			       elapsed(&start, &end, loop));

			/* GTT pread */
			gettimeofday(&start, NULL);
			for (loop = 0; loop < 1000; loop++)
				gem_read(fd, handle, 0, buf, sizeof(buf));
			gettimeofday(&end, NULL);
			printf("Time to pread 16k through the GTT:		%7.3fµs\n",
			       elapsed(&start, &end, loop));
		}

	}

	gem_close(fd, handle);
	close(fd);

	return 0;
}
