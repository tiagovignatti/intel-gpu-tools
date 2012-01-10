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

/*
 * Testcase: Kernel relocations vs. gpu races
 *
 */

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;

uint32_t blob[2048*2048];
#define NUM_TARGET_BOS 16
drm_intel_bo *pc_target_bo[NUM_TARGET_BOS];
drm_intel_bo *dummy_bo;
drm_intel_bo *special_bo;
uint32_t devid;
int special_reloc_ofs;
int special_batch_len;

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

static void create_special_bo(void)
{
	uint32_t data[1024];
	int len = 0;
	int small_pitch = 64;
#define BATCH(dw) data[len++] = (dw);

	memset(data, 0, 4096);
	special_bo = drm_intel_bo_alloc(bufmgr, "special batch", 4096, 4096);

	BATCH(XY_COLOR_BLT_CMD | COLOR_BLT_WRITE_ALPHA | XY_COLOR_BLT_WRITE_RGB);
	BATCH((3 << 24) | (0xf0 << 16) | small_pitch);
	BATCH(0);
	BATCH(1 << 16 | 1);
	special_reloc_ofs = 4*len;
	BATCH(0);
	BATCH(0xdeadbeef);

#define CMD_POLY_STIPPLE_OFFSET       0x7906
	/* batchbuffer end */
	if (IS_GEN5(batch->devid)) {
		BATCH(CMD_POLY_STIPPLE_OFFSET << 16);
		BATCH(0);
	}
	assert(len % 2 == 0);
	BATCH(MI_NOOP);
	BATCH(MI_BATCH_BUFFER_END);

	drm_intel_bo_subdata(special_bo, 0, 4096, data);
	special_batch_len = len*4;
}

static void emit_dummy_load(int pitch)
{
	int i;
	uint32_t tile_flags = 0;

	if (IS_965(devid)) {
		pitch /= 4;
		tile_flags = XY_SRC_COPY_BLT_SRC_TILED |
			XY_SRC_COPY_BLT_DST_TILED;
	}

	for (i = 0; i < 10; i++) {
		BEGIN_BATCH(8);
		OUT_BATCH(XY_SRC_COPY_BLT_CMD |
			  XY_SRC_COPY_BLT_WRITE_ALPHA |
			  XY_SRC_COPY_BLT_WRITE_RGB |
			  tile_flags);
		OUT_BATCH((3 << 24) | /* 32 bits */
			  (0xcc << 16) | /* copy ROP */
			  pitch);
		OUT_BATCH(0 << 16 | 1024);
		OUT_BATCH((2048) << 16 | (2048));
		OUT_RELOC_FENCED(dummy_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
		OUT_BATCH(0 << 16 | 0);
		OUT_BATCH(pitch);
		OUT_RELOC_FENCED(dummy_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
		ADVANCE_BATCH();

		if (IS_GEN6(devid) || IS_GEN7(devid)) {
			BEGIN_BATCH(3);
			OUT_BATCH(XY_SETUP_CLIP_BLT_CMD);
			OUT_BATCH(0);
			OUT_BATCH(0);
			ADVANCE_BATCH();
		}
	}
	intel_batchbuffer_flush(batch);
}

#define MAX_BLT_SIZE 128
int main(int argc, char **argv)
{
	uint32_t tiling_mode = I915_TILING_X;
	unsigned long pitch, act_size;
	int fd, i, ring;
	uint32_t test;

	memset(blob, 'A', sizeof(blob));

	fd = drm_open_any();

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	/* disable reuse, otherwise the test fails */
	//drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	devid = intel_get_drm_devid(fd);
	batch = intel_batchbuffer_alloc(bufmgr, devid);

	act_size = 2048;
	dummy_bo = drm_intel_bo_alloc_tiled(bufmgr, "tiled dummy_bo", act_size, act_size,
				      4, &tiling_mode, &pitch, 0);

	drm_intel_bo_subdata(dummy_bo, 0, act_size*act_size*4, blob);

	create_special_bo();

	if (intel_gen(devid) >= 6)
		ring = I915_EXEC_BLT;
	else
		ring = 0;

	for (i = 0; i < NUM_TARGET_BOS; i++) {
		pc_target_bo[i] = drm_intel_bo_alloc(bufmgr, "special batch", 4096, 4096);
		emit_dummy_load(pitch);
		assert(pc_target_bo[i]->offset == 0);
		drm_intel_bo_emit_reloc(special_bo, special_reloc_ofs,
					pc_target_bo[i],
					0,
					I915_GEM_DOMAIN_RENDER,
					I915_GEM_DOMAIN_RENDER);
		drm_intel_bo_mrb_exec(special_bo, special_batch_len, NULL,
				      0, 0, ring);
	}

	/* Only check at the end to avoid unnecessary synchronous behaviour. */
	for (i = 0; i < NUM_TARGET_BOS; i++) {
		drm_intel_bo_get_subdata(pc_target_bo[i], 0, 4, &test);
		if (test != 0xdeadbeef) {
			fprintf(stderr, "mismatch in buffer %i: 0x%08x instead of 0xdeadbeef\n", i, test);
			exit(1);
		}
		drm_intel_bo_unreference(pc_target_bo[i]);
	}

	drm_intel_gem_bo_map_gtt(dummy_bo);
	drm_intel_gem_bo_unmap_gtt(dummy_bo);

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
