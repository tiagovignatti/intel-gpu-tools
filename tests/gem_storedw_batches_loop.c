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

static drm_intel_bufmgr *bufmgr;
static drm_intel_bo *target_bo;
static int has_ppgtt = 0;

/* Like the store dword test, but we create new command buffers each time */
static void
store_dword_loop(void)
{
	int cmd, i, val = 0, ret;
	uint32_t *buf;
	drm_intel_bo *cmd_bo;

	cmd = MI_STORE_DWORD_IMM;
	if (!has_ppgtt)
		cmd |= MI_MEM_VIRTUAL;

	for (i = 0; i < 0x80000; i++) {
		cmd_bo = drm_intel_bo_alloc(bufmgr, "cmd bo", 4096, 4096);
		if (!cmd_bo) {
			fprintf(stderr, "failed to alloc cmd bo\n");
			exit(-1);
		}

		drm_intel_bo_map(cmd_bo, 1);
		buf = cmd_bo->virtual;

		buf[0] = cmd;
		buf[1] = 0;
		buf[2] = target_bo->offset;
		buf[3] = 0x42000000 + val;

		ret = drm_intel_bo_references(cmd_bo, target_bo);
		if (ret) {
			fprintf(stderr, "failed to link cmd & target bos\n");
			exit(-1);
		}

		ret = drm_intel_bo_emit_reloc(cmd_bo, 8, target_bo, 0,
					      I915_GEM_DOMAIN_INSTRUCTION,
					      I915_GEM_DOMAIN_INSTRUCTION);
		if (ret) {
			fprintf(stderr, "failed to emit reloc\n");
			exit(-1);
		}

		buf[4] = MI_BATCH_BUFFER_END;
		buf[5] = MI_BATCH_BUFFER_END;

		drm_intel_bo_unmap(cmd_bo);

		ret = drm_intel_bo_references(cmd_bo, target_bo);
		if (ret != 1) {
			fprintf(stderr, "bad bo reference count: %d\n", ret);
			exit(-1);
		}

		ret = drm_intel_bo_exec(cmd_bo, 6 * 4, NULL, 0, 0);
		if (ret) {
			fprintf(stderr, "bo exec failed: %d\n", ret);
			exit(-1);
		}

		drm_intel_bo_wait_rendering(cmd_bo);

		drm_intel_bo_map(target_bo, 1);

		buf = target_bo->virtual;
		if (buf[0] != (0x42000000 | val)) {
			fprintf(stderr,
				"value mismatch: cur 0x%08x, stored 0x%08x\n",
				buf[0], 0x42000000 | val);
			exit(-1);
		}
		buf[0] = 0; /* let batch write it again */
		drm_intel_bo_unmap(target_bo);

		drm_intel_bo_unreference(cmd_bo);

		val++;
	}

	printf("completed %d writes successfully\n", i);
}

int main(int argc, char **argv)
{
	int fd;
	int devid;

	if (argc != 1) {
		fprintf(stderr, "usage: %s\n", argv[0]);
		exit(-1);
	}

	fd = drm_open_any();
	devid = intel_get_drm_devid(fd);

	has_ppgtt = gem_uses_aliasing_ppgtt(fd);

	if (IS_GEN2(devid) || IS_GEN3(devid) || IS_GEN4(devid) || IS_GEN5(devid)) {

		fprintf(stderr, "MI_STORE_DATA can only use GTT address on gen4+/g33 and"
			"needs snoopable mem on pre-gen6\n");
		return 77;
	}


	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	if (!bufmgr) {
		fprintf(stderr, "failed to init libdrm\n");
		exit(-1);
	}
//	drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	target_bo = drm_intel_bo_alloc(bufmgr, "target bo", 4096, 4096);
	if (!target_bo) {
		fprintf(stderr, "failed to alloc target buffer\n");
		exit(-1);
	}

	store_dword_loop();

	drm_intel_bo_unreference(target_bo);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
