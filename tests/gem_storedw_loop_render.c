/*
 * Copyright © 2009 Intel Corporation
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
 *    Eric Anholt <eric@anholt.net>
 *    Jesse Barnes <jbarnes@virtuousgeek.org> (based on gem_bad_blit.c)
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

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
static drm_intel_bo *target_buffer;
static int has_ppgtt = 0;

/*
 * Testcase: Basic render MI check using MI_STORE_DATA_IMM
 */

static void
emit_store_dword_imm(int devid, drm_intel_bo *dest, uint32_t val)
{
	int cmd;
	cmd = MI_STORE_DWORD_IMM;
	if (!has_ppgtt)
		cmd |= MI_MEM_VIRTUAL;

	if (intel_gen(devid) >= 8) {
		BEGIN_BATCH(4);
		OUT_BATCH(cmd);
		OUT_RELOC(dest, I915_GEM_DOMAIN_INSTRUCTION,
			  I915_GEM_DOMAIN_INSTRUCTION, 0);
		OUT_BATCH(0);
		OUT_BATCH(val);
		ADVANCE_BATCH();
	} else {
		BEGIN_BATCH(4);
		OUT_BATCH(cmd);
		OUT_BATCH(0); /* reserved */
		OUT_RELOC(dest, I915_GEM_DOMAIN_INSTRUCTION,
			  I915_GEM_DOMAIN_INSTRUCTION, 0);
		OUT_BATCH(val);
		ADVANCE_BATCH();
	}
}

static void
store_dword_loop(int devid, int divider)
{
	int i, val = 0;
	uint32_t *buf;

	igt_info("running storedw loop on render with stall every %i batch\n", divider);

	for (i = 0; i < SLOW_QUICK(0x2000, 0x10); i++) {
		emit_store_dword_imm(devid, target_buffer, val);
		intel_batchbuffer_flush_on_ring(batch, 0);

		if (i % divider != 0)
			goto cont;

		drm_intel_bo_map(target_buffer, 0);

		buf = target_buffer->virtual;
		igt_assert_f(buf[0] == val,
			     "value mismatch: cur 0x%08x, stored 0x%08x\n",
			     buf[0], val);

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
	int devid;

	fd = drm_open_any();
	devid = intel_get_drm_devid(fd);

	has_ppgtt = gem_uses_aliasing_ppgtt(fd);

	igt_skip_on_f(intel_gen(devid) < 6,
		      "MI_STORE_DATA can only use GTT address on gen4+/g33 and "
		      "needs snoopable mem on pre-gen6\n");

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	igt_assert(bufmgr);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	batch = intel_batchbuffer_alloc(bufmgr, devid);
	igt_assert(batch);

	target_buffer = drm_intel_bo_alloc(bufmgr, "target bo", 4096, 4096);
	igt_assert(target_buffer);

	store_dword_loop(devid, 1);
	store_dword_loop(devid, 2);
	if (!igt_run_in_simulation()) {
		store_dword_loop(devid, 3);
		store_dword_loop(devid, 5);
	}

	drm_intel_bo_unreference(target_buffer);
	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);
}
