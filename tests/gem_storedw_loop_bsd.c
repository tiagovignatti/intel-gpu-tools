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
struct intel_batchbuffer *batch;
static drm_intel_bo *target_buffer;
static int has_ppgtt = 0;

/*
 * Testcase: Basic bsd MI check using MI_STORE_DATA_IMM
 */

static void
store_dword_loop(void)
{
	int cmd, i, val = 0;
	uint32_t *buf;

	cmd = MI_STORE_DWORD_IMM;
	if (!has_ppgtt)
		cmd |= MI_MEM_VIRTUAL;

	for (i = 0; i < 0x100000; i++) {
		BEGIN_BATCH(4);
		OUT_BATCH(cmd);
		OUT_BATCH(0); /* reserved */
		OUT_RELOC(target_buffer, I915_GEM_DOMAIN_INSTRUCTION,
			  I915_GEM_DOMAIN_INSTRUCTION, 0);
		OUT_BATCH(val);
		ADVANCE_BATCH();

		intel_batchbuffer_flush_on_ring(batch, I915_EXEC_BSD);

		drm_intel_bo_map(target_buffer, 0);

		buf = target_buffer->virtual;
		if (buf[0] != val) {
			fprintf(stderr,
				"value mismatch: cur 0x%08x, stored 0x%08x\n",
				buf[0], val);
			exit(-1);
		}

		drm_intel_bo_unmap(target_buffer);

		val++;
	}

	drm_intel_bo_map(target_buffer, 0);
	buf = target_buffer->virtual;

	printf("completed %d writes successfully, current value: 0x%08x\n", i,
			buf[0]);
	drm_intel_bo_unmap(target_buffer);
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

		fprintf(stderr, "MI_STORE_DATA can only use GTT address on gen4+/g33 and "
			"needs snoopable mem on pre-gen6\n");
		return 77;
	}

	if (IS_GEN6(devid)) {

		fprintf(stderr, "MI_STORE_DATA broken on gen6 bsd\n");
		return 77;
	}

	/* This only works with ppgtt */
	if (!has_ppgtt) {
		fprintf(stderr, "no ppgtt detected, which is required\n");
		return 77;
	}

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	if (!bufmgr) {
		fprintf(stderr, "failed to init libdrm\n");
		exit(-1);
	}
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	batch = intel_batchbuffer_alloc(bufmgr, devid);
	if (!batch) {
		fprintf(stderr, "failed to create batch buffer\n");
		exit(-1);
	}

	target_buffer = drm_intel_bo_alloc(bufmgr, "target bo", 4096, 4096);
	if (!target_buffer) {
		fprintf(stderr, "failed to alloc target buffer\n");
		exit(-1);
	}

	store_dword_loop();

	drm_intel_bo_unreference(target_buffer);
	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
