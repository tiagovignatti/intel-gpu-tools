/*
 * Copyright © 2009 Intel Corporation
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
 *
 */

/** @file gem_tiled_pread.c
 *
 * This is a test of pread's behavior on tiled objects with respect to the
 * reported swizzling value.
 *
 * The goal is to exercise the slow_bit17_copy path for reading on bit17
 * machines, but will also be useful for catching swizzling value bugs on
 * other systems.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_gpu_tools.h"

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
static const int width = 512, height = 512;
static const int size = 1024 * 1024;

#define PAGE_SIZE 4096

static drm_intel_bo *
create_bo(uint32_t devid)
{
	drm_intel_bo *bo, *linear_bo;
	uint32_t *linear;
	uint32_t tiling = I915_TILING_X;
	int ret, i;
	int val = 0;

	bo = drm_intel_bo_alloc(bufmgr, "tiled bo", size, 4096);
	ret = drm_intel_bo_set_tiling(bo, &tiling, width * 4);
	assert(ret == 0);
	assert(tiling == I915_TILING_X);
	linear_bo = drm_intel_bo_alloc(bufmgr, "linear src", size, 4096);

	/* Fill the BO with dwords starting at start_val */
	drm_intel_bo_map(linear_bo, 1);
	linear = linear_bo->virtual;

	for (i = 0; i < 1024 * 1024 / 4; i++)
		linear[i] = val++;
	drm_intel_bo_unmap(linear_bo);

	intel_copy_bo(batch, bo, linear_bo, width, height, devid);

	drm_intel_bo_unreference(linear_bo);

	return bo;
}

static int
swizzle_bit(int bit, int offset)
{
	return (offset & (1 << bit)) >> (bit - 6);
}

/* Translate from a swizzled offset in the tiled buffer to the corresponding
 * value from the original linear buffer.
 */
static uint32_t
calculate_expected(int offset)
{
	int tile_off = offset & (PAGE_SIZE - 1);
	int tile_base = offset - tile_off;
	int tile_index = tile_base / PAGE_SIZE;
	int tiles_per_row = width / (512 / 4); /* X tiled = 512b rows */

	/* base x,y values from the tile (page) index. */
	int base_y = tile_index / tiles_per_row * 8;
	int base_x = tile_index % tiles_per_row * 128;

	assert((offset % 4) == 0);
	/* x, y offsets within the tile */
	int tile_y = tile_off / 512;
	int tile_x = (tile_off % 512) / 4;

	/* printf("%3d, %3d, %3d,%3d\n", base_x, base_y, tile_x, tile_y); */
	return (base_y + tile_y) * width + base_x + tile_x;
}

int
main(int argc, char **argv)
{
	int fd;
	uint32_t devid;
	drm_intel_bo *bo;
	int i, iter = 100;
	uint32_t buf[width * height];
	uint32_t tiling, swizzle;

	fd = drm_open_any();
	devid = intel_get_drm_devid(fd);

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch = intel_batchbuffer_alloc(bufmgr);

	bo = create_bo(devid);

	drm_intel_bo_get_tiling(bo, &tiling, &swizzle);

	/* Read a bunch of random subsets of the data and check that they come
	 * out right.
	 */
	for (i = 0; i < iter; i++) {
		int offset = (random() % size) & ~3;
		int len = (random() % size) & ~3;
		int j;

		if (len == 0)
			len = 4;

		if (offset + len > size)
			len = size - offset;

		/* For sanity of reporting, make the first iteration be the
		 * whole buffer.
		 */
		if (i == 0) {
			offset = 0;
			len = size;
		}

		drm_intel_bo_get_subdata(bo, offset, len, buf);

		/* Translate from offsets in the read buffer to the swizzled
		 * address that it corresponds to.  This is the opposite of
		 * what Mesa does (calculate offset to be read given the linear
		 * offset it's looking for).
		 */
		for (j = offset; j < offset + len; j += 4) {
			uint32_t expected_val, found_val;
			int swizzled_offset;

			switch (swizzle) {
			case I915_BIT_6_SWIZZLE_NONE:
				swizzled_offset = j;
				break;
			case I915_BIT_6_SWIZZLE_9:
				swizzled_offset = j ^
					swizzle_bit(9, j);
				break;
			case I915_BIT_6_SWIZZLE_9_10:
				swizzled_offset = j ^
					swizzle_bit(9, j) ^
					swizzle_bit(10, j);
				break;
			case I915_BIT_6_SWIZZLE_9_11:
				swizzled_offset = j ^
					swizzle_bit(9, j) ^
					swizzle_bit(11, j);
				break;
			case I915_BIT_6_SWIZZLE_9_10_11:
				swizzled_offset = j ^
					swizzle_bit(9, j) ^
					swizzle_bit(10, j) ^
					swizzle_bit(11, j);
				break;
			default:
				fprintf(stderr, "Bad swizzle bits; %d\n",
					swizzle);
				abort();
			}
			expected_val = calculate_expected(swizzled_offset);
			found_val = buf[(j - offset) / 4];
			if (expected_val != found_val) {
				fprintf(stderr,
					"Bad read: %d instead of %d at 0x%08x "
					"for read from 0x%08x to 0x%08x\n",
					found_val, expected_val, j,
					offset, offset + len);
				abort();
			}
		}
	}

	drm_intel_bo_unreference(bo);

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
