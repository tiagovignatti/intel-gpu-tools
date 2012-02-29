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
 *
 */

/*
 * Testcase: Test the CS prefetch behaviour on batches
 *
 * Historically the batch prefetcher doesn't check whether it's crossing page
 * boundaries and likes to throw up when it gets a pagefault in return for his
 * over-eager behaviour. Check for this.
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

static void exec(int fd, uint32_t handle)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];
	int ret = 0;

	gem_exec[0].handle = handle;
	gem_exec[0].relocation_count = 0;
	gem_exec[0].relocs_ptr = 0;
	gem_exec[0].alignment = 0;
	gem_exec[0].offset = 0;
	gem_exec[0].flags = 0;
	gem_exec[0].rsvd1 = 0;
	gem_exec[0].rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = 4096;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = 0;
	execbuf.rsvd1 = 0;
	execbuf.rsvd2 = 0;

	ret = drmIoctl(fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER2,
		       &execbuf);
	gem_sync(fd, handle);
	assert(ret == 0);
}

int main(int argc, char **argv)
{
	uint32_t batch_end[4] = {MI_BATCH_BUFFER_END, 0, 0, 0};
	int fd, i, ret;
	uint64_t aper_size;
	int count;
	drm_intel_bo *sample_batch_bo;

	fd = drm_open_any();

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	if (!bufmgr) {
		fprintf(stderr, "failed to init libdrm\n");
		exit(-1);
	}

	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	
	aper_size = gem_aperture_size(fd);

	/* presume a big per-bo overhead */
	if (intel_get_total_ram_mb() < (aper_size / (1024*1024)) * 3 / 2) {
		fprintf(stderr, "not enough mem to run test\n");
		return 77;
	}

	count = aper_size / 4096;

	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
	assert(batch);

	sample_batch_bo = drm_intel_bo_alloc(bufmgr, "", 4096, 4096);
	assert(sample_batch_bo);
	ret = drm_intel_bo_subdata(sample_batch_bo, 4096-sizeof(batch_end),
				   sizeof(batch_end), batch_end);
	assert(ret == 0);

	/* fill the entire gart with batches and run them */
	for (i = 0; i < count; i++) {
		drm_intel_bo *batch_bo;

		batch_bo = drm_intel_bo_alloc(bufmgr, "", 4096, 4096);
		assert(batch_bo);

		/* copy the sample batch with the gpu to the new one, so that we
		 * also test the unmappable part of the gtt. */
		BEGIN_BATCH(8);
		OUT_BATCH(XY_SRC_COPY_BLT_CMD |
			  XY_SRC_COPY_BLT_WRITE_ALPHA |
			  XY_SRC_COPY_BLT_WRITE_RGB);
		OUT_BATCH((3 << 24) | /* 32 bits */
			  (0xcc << 16) | /* copy ROP */
			  4096);
		OUT_BATCH(0); /* dst y1,x1 */
		OUT_BATCH((1 << 16) | 1024);
		OUT_RELOC(batch_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
		OUT_BATCH((0 << 16) | 0); /* src x1, y1 */
		OUT_BATCH(4096);
		OUT_RELOC(sample_batch_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
		ADVANCE_BATCH();

		intel_batchbuffer_flush(batch);
		if (i % 100 == 0)
			gem_sync(fd, batch_bo->handle);

		drm_intel_bo_disable_reuse(batch_bo);

		/* launch the newly created batch */
		exec(fd, batch_bo->handle);

		// leak buffers
		//drm_intel_bo_unreference(batch_bo);
		drmtest_progress("gem_cs_prefetch: ", i, count);
	}

	fprintf(stderr, "Test suceeded, cleanup up - this might take a while.\n");
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
