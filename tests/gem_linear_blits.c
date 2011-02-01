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

/** @file gem_linear_blits.c
 *
 * This is a test of doing many blits, with a working set
 * larger than the aperture size.
 *
 * The goal is to simply ensure the basics work.
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

static uint64_t
gem_aperture_size(int fd)
{
	struct drm_i915_gem_get_aperture aperture;

	aperture.aper_size = 512*1024*1024;
	(void)drmIoctl(fd, DRM_IOCTL_I915_GEM_GET_APERTURE, &aperture);
	return aperture.aper_size;
}

static drm_intel_bo *
create_bo(uint32_t start_val)
{
	drm_intel_bo *bo;
	uint32_t *linear;
	int i;

	bo = drm_intel_bo_alloc(bufmgr, "linear bo", 1024 * 1024, 4096);

	/* Fill the BO with dwords starting at start_val */
	drm_intel_bo_map(bo, 1);
	linear = bo->virtual;
	for (i = 0; i < 1024 * 1024 / 4; i++)
		linear[i] = start_val++;
	drm_intel_bo_unmap(bo);

	return bo;
}

static void
check_bo(drm_intel_bo *bo, uint32_t start_val)
{
	uint32_t *linear;
	int i;

	drm_intel_bo_map(bo, 0);
	linear = bo->virtual;

	for (i = 0; i < 1024 * 1024 / 4; i++) {
		if (linear[i] != start_val) {
			fprintf(stderr, "Expected 0x%08x, found 0x%08x "
				"at offset 0x%08x\n",
				start_val, linear[i], i * 4);
			abort();
		}
		start_val++;
	}
	drm_intel_bo_unmap(bo);
}

int main(int argc, char **argv)
{
	drm_intel_bo *bo[4096];
	uint32_t bo_start_val[4096];
	uint32_t start = 0;
	int i, fd, count;

	fd = drm_open_any();

	count = 3 * gem_aperture_size(fd) / (1024*1024) / 2;
	printf("Using %d 1MiB buffers\n", count);
	assert(count <= 4096);

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));

	for (i = 0; i < count; i++) {
		bo[i] = create_bo(start);
		bo_start_val[i] = start;

		/*
		printf("Creating bo %d\n", i);
		check_bo(bo[i], bo_start_val[i]);
		*/

		start += 1024 * 1024 / 4;
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
		check_bo(bo[i], bo_start_val[i]);

		drm_intel_bo_unreference(bo[i]);
		bo[i] = NULL;
	}

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
