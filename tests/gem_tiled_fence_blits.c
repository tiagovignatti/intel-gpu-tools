/*
 * Copyright © 2009,2011 Intel Corporation
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

/** @file gem_tiled_fence_blits.c
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
static uint32_t linear[1024*1024/4];

static drm_intel_bo *
create_bo(int fd, uint32_t start_val)
{
	drm_intel_bo *bo;
	uint32_t tiling = I915_TILING_X;
	int ret, i;

	bo = drm_intel_bo_alloc(bufmgr, "tiled bo", 1024 * 1024, 4096);
	ret = drm_intel_bo_set_tiling(bo, &tiling, width * 4);
	assert(ret == 0);
	assert(tiling == I915_TILING_X);

	/* Fill the BO with dwords starting at start_val */
	for (i = 0; i < 1024 * 1024 / 4; i++)
		linear[i] = start_val++;

	gem_write(fd, bo->handle, 0, linear, sizeof(linear));

	return bo;
}

static void
check_bo(int fd, drm_intel_bo *bo, uint32_t start_val)
{
	int i;

	gem_read(fd, bo->handle, 0, linear, sizeof(linear));

	for (i = 0; i < 1024 * 1024 / 4; i++) {
		if (linear[i] != start_val) {
			fprintf(stderr, "Expected 0x%08x, found 0x%08x "
				"at offset 0x%08x\n",
				start_val, linear[i], i * 4);
			abort();
		}
		start_val++;
	}
}

int main(int argc, char **argv)
{
	drm_intel_bo *bo[4096];
	uint32_t bo_start_val[4096];
	uint32_t start = 0;
	int fd, i, count;

	fd = drm_open_any();
	count = 3 * gem_aperture_size(fd) / (1024*1024) / 2;
	if (count > intel_get_total_ram_mb() * 9 / 10) {
		count = intel_get_total_ram_mb() * 9 / 10;
		printf("not enough RAM to run test, reducing buffer count\n");
	}
	count |= 1;
	printf("Using %d 1MiB buffers\n", count);

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));

	for (i = 0; i < count; i++) {
		bo[i] = create_bo(fd, start);
		bo_start_val[i] = start;

		/*
		printf("Creating bo %d\n", i);
		check_bo(bo[i], bo_start_val[i]);
		*/

		start += 1024 * 1024 / 4;
	}

	for (i = 0; i < count; i++) {
		int src = count - i - 1;
		intel_copy_bo(batch, bo[i], bo[src], width, height);
		bo_start_val[i] = bo_start_val[src];
	}

	for (i = 0; i < count * 4; i++) {
		int src = random() % count;
		int dst = random() % count;

		if (src == dst)
			continue;

		intel_copy_bo(batch, bo[dst], bo[src], width, height);
		bo_start_val[dst] = bo_start_val[src];

		/*
		check_bo(bo[dst], bo_start_val[dst]);
		printf("%d: copy bo %d to %d\n", i, src, dst);
		*/
	}

	for (i = 0; i < count; i++) {
		/*
		printf("check %d\n", i);
		*/
		check_bo(fd, bo[i], bo_start_val[i]);

		drm_intel_bo_unreference(bo[i]);
		bo[i] = NULL;
	}

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
