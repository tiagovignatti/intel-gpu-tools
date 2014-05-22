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
#include "intel_chipset.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"

static drm_intel_bufmgr *bufmgr;
static drm_intel_bo *target_bo;
static int has_ppgtt = 0;

#define SECURE_DISPATCH (1<<0)

/* Like the store dword test, but we create new command buffers each time */
static void
store_dword_loop(int divider, unsigned flags)
{
	int cmd, i, val = 0;
	uint32_t *buf;
	drm_intel_bo *cmd_bo;

	igt_info("running storedw loop with stall every %i batch\n", divider);

	cmd = MI_STORE_DWORD_IMM;
	if (!has_ppgtt)
		cmd |= MI_MEM_VIRTUAL;

	for (i = 0; i < SLOW_QUICK(0x2000, 4); i++) {
		int j = 0;
		int cmd_address_offset;
		cmd_bo = drm_intel_bo_alloc(bufmgr, "cmd bo", 4096, 4096);
		igt_assert(cmd_bo);

		/* Upload through cpu mmaps to make sure we don't have a gtt
		 * mapping which could paper over secure batch submission
		 * failing to bind that. */
		drm_intel_bo_map(cmd_bo, 1);
		buf = cmd_bo->virtual;

		buf[j++] = cmd;
		if (intel_gen(drm_intel_bufmgr_gem_get_devid(bufmgr)) >= 8) {
			cmd_address_offset = j * 4;
			buf[j++] = target_bo->offset;
			buf[j++] = 0;
		} else {
			buf[j++] = 0;
			cmd_address_offset = j * 4;
			buf[j++] = target_bo->offset;
		}
		igt_assert(j > 0);
		buf[j++] = 0x42000000 + val;

		igt_assert(drm_intel_bo_references(cmd_bo, target_bo) == 0);

		igt_assert(drm_intel_bo_emit_reloc(cmd_bo, cmd_address_offset, target_bo, 0,
					      I915_GEM_DOMAIN_INSTRUCTION,
					      I915_GEM_DOMAIN_INSTRUCTION) == 0);
		buf[j++] = MI_BATCH_BUFFER_END;
		buf[j++] = MI_BATCH_BUFFER_END;

		drm_intel_bo_unmap(cmd_bo);

		igt_assert(drm_intel_bo_references(cmd_bo, target_bo) == 1);

#define LOCAL_I915_EXEC_SECURE (1<<9)
		igt_assert(drm_intel_bo_mrb_exec(cmd_bo, j * 4, NULL, 0, 0,
					    I915_EXEC_BLT |
					    (flags & SECURE_DISPATCH ? LOCAL_I915_EXEC_SECURE : 0))
			   == 0);

		if (i % divider != 0)
			goto cont;

		drm_intel_bo_wait_rendering(cmd_bo);

		drm_intel_bo_map(target_bo, 1);

		buf = target_bo->virtual;
		igt_assert_f(buf[0] == (0x42000000 | val),
			     "value mismatch: cur 0x%08x, stored 0x%08x\n",
			     buf[0], 0x42000000 | val);

		buf[0] = 0; /* let batch write it again */
		drm_intel_bo_unmap(target_bo);

cont:
		drm_intel_bo_unreference(cmd_bo);

		val++;
	}

	igt_info("completed %d writes successfully\n", i);
}

int fd;
int devid;

igt_main
{
	igt_skip_on_simulation();

	igt_fixture {
		fd = drm_open_any();
		devid = intel_get_drm_devid(fd);

		has_ppgtt = gem_uses_aliasing_ppgtt(fd);

		/* storedw needs gtt address on gen4+/g33 and snoopable memory.
		 * Strictly speaking we could implement this now ... */
		igt_require(intel_gen(devid) >= 6);

		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		igt_assert(bufmgr);

		//	drm_intel_bufmgr_gem_enable_reuse(bufmgr);

		target_bo = drm_intel_bo_alloc(bufmgr, "target bo", 4096, 4096);
		igt_assert(target_bo);
	}

	igt_subtest("normal") {
		store_dword_loop(1, 0);
		store_dword_loop(2, 0);
		store_dword_loop(3, 0);
		store_dword_loop(5, 0);
	}

	igt_subtest("secure-dispatch") {
		store_dword_loop(1, SECURE_DISPATCH);
		store_dword_loop(2, SECURE_DISPATCH);
		store_dword_loop(3, SECURE_DISPATCH);
		store_dword_loop(5, SECURE_DISPATCH);
	}

	igt_fixture {
		drm_intel_bo_unreference(target_bo);
		drm_intel_bufmgr_destroy(bufmgr);

		close(fd);
	}
}
