/*
 * Copyright © 2012 Intel Corporation
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

#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "drm.h"

IGT_TEST_DESCRIPTION("Check set_tiling vs gtt mmap coherency.");

#define OBJECT_SIZE (1024*1024)
#define TEST_STRIDE (1024*4)

/**
 * Testcase: Check set_tiling vs gtt mmap coherency
 */

igt_simple_main
{
	int fd;
	uint32_t *ptr;
	uint32_t data[OBJECT_SIZE/4];
	int i;
	uint32_t handle;
	bool tiling_changed;
	int tile_height;

	igt_skip_on_simulation();

	fd = drm_open_driver(DRIVER_INTEL);

	if (IS_GEN2(intel_get_drm_devid(fd)))
		tile_height = 16;
	else
		tile_height = 8;

	handle = gem_create(fd, OBJECT_SIZE);
	ptr = gem_mmap__gtt(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);

	/* gtt coherency is done with set_domain in libdrm, don't break that */
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
	for (i = 0; i < OBJECT_SIZE/4; i++)
		ptr[i] = data[i] = i;

	gem_set_tiling(fd, handle, I915_TILING_X, TEST_STRIDE);

	igt_info("testing untiled->tiled\n");
	tiling_changed = false;
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, 0);
	/* Too lazy to check for the correct tiling, and impossible anyway on
	 * bit17 swizzling machines. */
	for (i = 0; i < OBJECT_SIZE/4; i++)
		if (ptr[i] != data[i])
			tiling_changed = true;
	igt_assert(tiling_changed);

	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
	for (i = 0; i < OBJECT_SIZE/4; i++)
		ptr[i] = data[i] = i;

	gem_set_tiling(fd, handle, I915_TILING_X, TEST_STRIDE*2);

	igt_info("testing tiled->tiled\n");
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, 0);
	for (i = 0; i < OBJECT_SIZE/4; i++) {
		int tile_row = i / (TEST_STRIDE * tile_height / 4);
		int row = i / (TEST_STRIDE * 2 / 4);
		int half = i & (TEST_STRIDE / 4);
		int ofs = i % (TEST_STRIDE / 4);
		int data_i = (tile_row/2)*(TEST_STRIDE * tile_height / 4)
			+ row*TEST_STRIDE/4
			+ half*tile_height + ofs;
		uint32_t val = data[data_i];

		igt_assert_f(ptr[i] == val,
			     "mismatch at %i, row=%i, half=%i, ofs=%i, "
			     "read: 0x%08x, expected: 0x%08x\n",
			     i, row, half, ofs,
			     ptr[i], val);

	}

	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
	for (i = 0; i < OBJECT_SIZE/4; i++)
		ptr[i] = data[i] = i;

	gem_set_tiling(fd, handle, I915_TILING_NONE, 0);
	igt_info("testing tiled->untiled\n");
	tiling_changed = false;
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, 0);
	/* Too lazy to check for the correct tiling, and impossible anyway on
	 * bit17 swizzling machines. */
	for (i = 0; i < OBJECT_SIZE/4; i++)
		if (ptr[i] != data[i])
			tiling_changed = true;
	igt_assert(tiling_changed);

	munmap(ptr, OBJECT_SIZE);

	close(fd);
}
