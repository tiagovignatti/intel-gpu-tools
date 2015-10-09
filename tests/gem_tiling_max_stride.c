/*
 * Copyright © 2013 Intel Corporation
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
 *    Ville Syrjälä <ville.syrjala@linux.intel.com>
 *
 */

#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "drm.h"

IGT_TEST_DESCRIPTION("Check that max fence stride works.");

static void do_test_invalid_tiling(int fd, uint32_t handle, int tiling, int stride)
{
	igt_assert(__gem_set_tiling(fd, handle, tiling, tiling ? stride : 0) == -EINVAL);
}

static void test_invalid_tiling(int fd, uint32_t handle, int stride)
{
	do_test_invalid_tiling(fd, handle, I915_TILING_X, stride);
	do_test_invalid_tiling(fd, handle, I915_TILING_Y, stride);
}

/**
 * Testcase: Check that max fence stride works
 */

igt_simple_main
{
	int fd;
	uint32_t *ptr;
	uint32_t *data;
	uint32_t handle;
	uint32_t stride;
	uint32_t size;
	uint32_t devid;
	int i = 0, x, y;
	int tile_width = 512;
	int tile_height = 8;

	fd = drm_open_driver(DRIVER_INTEL);

	devid = intel_get_drm_devid(fd);

	if (intel_gen(devid) >= 7)
		stride = 256 * 1024;
	else if (intel_gen(devid) >= 4)
		stride = 128 * 1024;
	else {
		if (IS_GEN2(devid)) {
			tile_width = 128;
			tile_height = 16;
		}
		stride = 8 * 1024;
	}

	size = stride * tile_height;

	data = malloc(size);
	igt_assert(data);

	/* Fill each line with the line number */
	for (y = 0; y < tile_height; y++) {
		for (x = 0; x < stride / 4; x++)
			data[i++] = y;
	}

	handle = gem_create(fd, size);

	ptr = gem_mmap__gtt(fd, handle, size, PROT_READ | PROT_WRITE);

	test_invalid_tiling(fd, handle, 0);
	test_invalid_tiling(fd, handle, 64);
	test_invalid_tiling(fd, handle, stride - 1);
	test_invalid_tiling(fd, handle, stride + 1);
	test_invalid_tiling(fd, handle, stride + 127);
	test_invalid_tiling(fd, handle, stride + 128);
	test_invalid_tiling(fd, handle, stride + tile_width - 1);
	test_invalid_tiling(fd, handle, stride + tile_width);
	test_invalid_tiling(fd, handle, stride * 2);
	test_invalid_tiling(fd, handle, INT_MAX);
	test_invalid_tiling(fd, handle, UINT_MAX);

	gem_set_tiling(fd, handle, I915_TILING_X, stride);

	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);

	memcpy(ptr, data, size);

	gem_set_tiling(fd, handle, I915_TILING_NONE, 0);

	memcpy(data, ptr, size);

	/* Check that each tile contains the expected pattern */
	for (i = 0; i < size / 4; ) {
		for (y = 0; y < tile_height; y++) {
			for (x = 0; x < tile_width / 4; x++) {
				igt_assert(y == data[i]);
				i++;
			}
		}
	}

	munmap(ptr, size);

	close(fd);
}
