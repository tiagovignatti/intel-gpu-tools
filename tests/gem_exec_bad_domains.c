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

/* Testcase: Test whether the kernel rejects relocations with non-gpu domains
 *
 * If it does not, it'll oops somewhen later on because we don't expect that.
 */

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;

#define BAD_GTT_DEST ((512*1024*1024)) /* past end of aperture */

static int
run_batch(void)
{
	unsigned int used = batch->ptr - batch->buffer;
	int ret;

	if (used == 0)
		return 0;

	/* Round batchbuffer usage to 2 DWORDs. */
	if ((used & 4) == 0) {
		*(uint32_t *) (batch->ptr) = 0; /* noop */
		batch->ptr += 4;
	}

	/* Mark the end of the buffer. */
	*(uint32_t *)(batch->ptr) = MI_BATCH_BUFFER_END; /* noop */
	batch->ptr += 4;
	used = batch->ptr - batch->buffer;

	ret = drm_intel_bo_subdata(batch->bo, 0, used, batch->buffer);
	assert(ret == 0);

	batch->ptr = NULL;

	ret = drm_intel_bo_mrb_exec(batch->bo, used, NULL, 0, 0, 0);

	intel_batchbuffer_reset(batch);

	return ret;
}

int main(int argc, char **argv)
{
	int fd, ret;
	drm_intel_bo *tmp;

	fd = drm_open_any();

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));

	tmp = drm_intel_bo_alloc(bufmgr, "tmp", 128 * 128, 4096);

	BEGIN_BATCH(2);
	OUT_BATCH(0);
	OUT_RELOC(tmp, I915_GEM_DOMAIN_CPU, 0, 0);
	ADVANCE_BATCH();
	ret = run_batch();
	if (ret != -EINVAL) {
		fprintf(stderr, "(cpu, 0) reloc not rejected\n");
		exit(1);
	}

	BEGIN_BATCH(2);
	OUT_BATCH(0);
	OUT_RELOC(tmp, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU, 0);
	ADVANCE_BATCH();
	ret = run_batch();
	if (ret != -EINVAL) {
		fprintf(stderr, "(cpu, cpu) reloc not rejected\n");
		exit(1);
	}

	BEGIN_BATCH(2);
	OUT_BATCH(0);
	OUT_RELOC(tmp, I915_GEM_DOMAIN_GTT, 0, 0);
	ADVANCE_BATCH();
	ret = run_batch();
	if (ret != -EINVAL) {
		fprintf(stderr, "(gtt, 0) reloc not rejected\n");
		exit(1);
	}

	BEGIN_BATCH(2);
	OUT_BATCH(0);
	OUT_RELOC(tmp, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT, 0);
	ADVANCE_BATCH();
	ret = run_batch();
	if (ret != -EINVAL) {
		fprintf(stderr, "(gtt, gtt) reloc not rejected\n");
		exit(1);
	}

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
