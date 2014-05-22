/*
 * Copyright © 2012 Intel Corporation
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
 *    Xiang, Haihao <haihao.xiang@intel.com> (based on gem_store_dw_loop_*)
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

#define LOCAL_I915_EXEC_VEBOX (4<<0)

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
static drm_intel_bo *target_buffer;

/*
 * Testcase: Basic vebox MI check using MI_STORE_DATA_IMM
 */

static void
store_dword_loop(int divider)
{
	int cmd, i, val = 0;
	uint32_t *buf;

	igt_info("running storedw loop on blt with stall every %i batch\n", divider);

	cmd = MI_STORE_DWORD_IMM;

	for (i = 0; i < SLOW_QUICK(0x2000, 0x10); i++) {
		BEGIN_BATCH(4);
		OUT_BATCH(cmd);
		if (intel_gen(batch->devid) < 8)
			OUT_BATCH(0); /* reserved */
		OUT_RELOC(target_buffer, I915_GEM_DOMAIN_INSTRUCTION,
			  I915_GEM_DOMAIN_INSTRUCTION, 0);
		BLIT_RELOC_UDW(batch->devid);
		OUT_BATCH(val);
		ADVANCE_BATCH();

		intel_batchbuffer_flush_on_ring(batch, LOCAL_I915_EXEC_VEBOX);

		if (i % divider != 0)
			goto cont;

		drm_intel_bo_map(target_buffer, 0);

		buf = target_buffer->virtual;
		igt_assert_cmpint (buf[0], ==, val);

		drm_intel_bo_unmap(target_buffer);

cont:
		val++;
	}

	drm_intel_bo_map(target_buffer, 0);
	buf = target_buffer->virtual;

	igt_info("completed %d writes successfully, current value: 0x%08x\n", i,
			buf[0]);
	drm_intel_bo_unmap(target_buffer);
}

igt_simple_main
{
	int fd;

	fd = drm_open_any();

	igt_require(gem_has_vebox(fd));
	igt_require(gem_uses_aliasing_ppgtt(fd));

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	igt_assert(bufmgr);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
	igt_require(batch);

	target_buffer = drm_intel_bo_alloc(bufmgr, "target bo", 4096, 4096);
	igt_require(target_buffer);

	store_dword_loop(1);
	store_dword_loop(2);
	if (!igt_run_in_simulation()) {
		store_dword_loop(3);
		store_dword_loop(5);
	}

	drm_intel_bo_unreference(target_buffer);
	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);
}
