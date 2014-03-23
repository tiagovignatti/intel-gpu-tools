/*
 * Copyright © 2013 Intel Corporation
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

#include <drm.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_chipset.h"
#include "intel_io.h"
#include "i830_reg.h"
#include "igt_aux.h"

#define LOCAL_I915_EXEC_VEBOX (4<<0)

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
static drm_intel_bo *load_bo, *target_bo, *dummy_bo;
int fd;

/* Testcase: check read/write syncpoints when switching rings
 *
 * We've had a bug where the syncpoint for the last write was mangled after a
 * ring switch using semaphores. This resulted in cpu reads returning before the
 * write actually completed. This test exercises this.
 */

#define COLOR 0xffffffff
static void run_test(int ring)
{
	uint32_t *ptr;
	int i;

	gem_require_ring(fd, ring);
	/* Testing render only makes sense with separate blt. */
	if (ring == I915_EXEC_RENDER)
		gem_require_ring(fd, I915_EXEC_BLT);

	target_bo = drm_intel_bo_alloc(bufmgr, "target bo", 4096, 4096);
	igt_assert(target_bo);

	/* Need to map first so that we can do our own domain mangement with
	 * set_domain. */
	drm_intel_bo_map(target_bo, 0);
	ptr = target_bo->virtual;
	igt_assert(*ptr == 0);

	/* put some load onto the gpu to keep the light buffers active for long
	 * enough */
	for (i = 0; i < 1000; i++) {
		BLIT_COPY_BATCH_START(batch->devid, 0);
		OUT_BATCH((3 << 24) | /* 32 bits */
			  (0xcc << 16) | /* copy ROP */
			  4096);
		OUT_BATCH(0); /* dst x1,y1 */
		OUT_BATCH((1024 << 16) | 512);
		OUT_RELOC(load_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
		BLIT_RELOC_UDW(batch->devid);
		OUT_BATCH((0 << 16) | 512); /* src x1, y1 */
		OUT_BATCH(4096);
		OUT_RELOC(load_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
		BLIT_RELOC_UDW(batch->devid);
		ADVANCE_BATCH();
	}

	COLOR_BLIT_COPY_BATCH_START(batch->devid, 0);
	OUT_BATCH((3 << 24) | /* 32 bits */
		  (0xff << 16) |
		  128);
	OUT_BATCH(0); /* dst x1,y1 */
	OUT_BATCH((1 << 16) | 1);
	OUT_RELOC(target_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	BLIT_RELOC_UDW(batch->devid);
	OUT_BATCH(COLOR);
	ADVANCE_BATCH();

	intel_batchbuffer_flush(batch);

	/* Emit an empty batch so that signalled seqno on the target ring >
	 * signalled seqnoe on the blt ring. This is required to hit the bug. */
	BEGIN_BATCH(2);
	OUT_BATCH(MI_NOOP);
	OUT_BATCH(MI_NOOP);
	ADVANCE_BATCH();
	intel_batchbuffer_flush_on_ring(batch, ring);

	/* For the ring->ring sync it's important to only emit a read reloc, for
	 * otherwise the obj->last_write_seqno will be updated. */
	if (ring == I915_EXEC_RENDER) {
		BEGIN_BATCH(4);
		OUT_BATCH(MI_COND_BATCH_BUFFER_END | MI_DO_COMPARE);
		OUT_BATCH(0xffffffff); /* compare dword */
		OUT_RELOC(target_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
		OUT_BATCH(MI_NOOP);
		ADVANCE_BATCH();
	} else {
		BEGIN_BATCH(4);
		OUT_BATCH(MI_FLUSH_DW | 1);
		OUT_BATCH(0); /* reserved */
		OUT_RELOC(target_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
		OUT_BATCH(MI_NOOP | (1<<22) | (0xf));
		ADVANCE_BATCH();
	}
	intel_batchbuffer_flush_on_ring(batch, ring);

	gem_set_domain(fd, target_bo->handle, I915_GEM_DOMAIN_GTT, 0);
	igt_assert(*ptr == COLOR);
	drm_intel_bo_unmap(target_bo);

	drm_intel_bo_unreference(target_bo);
}

igt_main
{
	static const struct {
		const char *name;
		int ring;
	} tests[] = {
		{ "blt2render", I915_EXEC_RENDER },
		{ "blt2bsd", I915_EXEC_BSD },
		{ "blt2vebox", LOCAL_I915_EXEC_VEBOX },
	};
	int i;

	igt_skip_on_simulation();

	igt_fixture {
		fd = drm_open_any();

		/* Test requires MI_FLUSH_DW and MI_COND_BATCH_BUFFER_END */
		igt_require(intel_gen(intel_get_drm_devid(fd)) >= 6);

		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		igt_assert(bufmgr);
		/* don't enable buffer reuse!! */
		//drm_intel_bufmgr_gem_enable_reuse(bufmgr);

		batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
		igt_assert(batch);

		dummy_bo = drm_intel_bo_alloc(bufmgr, "dummy bo", 4096, 4096);
		igt_assert(dummy_bo);

		load_bo = drm_intel_bo_alloc(bufmgr, "load bo", 1024*4096, 4096);
		igt_assert(load_bo);
	}

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		igt_subtest(tests[i].name)
			run_test(tests[i].ring);
	}

	igt_fork_signal_helper();
	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		igt_subtest_f("%s-interruptible", tests[i].name)
			run_test(tests[i].ring);
	}
	igt_stop_signal_helper();

	igt_fixture {
		drm_intel_bufmgr_destroy(bufmgr);

		close(fd);
	}
}
