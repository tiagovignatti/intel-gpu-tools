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

/** @file gem_unfence_active_buffers.c
 *
 * Testcase: Check for use-after free in the fence stealing code
 *
 * If we're stealing the fence of a active object where the active list is the
 * only thing holding a reference, we need to be careful not to access the old
 * object we're stealing the fence from after that reference has been dropped by
 * retire_requests.
 *
 * Note that this needs slab poisoning enabled in the kernel to reliably hit the
 * problem - the race window is too small.
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
#include <stdbool.h>
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

uint32_t data[TEST_SIZE/4];

int main(int argc, char **argv)
{
	int i, ret, fd, num_fences;
	drm_intel_bo *busy_bo, *test_bo;
	uint32_t tiling = I915_TILING_X;

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

	num_fences = gem_available_fences(fd);
	printf("creating havoc on %i fences\n", num_fences);

	for (i = 0; i < num_fences*2; i++) {
		test_bo = drm_intel_bo_alloc(bufmgr, "test_bo",
					     TEST_SIZE, 4096);
		ret = drm_intel_bo_set_tiling(test_bo, &tiling, TEST_STRIDE);
		assert(ret == 0);

		drm_intel_bo_disable_reuse(test_bo);

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
		printf("test bo offset: %#lx\n", test_bo->offset);

		drm_intel_bo_unreference(test_bo);
	}

	/* launch a few batchs to ensure the damaged slab objects get reused. */
	for (i = 0; i < 10; i++) {
		BEGIN_BATCH(8);
		OUT_BATCH(XY_SRC_COPY_BLT_CMD |
			  XY_SRC_COPY_BLT_WRITE_ALPHA |
			  XY_SRC_COPY_BLT_WRITE_RGB);
		OUT_BATCH((3 << 24) | /* 32 bits */
			  (0xcc << 16) | /* copy ROP */
			  2*1024*4);
		OUT_BATCH(0 << 16 | 1024);
		OUT_BATCH((1) << 16 | (1));
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

	return 0;
}
