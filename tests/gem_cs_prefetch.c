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
 *
 * This test for a bug where we've failed to plug a scratch pte entry into the
 * very last gtt pte.
 */
#include "igt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <drm.h>

#include "intel_bufmgr.h"

IGT_TEST_DESCRIPTION("Test the CS prefetch behaviour on batches.");

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;

static void exec(int fd, uint32_t handle)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];

	memset(gem_exec, 0, sizeof(gem_exec));
	gem_exec[0].handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = 4096;

	gem_execbuf(fd, &execbuf);
	gem_sync(fd, handle);
}

igt_simple_main
{
	uint32_t batch_end[4] = {MI_BATCH_BUFFER_END, 0, 0, 0};
	int fd, i, ret;
	uint64_t aper_size;
	int count;
	drm_intel_bo *sample_batch_bo;

	igt_skip_on_simulation();

	fd = drm_open_any();

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	igt_assert(bufmgr);

	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	
	aper_size = gem_aperture_size(fd);

	/* presume a big per-bo overhead */
	igt_skip_on_f(intel_get_total_ram_mb() < (aper_size / (1024*1024)) * 3 / 2,
		      "not enough mem to run test\n");

	count = aper_size / 4096;

	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
	igt_assert(batch);

	sample_batch_bo = drm_intel_bo_alloc(bufmgr, "", 4096, 4096);
	igt_assert(sample_batch_bo);
	ret = drm_intel_bo_subdata(sample_batch_bo, 4096-sizeof(batch_end),
				   sizeof(batch_end), batch_end);
	igt_assert(ret == 0);

	/* fill the entire gart with batches and run them */
	for (i = 0; i < count; i++) {
		drm_intel_bo *batch_bo;

		batch_bo = drm_intel_bo_alloc(bufmgr, "", 4096, 4096);
		igt_assert(batch_bo);

		/* copy the sample batch with the gpu to the new one, so that we
		 * also test the unmappable part of the gtt. */
		BLIT_COPY_BATCH_START(0);
		OUT_BATCH((3 << 24) | /* 32 bits */
			  (0xcc << 16) | /* copy ROP */
			  4096);
		OUT_BATCH(0); /* dst y1,x1 */
		OUT_BATCH((1 << 16) | 1024);
		OUT_RELOC_FENCED(batch_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
		OUT_BATCH((0 << 16) | 0); /* src x1, y1 */
		OUT_BATCH(4096);
		OUT_RELOC_FENCED(sample_batch_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
		ADVANCE_BATCH();

		intel_batchbuffer_flush(batch);
		if (i % 100 == 0)
			gem_sync(fd, batch_bo->handle);

		drm_intel_bo_disable_reuse(batch_bo);

		/* launch the newly created batch */
		exec(fd, batch_bo->handle);

		// leak buffers
		//drm_intel_bo_unreference(batch_bo);
		igt_progress("gem_cs_prefetch: ", i, count);
	}

	igt_info("Test suceeded, cleanup up - this might take a while.\n");
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);
}
