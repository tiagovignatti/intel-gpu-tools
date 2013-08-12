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
#include "i830_reg.h"

#define LOCAL_I915_EXEC_VEBOX (4<<0)

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
static drm_intel_bo *target_buffer;

/*
 * Testcase: Basic check of ring<->cpu sync using a dummy reloc
 *
 * The last test (that randomly switches the ring) seems to be pretty effective
 * at hitting the missed irq bug that's worked around with the HWSTAM irq write.
 */


#define MI_COND_BATCH_BUFFER_END	(0x36<<23 | 1)
#define MI_DO_COMPARE			(1<<21)
static void
dummy_reloc_loop(int ring)
{
	int i;

	for (i = 0; i < 0x100000; i++) {
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

		drm_intel_bo_map(target_buffer, 0);
		// map to force completion
		drm_intel_bo_unmap(target_buffer);
	}
}

static void
dummy_reloc_loop_random_ring(int num_rings)
{
	int i;

	srandom(0xdeadbeef);

	for (i = 0; i < 0x100000; i++) {
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

		drm_intel_bo_map(target_buffer, 0);
		// map to force waiting on rendering
		drm_intel_bo_unmap(target_buffer);
	}
}

int main(int argc, char **argv)
{
	int fd;
	int devid;
	int num_rings;

	drmtest_subtest_init(argc, argv);
	drmtest_skip_on_simulation();

	fd = drm_open_any();
	devid = intel_get_drm_devid(fd);
	num_rings = gem_get_num_rings(fd);
	if (!HAS_BLT_RING(devid)) {
		fprintf(stderr, "not (yet) implemented for pre-snb\n");
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

	drmtest_subtest_block("render") {
		printf("running dummy loop on render\n");
		dummy_reloc_loop(I915_EXEC_RENDER);
		printf("dummy loop run on render completed\n");
	}

	drmtest_subtest_block("bsd") {
		if (gem_check_bsd(fd)) {
			sleep(2);
			printf("running dummy loop on bsd\n");
			dummy_reloc_loop(I915_EXEC_BSD);
			printf("dummy loop run on bsd completed\n");
		}
	}

	drmtest_subtest_block("blt") {
		if (gem_check_blt(fd)) {
			sleep(2);
			printf("running dummy loop on blt\n");
			dummy_reloc_loop(I915_EXEC_BLT);
			printf("dummy loop run on blt completed\n");
		}
	}

	drmtest_subtest_block("vebox") {
		if (gem_check_vebox(fd)) {
			sleep(2);
			printf("running dummy loop on vebox\n");
			dummy_reloc_loop(LOCAL_I915_EXEC_VEBOX);
			printf("dummy loop run on vebox completed\n");
		}
	}

	drmtest_subtest_block("mixed") {
		if (num_rings > 1) {
			sleep(2);
			printf("running dummy loop on random rings\n");
			dummy_reloc_loop_random_ring(num_rings);
			printf("dummy loop run on random rings completed\n");
		}
	}

	drm_intel_bo_unreference(target_buffer);
	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return drmtest_retval();
}
