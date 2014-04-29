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
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"
#include "intel_chipset.h"

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
	igt_assert(ret == 0);

	batch->ptr = NULL;

	ret = drm_intel_bo_mrb_exec(batch->bo, used, NULL, 0, 0, 0);

	intel_batchbuffer_reset(batch);

	return ret;
}

#define I915_GEM_GPU_DOMAINS \
	(I915_GEM_DOMAIN_RENDER | \
	 I915_GEM_DOMAIN_SAMPLER | \
	 I915_GEM_DOMAIN_COMMAND | \
	 I915_GEM_DOMAIN_INSTRUCTION | \
	 I915_GEM_DOMAIN_VERTEX)

static void multi_write_domain(int fd)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec[2];
	struct drm_i915_gem_relocation_entry reloc[1];
	uint32_t handle, handle_target;
	int ret;

	handle = gem_create(fd, 4096);
	handle_target = gem_create(fd, 4096);

	exec[0].handle = handle_target;
	exec[0].relocation_count = 0;
	exec[0].relocs_ptr = 0;
	exec[0].alignment = 0;
	exec[0].offset = 0;
	exec[0].flags = 0;
	exec[0].rsvd1 = 0;
	exec[0].rsvd2 = 0;

	exec[1].handle = handle;
	exec[1].relocation_count = 1;
	exec[1].relocs_ptr = (uintptr_t) reloc;
	exec[1].alignment = 0;
	exec[1].offset = 0;
	exec[1].flags = 0;
	exec[1].rsvd1 = 0;
	exec[1].rsvd2 = 0;

	reloc[0].offset = 4;
	reloc[0].delta = 0;
	reloc[0].target_handle = handle_target;
	reloc[0].read_domains = I915_GEM_DOMAIN_RENDER | I915_GEM_DOMAIN_INSTRUCTION;
	reloc[0].write_domain = I915_GEM_DOMAIN_RENDER | I915_GEM_DOMAIN_INSTRUCTION;
	reloc[0].presumed_offset = 0;

	execbuf.buffers_ptr = (uintptr_t)exec;
	execbuf.buffer_count = 2;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = 8;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = 0;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	ret = drmIoctl(fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER2,
		       &execbuf);
	igt_assert(ret != 0 && errno == EINVAL);

	gem_close(fd, handle);
	gem_close(fd, handle_target);
}

int fd;
drm_intel_bo *tmp;

igt_main
{
	igt_fixture {
		fd = drm_open_any();

		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		drm_intel_bufmgr_gem_enable_reuse(bufmgr);
		batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));

		tmp = drm_intel_bo_alloc(bufmgr, "tmp", 128 * 128, 4096);
	}

	igt_subtest("cpu-domain") {
		BEGIN_BATCH(2);
		OUT_BATCH(0);
		OUT_RELOC(tmp, I915_GEM_DOMAIN_CPU, 0, 0);
		ADVANCE_BATCH();
		igt_assert(run_batch() == -EINVAL);

		BEGIN_BATCH(2);
		OUT_BATCH(0);
		OUT_RELOC(tmp, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU, 0);
		ADVANCE_BATCH();
		igt_assert(run_batch() == -EINVAL);
	}

	igt_subtest("gtt-domain") {
		BEGIN_BATCH(2);
		OUT_BATCH(0);
		OUT_RELOC(tmp, I915_GEM_DOMAIN_GTT, 0, 0);
		ADVANCE_BATCH();
		igt_assert(run_batch() == -EINVAL);

		BEGIN_BATCH(2);
		OUT_BATCH(0);
		OUT_RELOC(tmp, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT, 0);
		ADVANCE_BATCH();
		igt_assert(run_batch() == -EINVAL);
	}

	/* Note: Older kernels disallow this. Punt on the skip check though
	 * since this is too old. */
	igt_subtest("conflicting-write-domain") {
		BEGIN_BATCH(4);
		OUT_BATCH(0);
		OUT_RELOC(tmp, I915_GEM_DOMAIN_RENDER,
			  I915_GEM_DOMAIN_RENDER, 0);
		OUT_BATCH(0);
		OUT_RELOC(tmp, I915_GEM_DOMAIN_INSTRUCTION,
			  I915_GEM_DOMAIN_INSTRUCTION, 0);
		ADVANCE_BATCH();
		igt_assert(run_batch() == 0);
	}

	igt_subtest("double-write-domain")
		multi_write_domain(fd);

	igt_subtest("invalid-gpu-domain") {
		BEGIN_BATCH(2);
		OUT_BATCH(0);
		OUT_RELOC(tmp, ~(I915_GEM_GPU_DOMAINS | I915_GEM_DOMAIN_GTT | I915_GEM_DOMAIN_CPU),
			  0, 0);
		ADVANCE_BATCH();
		igt_assert(run_batch() == -EINVAL);

		BEGIN_BATCH(2);
		OUT_BATCH(0);
		OUT_RELOC(tmp, I915_GEM_DOMAIN_GTT << 1,
			  I915_GEM_DOMAIN_GTT << 1, 0);
		ADVANCE_BATCH();
		igt_assert(run_batch() == -EINVAL);
	}

	igt_fixture {
		intel_batchbuffer_free(batch);
		drm_intel_bufmgr_destroy(bufmgr);

		close(fd);
	}
}
