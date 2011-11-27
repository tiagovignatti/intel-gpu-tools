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
 * Testcase: Provoke the hangcheck timer on an otherwise idle system
 *
 * This tries to hit forcewake locking bugs when the hangcheck runs. Somehow we
 * often luck out and the hangcheck runs while someone else is already holding
 * the dev->struct_mutex.
 *
 * It's imperative that nothing else runs while this test runs, i.e. kill your X
 * session, please.
 */

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;

uint32_t blob[2048*2048];

#define MAX_BLT_SIZE 128
int main(int argc, char **argv)
{
	drm_intel_bo *bo = NULL;
	uint32_t tiling_mode = I915_TILING_X;
	unsigned long pitch, act_size;
	int fd, i, devid;

	memset(blob, 'A', sizeof(blob));

	fd = drm_open_any();

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	devid = intel_get_drm_devid(fd);
	batch = intel_batchbuffer_alloc(bufmgr, devid);

	act_size = 2048;
	printf("filling ring\n");
	drm_intel_bo_unreference(bo);
	bo = drm_intel_bo_alloc_tiled(bufmgr, "tiled bo", act_size, act_size,
				      4, &tiling_mode, &pitch, 0);

	drm_intel_bo_subdata(bo, 0, act_size*act_size*4, blob);

	if (IS_965(devid))
		pitch /= 4;

	for (i = 0; i < 10000; i++) {
		BEGIN_BATCH(8);
		OUT_BATCH(XY_SRC_COPY_BLT_CMD |
			  XY_SRC_COPY_BLT_WRITE_ALPHA |
			  XY_SRC_COPY_BLT_WRITE_RGB |
			  XY_SRC_COPY_BLT_SRC_TILED |
			  XY_SRC_COPY_BLT_DST_TILED);
		OUT_BATCH((3 << 24) | /* 32 bits */
			  (0xcc << 16) | /* copy ROP */
			  pitch);
		OUT_BATCH(0 << 16 | 1024);
		OUT_BATCH((2048) << 16 | (2048));
		OUT_RELOC_FENCED(bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
		OUT_BATCH(0 << 16 | 0);
		OUT_BATCH(pitch);
		OUT_RELOC_FENCED(bo, I915_GEM_DOMAIN_RENDER, 0, 0);
		ADVANCE_BATCH();

		if (IS_GEN6(devid) || IS_GEN7(devid)) {
			BEGIN_BATCH(3);
			OUT_BATCH(XY_SETUP_CLIP_BLT_CMD);
			OUT_BATCH(0);
			OUT_BATCH(0);
			ADVANCE_BATCH();
		}
	}

	printf("waiting\n");
	sleep(10);

	printf("done waiting, check dmesg\n");
	drm_intel_bo_unreference(bo);

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
