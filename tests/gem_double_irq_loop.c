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
 *    Daniel Vetter <daniel.vetter@ffwll.ch> (based on gem_storedw_*.c)
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
#include "i830_reg.h"
#include "intel_chipset.h"

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
static drm_intel_bo *target_buffer, *blt_bo;

/*
 * Testcase: Basic check for missed irqs on blt
 *
 * Execs one large and then immediately a tiny batch on the blt ring. Then waits
 * on the second batch. This hopefully catches races in our irq acknowledgement.
 */


#define MI_COND_BATCH_BUFFER_END	(0x36<<23 | 1)
#define MI_DO_COMPARE			(1<<21)
static void
dummy_reloc_loop(void)
{
	int i;

	for (i = 0; i < 0x800; i++) {
		BLIT_COPY_BATCH_START(batch->devid, 0);
		OUT_BATCH((3 << 24) | /* 32 bits */
			  (0xcc << 16) | /* copy ROP */
			  4*4096);
		OUT_BATCH(2048 << 16 | 0);
		OUT_BATCH((4096) << 16 | (2048));
		OUT_RELOC_FENCED(blt_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
		BLIT_RELOC_UDW(batch->devid);
		OUT_BATCH(0 << 16 | 0);
		OUT_BATCH(4*4096);
		OUT_RELOC_FENCED(blt_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
		BLIT_RELOC_UDW(batch->devid);
		ADVANCE_BATCH();
		intel_batchbuffer_flush(batch);

		BEGIN_BATCH(4);
		OUT_BATCH(MI_FLUSH_DW | 1);
		OUT_BATCH(0); /* reserved */
		OUT_RELOC(target_buffer, I915_GEM_DOMAIN_RENDER,
				I915_GEM_DOMAIN_RENDER, 0);
		OUT_BATCH(MI_NOOP | (1<<22) | (0xf));
		ADVANCE_BATCH();
		intel_batchbuffer_flush(batch);

		drm_intel_bo_map(target_buffer, 0);
		// map to force completion
		drm_intel_bo_unmap(target_buffer);
	}
}

igt_simple_main
{
	int fd;
	int devid;

	igt_skip_on_simulation();

	fd = drm_open_any();
	devid = intel_get_drm_devid(fd);
	igt_require_f(HAS_BLT_RING(devid),
		      "not (yet) implemented for pre-snb\n");

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	igt_assert(bufmgr);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	batch = intel_batchbuffer_alloc(bufmgr, devid);
	igt_assert(batch);

	target_buffer = drm_intel_bo_alloc(bufmgr, "target bo", 4096, 4096);
	igt_assert(target_buffer);

	blt_bo = drm_intel_bo_alloc(bufmgr, "target bo", 4*4096*4096, 4096);
	igt_assert(blt_bo);

	dummy_reloc_loop();

	drm_intel_bo_unreference(target_buffer);
	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);
}
