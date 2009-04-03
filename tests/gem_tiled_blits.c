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

/** @file gem_tiled_blits.c
 *
 * This is a test of doing many tiled blits, with a working set
 * larger than the aperture size.
 *
 * The goal is to catch a couple types of failure;
 * - Fence management problems on pre-965.
 * - A17 or L-shaped memory tiling workaround problems in acceleration.
 *
 * The model is to fill a collection of 1MB objects in a way that can't trip
 * over A6 swizzling -- upload data to a non-tiled object, blit to the tiled
 * object.  Then, copy the 1MB objects randomly between each other for a while.
 * Finally, download their data through linear objects again and see what
 * resulted.
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
static int width = 512, height = 512;

static void
copy_bo(drm_intel_bo *dst_bo, drm_intel_bo *src_bo)
{
	uint32_t src_tiling, dst_tiling, swizzle;
	uint32_t src_pitch, dst_pitch;
	uint32_t cmd_bits = 0;

	drm_intel_bo_get_tiling(src_bo, &src_tiling, &swizzle);
	drm_intel_bo_get_tiling(dst_bo, &dst_tiling, &swizzle);

	src_pitch = width * 4;
	if (IS_965(devid) && src_tiling != I915_TILING_NONE) {
		src_pitch /= 4;
		cmd_bits |= XY_SRC_COPY_BLT_SRC_TILED;
	}

	dst_pitch = width * 4;
	if (IS_965(devid) && dst_tiling != I915_TILING_NONE) {
		dst_pitch /= 4;
		cmd_bits |= XY_SRC_COPY_BLT_DST_TILED;
	}

	BEGIN_BATCH(8);
	OUT_BATCH(XY_SRC_COPY_BLT_CMD |
		  XY_SRC_COPY_BLT_WRITE_ALPHA |
		  XY_SRC_COPY_BLT_WRITE_RGB |
		  cmd_bits);
	OUT_BATCH((3 << 24) | /* 32 bits */
		  (0xcc << 16) | /* copy ROP */
		  dst_pitch);
	OUT_BATCH(0); /* dst x1,y1 */
	OUT_BATCH((height << 16) | width); /* dst x2,y2 */
	OUT_RELOC(dst_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(0); /* src x1,y1 */
	OUT_BATCH(src_pitch);
	OUT_RELOC(src_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
	ADVANCE_BATCH();

	intel_batchbuffer_flush(batch);
}

static drm_intel_bo *
create_bo(uint32_t start_val)
{
	drm_intel_bo *bo, *linear_bo;
	uint32_t *linear;
	uint32_t tiling = I915_TILING_X;
	int ret, i;

	bo = drm_intel_bo_alloc(bufmgr, "tiled bo", 1024 * 1024, 4096);
	ret = drm_intel_bo_set_tiling(bo, &tiling, width * 4);
	assert(ret == 0);
	assert(tiling == I915_TILING_X);

	linear_bo = drm_intel_bo_alloc(bufmgr, "linear src", 1024 * 1024, 4096);

	/* Fill the BO with dwords starting at start_val */
	drm_intel_bo_map(linear_bo, 1);
	linear = linear_bo->virtual;

	for (i = 0; i < 1024 * 1024 / 4; i++) {
		linear[i] = start_val++;
	}
	drm_intel_bo_unmap(linear_bo);

	copy_bo(bo, linear_bo);

	drm_intel_bo_unreference(linear_bo);

	return bo;
}

static void
check_bo(drm_intel_bo *bo, uint32_t start_val)
{
	drm_intel_bo *linear_bo;
	uint32_t *linear;
	int i;

	linear_bo = drm_intel_bo_alloc(bufmgr, "linear dst", 1024 * 1024, 4096);

	copy_bo(linear_bo, bo);

	drm_intel_bo_map(linear_bo, 0);
	linear = linear_bo->virtual;

	for (i = 0; i < 1024 * 1024 / 4; i++) {
		if (linear[i] != start_val) {
			fprintf(stderr, "Expected 0x%08x, found 0x%08x "
				"at offset 0x%08x\n",
				start_val, linear[i], i * 4);
			abort();
		}
		start_val++;
	}
	drm_intel_bo_unmap(linear_bo);

	drm_intel_bo_unreference(linear_bo);
}

int main(int argc, char **argv)
{
	int fd;
	int bo_count = 768; /* 768MB of objects */
	drm_intel_bo *bo[bo_count];
	uint32_t bo_start_val[bo_count];
	uint32_t start = 0;
	int i;

	fd = drm_open_any();
	intel_get_drm_devid(fd);

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch = intel_batchbuffer_alloc(bufmgr);

	for (i = 0; i < bo_count; i++) {
		bo[i] = create_bo(start);
		bo_start_val[i] = start;

		/*
		printf("Creating bo %d\n", i);
		check_bo(bo[i], bo_start_val[i]);
		*/

		start += 1024 * 1024 / 4;
	}

	for (i = 0; i < bo_count * 4; i++) {
		int src = random() % bo_count;
		int dst = random() % bo_count;

		if (src == dst)
			continue;

		copy_bo(bo[dst], bo[src]);
		bo_start_val[dst] = bo_start_val[src];

		/*
		check_bo(bo[dst], bo_start_val[dst]);
		printf("%d: copy bo %d to %d\n", i, src, dst);
		*/
	}

	for (i = 0; i < bo_count; i++) {
		/*
		printf("check %d\n", i);
		*/
		check_bo(bo[i], bo_start_val[i]);

		drm_intel_bo_unreference(bo[i]);
		bo[i] = NULL;
	}

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
