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

/** @file gem_tiled_after_untiled_blt.c
 *
 * Testcase: Check for proper synchronization when switching tiled->untiled
 *
 * The blitter on gen3 and earlier needs properly set up fences. Which also
 * means that for untiled blits we may not set up a fence before that blt has
 * finished.
 *
 * Current kernels have a bug there, but it's pretty hard to hit because you
 * need:
 * - a blt on an untiled object which is aligned correctly for tiling.
 * - a set_tiling to switch that object to tiling
 * - another blt without any intervening cpu access that uses this object.
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
uint32_t devid;

#define TEST_SIZE (1024*1024)
#define TEST_STRIDE (4*1024)

int main(int argc, char **argv)
{
	drm_intel_bo *busy_bo, *test_bo, *target_bo;
	int i, fd, ret;
	uint32_t data[TEST_SIZE/4];
	uint32_t tiling = I915_TILING_X;
	uint32_t *ptr;
	uint32_t test_bo_handle;

	for (i = 0; i < 1024*256; i++)
		data[i] = i;

	fd = drm_open_any();

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	devid = intel_get_drm_devid(fd);
	batch = intel_batchbuffer_alloc(bufmgr, devid);

	printf("filling ring\n");
	busy_bo = drm_intel_bo_alloc(bufmgr, "busy bo bo", 16*1024*1024, 4096);

	for (i = 0; i < 250; i++) {
		BEGIN_BATCH(8);
		OUT_BATCH(XY_SRC_COPY_BLT_CMD |
			  XY_SRC_COPY_BLT_WRITE_ALPHA |
			  XY_SRC_COPY_BLT_WRITE_RGB);
		OUT_BATCH((3 << 24) | /* 32 bits */
			  (0xcc << 16) | /* copy ROP */
			  2*1024*4);
		OUT_BATCH(0 << 16 | 1024);
		OUT_BATCH((2048) << 16 | (2048));
		OUT_RELOC_FENCED(busy_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
		OUT_BATCH(0 << 16 | 0);
		OUT_BATCH(2*1024*4);
		OUT_RELOC_FENCED(busy_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
		ADVANCE_BATCH();

		if (IS_GEN6(devid) || IS_GEN7(devid)) {
			BEGIN_BATCH(3);
			OUT_BATCH(XY_SETUP_CLIP_BLT_CMD);
			OUT_BATCH(0);
			OUT_BATCH(0);
			ADVANCE_BATCH();
		}
	}
	intel_batchbuffer_flush(batch);

	printf("playing tricks\n");
	/* first allocate the target so it gets out of the way of playing funky
	 * tricks */
	target_bo = drm_intel_bo_alloc(bufmgr, "target bo", TEST_SIZE, 4096);

	/* allocate buffer tiled and touch it, so that it's properly aligned in
	 * the gtt. */
	test_bo = drm_intel_bo_alloc(bufmgr, "tiled busy bo", TEST_SIZE, 4096);
	test_bo_handle = test_bo->handle;
	ret = drm_intel_bo_set_tiling(test_bo, &tiling, TEST_STRIDE);
	assert(ret == 0);
	assert(tiling == I915_TILING_X);
	drm_intel_gem_bo_map_gtt(test_bo);
	ptr = test_bo->virtual;
	*ptr = 0;
	drm_intel_gem_bo_unmap_gtt(test_bo);

	drm_intel_bo_unreference(test_bo);

	test_bo = NULL;

	/* note we need a bo bigger than batches, otherwise the buffer reuse
	 * trick will fail. */
	test_bo = drm_intel_bo_alloc(bufmgr, "busy bo", TEST_SIZE, 4096);
	if (test_bo_handle != test_bo->handle)
		fprintf(stderr, "libdrm reuse trick failed\n");
	test_bo_handle = test_bo->handle;

	drm_intel_bo_subdata(test_bo, 0, TEST_SIZE, data);

	BEGIN_BATCH(8);
	OUT_BATCH(XY_SRC_COPY_BLT_CMD |
		  XY_SRC_COPY_BLT_WRITE_ALPHA |
		  XY_SRC_COPY_BLT_WRITE_RGB);
	OUT_BATCH((3 << 24) | /* 32 bits */
		  (0xcc << 16) | /* copy ROP */
		  TEST_STRIDE);
	OUT_BATCH(0 << 16 | 0);
	OUT_BATCH((256) << 16 | (1024));
	OUT_RELOC_FENCED(target_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(0 << 16 | 0);
	OUT_BATCH(TEST_STRIDE);
	OUT_RELOC_FENCED(test_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
	ADVANCE_BATCH();
	intel_batchbuffer_flush(batch);

	drm_intel_bo_unreference(test_bo);

	test_bo = drm_intel_bo_alloc_for_render(bufmgr, "tiled busy bo", TEST_SIZE, 4096);
	if (test_bo_handle != test_bo->handle)
		fprintf(stderr, "libdrm reuse trick failed\n");
	ret = drm_intel_bo_set_tiling(test_bo, &tiling, TEST_STRIDE);
	assert(ret == 0);
	assert(tiling == I915_TILING_X);

	/* Note: We don't care about gen4+ here because the blitter doesn't use
	 * fences there. So not setting tiling flags on the tiled buffer is ok.
	 */
	BEGIN_BATCH(8);
	OUT_BATCH(XY_SRC_COPY_BLT_CMD |
		  XY_SRC_COPY_BLT_WRITE_ALPHA |
		  XY_SRC_COPY_BLT_WRITE_RGB);
	OUT_BATCH((3 << 24) | /* 32 bits */
		  (0xcc << 16) | /* copy ROP */
		  TEST_STRIDE);
	OUT_BATCH(0 << 16 | 0);
	OUT_BATCH((1) << 16 | (1));
	OUT_RELOC_FENCED(test_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(0 << 16 | 0);
	OUT_BATCH(TEST_STRIDE);
	OUT_RELOC_FENCED(test_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
	ADVANCE_BATCH();
	intel_batchbuffer_flush(batch);

	drm_intel_bo_unreference(test_bo);

	/* Now try to trick the kernel the kernel into setting up the fence too
	 * early. */

	printf("checking\n");
	memset(data, 0, TEST_SIZE);
	drm_intel_bo_get_subdata(target_bo, 0, TEST_SIZE, data);
	for (i = 0; i < TEST_SIZE/4; i++)
		assert(data[i] == i);

	printf("done\n");


	return 0;
}
