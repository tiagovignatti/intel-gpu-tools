/*
 * Copyright © 2011 Intel Corporation
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

/* Testcase: check whether the libdrm vma limiter works
 *
 * We've had reports of the X server exhausting the default rlimit of 64k vma's
 * in the kernel. libdrm has grown facilities to limit the vma caching since,
 * this checks whether they actually work.
 *
 * This one checks whether mmaps of unused cached bos are also properly reaped.
 */

/* we do both cpu and gtt maps, so only need half of 64k to exhaust */

int main(int argc, char **argv)
{
	int fd;
	int i;
	char *ptr;
	drm_intel_bo *load_bo;

	fd = drm_open_any();

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));

	load_bo = drm_intel_bo_alloc(bufmgr, "target bo", 1024*4096, 4096);
	assert(load_bo);

	drm_intel_bufmgr_gem_set_vma_cache_size(bufmgr, 500);

	/* IMPORTANT: we need to enable buffer reuse, otherwise we won't test
	 * the libdrm bo cache! */
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	/* put some load onto the gpu to keep the light buffers active for long
	 * enough */
	for (i = 0; i < 10000; i++) {
		BEGIN_BATCH(8);
		OUT_BATCH(XY_SRC_COPY_BLT_CMD |
			  XY_SRC_COPY_BLT_WRITE_ALPHA |
			  XY_SRC_COPY_BLT_WRITE_RGB);
		OUT_BATCH((3 << 24) | /* 32 bits */
			  (0xcc << 16) | /* copy ROP */
			  4096);
		OUT_BATCH(0); /* dst x1,y1 */
		OUT_BATCH((1024 << 16) | 512);
		OUT_RELOC(load_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
		OUT_BATCH((0 << 16) | 512); /* src x1, y1 */
		OUT_BATCH(4096);
		OUT_RELOC(load_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
		ADVANCE_BATCH();
	}

#define GROUP_SZ 100
	for (i = 0; i < 68000; ) {
		int j;
		drm_intel_bo *bo[GROUP_SZ];

		for (j = 0; j < GROUP_SZ; j++, i++) {
			bo[j] = drm_intel_bo_alloc(bufmgr, "mmap bo", 4096, 4096);
			assert(bo[j]);

			drm_intel_gem_bo_map_gtt(bo[j]);
			ptr = bo[j]->virtual;
			assert(ptr);
			*ptr = 'c';
			drm_intel_gem_bo_unmap_gtt(bo[j]);

			/* put it onto the active list ... */
			BEGIN_BATCH(6);
			OUT_BATCH(XY_COLOR_BLT_CMD |
				  XY_COLOR_BLT_WRITE_ALPHA |
				  XY_COLOR_BLT_WRITE_RGB);
			OUT_BATCH((3 << 24) | /* 32 bits */
				  128);
			OUT_BATCH(0); /* dst x1,y1 */
			OUT_BATCH((1 << 16) | 1);
			OUT_RELOC(bo[j], I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
			OUT_BATCH(0xffffffff); /* color */
			ADVANCE_BATCH();
		}
		intel_batchbuffer_flush(batch);

		for (j = 0; j < GROUP_SZ; j++)
			drm_intel_bo_unreference(bo[j]);
	}

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
