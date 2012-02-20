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

/** @file gem_ringfill.c
 *
 * This is a test of doing many tiny batchbuffer operations, in the hope of
 * catching failure to manage the ring properly near full.
 */

#include <stdbool.h>
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

int main(int argc, char **argv)
{
	int fd;
	int i;
	drm_intel_bo *src_bo, *dst_bo;
	uint32_t *map;
	int fails = 0;
	int pitch = width * 4;
	int size = pitch * height;
	int blits;

	fd = drm_open_any();

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));

	src_bo = drm_intel_bo_alloc(bufmgr, "src bo", size, 4096);
	dst_bo = drm_intel_bo_alloc(bufmgr, "src bo", size, 4096);

	/* Fill the src with indexes of the pixels */
	drm_intel_bo_map(src_bo, true);
	map = src_bo->virtual;
	for (i = 0; i < width * height; i++)
		map[i] = i;
	drm_intel_bo_unmap(src_bo);

	/* Fill the dst with garbage. */
	drm_intel_bo_map(dst_bo, true);
	map = dst_bo->virtual;
	for (i = 0; i < width * height; i++)
		map[i] = 0xd0d0d0d0;
	drm_intel_bo_unmap(dst_bo);

	/* The ring we've been using is 128k, and each rendering op
	 * will use at least 8 dwords:
	 *
	 * BATCH_START
	 * BATCH_START offset
	 * MI_FLUSH
	 * STORE_DATA_INDEX
	 * STORE_DATA_INDEX offset
	 * STORE_DATA_INDEX value
	 * MI_USER_INTERRUPT
	 * (padding)
	 *
	 * So iterate just a little more than that -- if we don't fill the ring
	 * doing this, we aren't likely to with this test.
	 */
	blits = width * height;
	for (i = 0; i < blits; i++) {
		int x = i % width;
		int y = i / width;

		assert(y < height);

		BEGIN_BATCH(8);
		OUT_BATCH(XY_SRC_COPY_BLT_CMD |
			  XY_SRC_COPY_BLT_WRITE_ALPHA |
			  XY_SRC_COPY_BLT_WRITE_RGB);
		OUT_BATCH((3 << 24) | /* 32 bits */
			  (0xcc << 16) | /* copy ROP */
			  pitch);
		OUT_BATCH((y << 16) | x); /* dst x1,y1 */
		OUT_BATCH(((y + 1) << 16) | (x + 1)); /* dst x2,y2 */
		OUT_RELOC(dst_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
		OUT_BATCH((y << 16) | x); /* src x1,y1 */
		OUT_BATCH(pitch);
		OUT_RELOC(src_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
		ADVANCE_BATCH();

		intel_batchbuffer_flush(batch);
	}

	/* verify */
	drm_intel_bo_map(dst_bo, false);
	map = dst_bo->virtual;
	for (i = 0; i < blits; i++) {
		int x = i % width;
		int y = i / width;

		if (map[i] != i) {

			printf("Copy #%d at %d,%d failed: read 0x%08x\n",
			       i, x, y, map[i]);

			if (fails++ > 9)
				exit(1);
		}
	}
	drm_intel_bo_unmap(dst_bo);

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return fails != 0;
}
