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

/*
 * Testcase: (TLB-)Coherency of pipe_control QW writes
 *
 * Writes a counter-value into an always newly allocated target bo (by disabling
 * buffer reuse). Decently trashes on tlb inconsistencies, too.
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
uint32_t devid;

#define GFX_OP_PIPE_CONTROL	((0x3<<29)|(0x3<<27)|(0x2<<24)|2)
#define   PIPE_CONTROL_WRITE_IMMEDIATE	(1<<14)
#define   PIPE_CONTROL_WRITE_TIMESTAMP	(3<<14)
#define   PIPE_CONTROL_DEPTH_STALL (1<<13)
#define   PIPE_CONTROL_WC_FLUSH	(1<<12)
#define   PIPE_CONTROL_IS_FLUSH	(1<<11) /* MBZ on Ironlake */
#define   PIPE_CONTROL_TC_FLUSH (1<<10) /* GM45+ only */
#define   PIPE_CONTROL_STALL_AT_SCOREBOARD (1<<1)
#define   PIPE_CONTROL_CS_STALL	(1<<20)
#define   PIPE_CONTROL_GLOBAL_GTT (1<<2) /* in addr dword */

/* Like the store dword test, but we create new command buffers each time */
static void
store_pipe_control_loop(void)
{
	int i, val = 0;
	uint32_t *buf;
	drm_intel_bo *target_bo;

	for (i = 0; i < 0x10000; i++) {
		/* we want to check tlb consistency of the pipe_control target,
		 * so get a new buffer every time around */
		target_bo = drm_intel_bo_alloc(bufmgr, "target bo", 4096, 4096);
		if (!target_bo) {
			fprintf(stderr, "failed to alloc target buffer\n");
			exit(-1);
		}

		/* gem_storedw_batches_loop.c is a bit overenthusiastic with
		 * creating new batchbuffers - with buffer reuse disabled, the
		 * support code will do that for us. */
		if (intel_gen(devid) >= 6) {
			/* work-around hw issue, see intel_emit_post_sync_nonzero_flush
			 * in mesa sources. */
			BEGIN_BATCH(4);
			OUT_BATCH(GFX_OP_PIPE_CONTROL);
			OUT_BATCH(PIPE_CONTROL_CS_STALL |
			     PIPE_CONTROL_STALL_AT_SCOREBOARD);
			OUT_BATCH(0); /* address */
			OUT_BATCH(0); /* write data */
			ADVANCE_BATCH();

			BEGIN_BATCH(4);
			OUT_BATCH(GFX_OP_PIPE_CONTROL);
			OUT_BATCH(PIPE_CONTROL_WRITE_IMMEDIATE);
			OUT_RELOC(target_bo,
			     I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION, 
			     PIPE_CONTROL_GLOBAL_GTT);
			OUT_BATCH(val); /* write data */
			ADVANCE_BATCH();
		} else if (intel_gen(devid) >= 4) {
			BEGIN_BATCH(4);
			OUT_BATCH(GFX_OP_PIPE_CONTROL | PIPE_CONTROL_WC_FLUSH |
					PIPE_CONTROL_TC_FLUSH |
					PIPE_CONTROL_WRITE_IMMEDIATE | 2);
			OUT_RELOC(target_bo,
				I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
				PIPE_CONTROL_GLOBAL_GTT);
			OUT_BATCH(val);
			OUT_BATCH(0xdeadbeef);
			ADVANCE_BATCH();
		}

		intel_batchbuffer_flush_on_ring(batch, 0);

		drm_intel_bo_map(target_bo, 1);

		buf = target_bo->virtual;
		if (buf[0] != val) {
			fprintf(stderr,
				"value mismatch: cur 0x%08x, stored 0x%08x\n",
				buf[0], val);
			exit(-1);
		}
		buf[0] = 0; /* let batch write it again */
		drm_intel_bo_unmap(target_bo);

		drm_intel_bo_unreference(target_bo);

		val++;
	}

	printf("completed %d writes successfully\n", i);
}

int main(int argc, char **argv)
{
	int fd;

	if (argc != 1) {
		fprintf(stderr, "usage: %s\n", argv[0]);
		exit(-1);
	}

	fd = drm_open_any();
	devid = intel_get_drm_devid(fd);

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	if (!bufmgr) {
		fprintf(stderr, "failed to init libdrm\n");
		exit(-1);
	}

	if (IS_GEN2(devid) || IS_GEN3(devid)) {
		fprintf(stderr, "no pipe_control on gen2/3\n");
		return 77;
	}
	if (devid == PCI_CHIP_I965_G) {
		fprintf(stderr, "pipe_control totally broken on i965\n");
		return 77;
	}
	/* IMPORTANT: No call to
	 * drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	 * here because we wan't to have fresh buffers (to trash the tlb)
	 * every time! */

	batch = intel_batchbuffer_alloc(bufmgr, devid);
	if (!batch) {
		fprintf(stderr, "failed to create batch buffer\n");
		exit(-1);
	}

	store_pipe_control_loop();

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
