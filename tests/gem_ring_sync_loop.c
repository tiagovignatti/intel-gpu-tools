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
static drm_intel_bo *target_buffer;

/*
 * Testcase: Basic check of ring<->ring sync using a dummy reloc
 *
 * Extremely efficient at catching missed irqs with semaphores=0 ...
 */

#define MI_COND_BATCH_BUFFER_END	(0x36<<23 | 1)
#define MI_DO_COMPARE			(1<<21)

static void
store_dword_loop(int fd)
{
	int i;
	int num_rings = gem_get_num_rings(fd);

	srandom(0xdeadbeef);

	for (i = 0; i < SLOW_QUICK(0x100000, 10); i++) {
		int ring = random() % num_rings + 1;

		if (ring == I915_EXEC_RENDER) {
			BEGIN_BATCH(4);
			OUT_BATCH(MI_COND_BATCH_BUFFER_END | MI_DO_COMPARE);
			OUT_BATCH(0xffffffff); /* compare dword */
			OUT_RELOC(target_buffer, I915_GEM_DOMAIN_RENDER,
					I915_GEM_DOMAIN_RENDER, 0);
			OUT_BATCH(MI_NOOP);
			ADVANCE_BATCH();
		} else {
			BEGIN_BATCH(4);
			OUT_BATCH(MI_FLUSH_DW | 1);
			OUT_BATCH(0); /* reserved */
			OUT_RELOC(target_buffer, I915_GEM_DOMAIN_RENDER,
					I915_GEM_DOMAIN_RENDER, 0);
			OUT_BATCH(MI_NOOP | (1<<22) | (0xf));
			ADVANCE_BATCH();
		}
		intel_batchbuffer_flush_on_ring(batch, ring);
	}

	drm_intel_bo_map(target_buffer, 0);
	// map to force waiting on rendering
	drm_intel_bo_unmap(target_buffer);
}

igt_simple_main
{
	int fd;
	int devid;

	fd = drm_open_any();
	devid = intel_get_drm_devid(fd);
	gem_require_ring(fd, I915_EXEC_BLT);


	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	igt_assert(bufmgr);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	batch = intel_batchbuffer_alloc(bufmgr, devid);
	igt_assert(batch);

	target_buffer = drm_intel_bo_alloc(bufmgr, "target bo", 4096, 4096);
	igt_assert(target_buffer);

	store_dword_loop(fd);

	drm_intel_bo_unreference(target_buffer);
	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);
}
