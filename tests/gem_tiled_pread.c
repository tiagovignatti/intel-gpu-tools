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
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_io.h"
#include "intel_chipset.h"

#define WIDTH 512
#define HEIGHT 512
static uint32_t linear[WIDTH * HEIGHT];

#define PAGE_SIZE 4096

static int tile_width;
static int tile_height;
static int tile_size;

static void
gem_get_tiling(int fd, uint32_t handle, uint32_t *tiling, uint32_t *swizzle)
{
	struct drm_i915_gem_get_tiling get_tiling;
	int ret;

	memset(&get_tiling, 0, sizeof(get_tiling));
	get_tiling.handle = handle;

	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_GET_TILING, &get_tiling);
	igt_assert(ret == 0);

	*tiling = get_tiling.tiling_mode;
	*swizzle = get_tiling.swizzle_mode;
}

static uint32_t
create_bo(int fd)
{
	uint32_t handle;
	uint32_t *data;
	int i;

	handle = gem_create(fd, sizeof(linear));
	gem_set_tiling(fd, handle, I915_TILING_X, WIDTH * sizeof(uint32_t));

	/* Fill the BO with dwords starting at start_val */
	data = gem_mmap(fd, handle, sizeof(linear), PROT_READ | PROT_WRITE);
	for (i = 0; i < WIDTH*HEIGHT; i++)
		data[i] = i;
	munmap(data, sizeof(linear));

	return handle;
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
	int tile_off = offset & (tile_size - 1);
	int tile_base = offset & -tile_size;
	int tile_index = tile_base / tile_size;
	int tiles_per_row = 4*WIDTH / tile_width;

	/* base x,y values from the tile (page) index. */
	int base_y = tile_index / tiles_per_row * tile_height;
	int base_x = tile_index % tiles_per_row * (tile_width/4);

	/* x, y offsets within the tile */
	int tile_y = tile_off / tile_width;
	int tile_x = (tile_off % tile_width) / 4;

	igt_debug("%3d, %3d, %3d,%3d\n", base_x, base_y, tile_x, tile_y);
	return (base_y + tile_y) * WIDTH + base_x + tile_x;
}

igt_simple_main
{
	int fd;
	int i, iter = 100;
	uint32_t tiling, swizzle;
	uint32_t handle;
	uint32_t devid;

	fd = drm_open_any();

	handle = create_bo(fd);
	gem_get_tiling(fd, handle, &tiling, &swizzle);

	devid = intel_get_drm_devid(fd);

	if (IS_GEN2(devid)) {
		tile_height = 16;
		tile_width = 128;
		tile_size = 2048;
	} else {
		tile_height = 8;
		tile_width = 512;
		tile_size = PAGE_SIZE;
	}

	/* Read a bunch of random subsets of the data and check that they come
	 * out right.
	 */
	for (i = 0; i < iter; i++) {
		int size = WIDTH * HEIGHT * 4;
		int offset = (random() % size) & ~3;
		int len = (random() % size) & ~3;
		int j;

		if (len == 0)
			len = 4;

		if (offset + len > size)
			len = size - offset;

		if (i == 0) {
			offset = 0;
			len = size;
		}

		gem_read(fd, handle, offset, linear, len);

		/* Translate from offsets in the read buffer to the swizzled
		 * address that it corresponds to.  This is the opposite of
		 * what Mesa does (calculate offset to be read given the linear
		 * offset it's looking for).
		 */
		for (j = offset; j < offset + len; j += 4) {
			uint32_t expected_val, found_val;
			int swizzled_offset;
			const char *swizzle_str;

			switch (swizzle) {
			case I915_BIT_6_SWIZZLE_NONE:
				swizzled_offset = j;
				swizzle_str = "none";
				break;
			case I915_BIT_6_SWIZZLE_9:
				swizzled_offset = j ^
					swizzle_bit(9, j);
				swizzle_str = "bit9";
				break;
			case I915_BIT_6_SWIZZLE_9_10:
				swizzled_offset = j ^
					swizzle_bit(9, j) ^
					swizzle_bit(10, j);
				swizzle_str = "bit9^10";
				break;
			case I915_BIT_6_SWIZZLE_9_11:
				swizzled_offset = j ^
					swizzle_bit(9, j) ^
					swizzle_bit(11, j);
				swizzle_str = "bit9^11";
				break;
			case I915_BIT_6_SWIZZLE_9_10_11:
				swizzled_offset = j ^
					swizzle_bit(9, j) ^
					swizzle_bit(10, j) ^
					swizzle_bit(11, j);
				swizzle_str = "bit9^10^11";
				break;
			default:
				igt_assert_f(0, "Bad swizzle bits; %d\n",
					     swizzle);
			}
			expected_val = calculate_expected(swizzled_offset);
			found_val = linear[(j - offset) / 4];
			igt_assert_f(expected_val == found_val,
				     "Bad read [%d]: %d instead of %d at 0x%08x "
				     "for read from 0x%08x to 0x%08x, swizzle=%s\n",
				     i, found_val, expected_val, j,
				     offset, offset + len,
				     swizzle_str);
		}
	}

	close(fd);
}
