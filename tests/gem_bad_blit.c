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
 *
 */

/** @file gem_tiled_blits.c
 *
 * This is a test of doing many tiled blits, with a working set
 * larger than the aperture size.
 *
 * The goal is to catch a couple types of failure;
 * - Fence management problems on pre-965.
 * - A17 or L-shaped memory tiling workaround problems in acceleration.
 *
 * The model is to fill a collection of 1MB objects in a way that can't trip
 * over A6 swizzling -- upload data to a non-tiled object, blit to the tiled
 * object.  Then, copy the 1MB objects randomly between each other for a while.
 * Finally, download their data through linear objects again and see what
 * resulted.
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
#include "intel_chipset.h"
#include "intel_io.h"

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;

#define BAD_GTT_DEST ((256*1024*1024)) /* past end of aperture */

static void
bad_blit(drm_intel_bo *src_bo, uint32_t devid)
{
	uint32_t src_pitch = 512, dst_pitch = 512;
	uint32_t cmd_bits = 0;

	if (IS_965(devid)) {
		src_pitch /= 4;
		cmd_bits |= XY_SRC_COPY_BLT_SRC_TILED;
	}

	if (IS_965(devid)) {
		dst_pitch /= 4;
		cmd_bits |= XY_SRC_COPY_BLT_DST_TILED;
	}

	BLIT_COPY_BATCH_START(devid, cmd_bits);
	OUT_BATCH((3 << 24) | /* 32 bits */
		  (0xcc << 16) | /* copy ROP */
		  dst_pitch);
	OUT_BATCH(0); /* dst x1,y1 */
	OUT_BATCH((64 << 16) | 64); /* 64x64 blit */
	OUT_BATCH(BAD_GTT_DEST);
	BLIT_RELOC_UDW(devid);
	OUT_BATCH(0); /* src x1,y1 */
	OUT_BATCH(src_pitch);
	OUT_RELOC(src_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
	BLIT_RELOC_UDW(devid);
	ADVANCE_BATCH();

	intel_batchbuffer_flush(batch);
}

int main(int argc, char **argv)
{
	drm_intel_bo *src;
	int fd;

	fd = drm_open_any();

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));

	src = drm_intel_bo_alloc(bufmgr, "src", 128 * 128, 4096);

	bad_blit(src, batch->devid);

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
