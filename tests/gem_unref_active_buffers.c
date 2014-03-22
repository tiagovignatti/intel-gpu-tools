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
 * Testcase: Unreferencing of active buffers
 *
 * Execs buffers and immediately unreferences them, hence the kernel active list
 * will be the last one to hold a reference on them. Usually libdrm bo caching
 * prevents that by keeping another reference.
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

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
static drm_intel_bo *load_bo;

igt_simple_main
{
	int fd, i;

	igt_skip_on_simulation();

	fd = drm_open_any();

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	igt_assert(bufmgr);
	/* don't enable buffer reuse!! */
	//drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
	igt_assert(batch);

	/* put some load onto the gpu to keep the light buffers active for long
	 * enough */
	for (i = 0; i < 1000; i++) {
		load_bo = drm_intel_bo_alloc(bufmgr, "target bo", 1024*4096, 4096);
		igt_assert(load_bo);

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

		intel_batchbuffer_flush(batch);

		drm_intel_bo_disable_reuse(load_bo);
		drm_intel_bo_unreference(load_bo);
	}

	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);
}
