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

/** @file gem_ringfill.c
 *
 * This is a test of doing many tiny batchbuffer operations, in the hope of
 * catching failure to manage the ring properly near full.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <drm.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_chipset.h"
#include "intel_io.h"
#include "igt_aux.h"

struct bo {
	const char *ring;
	drm_intel_bo *src, *dst, *tmp;
};

static const int width = 512, height = 512;

static void create_bo(drm_intel_bufmgr *bufmgr,
		      struct bo *b,
		      const char *ring)
{
	int size = 4 * width * height, i;
	uint32_t *map;

	b->ring = ring;
	b->src = drm_intel_bo_alloc(bufmgr, "src", size, 4096);
	b->dst = drm_intel_bo_alloc(bufmgr, "dst", size, 4096);
	b->tmp = drm_intel_bo_alloc(bufmgr, "tmp", size, 4096);

	/* Fill the src with indexes of the pixels */
	drm_intel_bo_map(b->src, true);
	map = b->src->virtual;
	for (i = 0; i < width * height; i++)
		map[i] = i;
	drm_intel_bo_unmap(b->src);

	/* Fill the dst with garbage. */
	drm_intel_bo_map(b->dst, true);
	map = b->dst->virtual;
	for (i = 0; i < width * height; i++)
		map[i] = 0xd0d0d0d0;
	drm_intel_bo_unmap(b->dst);
}

static int check_bo(struct bo *b)
{
	const uint32_t *map;
	int i, fails = 0;

	drm_intel_bo_map(b->dst, false);
	map = b->dst->virtual;
	for (i = 0; i < width*height; i++) {
		if (map[i] != i && ++fails <= 9) {
			int x = i % width;
			int y = i / width;

			igt_info("%s: copy #%d at %d,%d failed: read 0x%08x\n",
				 b->ring, i, x, y, map[i]);
		}
	}
	drm_intel_bo_unmap(b->dst);

	return fails;
}

static void destroy_bo(struct bo *b)
{
	drm_intel_bo_unreference(b->src);
	drm_intel_bo_unreference(b->tmp);
	drm_intel_bo_unreference(b->dst);
}

static int check_ring(drm_intel_bufmgr *bufmgr,
		      struct intel_batchbuffer *batch,
		      const char *ring,
		      igt_render_copyfunc_t copy)
{
	struct igt_buf src, tmp, dst;
	struct bo bo;
	char output[100];
	int i;

	snprintf(output, 100, "filling %s ring: ", ring);

	create_bo(bufmgr, &bo, ring);

	src.stride = 4 * width;
	src.tiling = 0;
	src.size = 4 * width * height;
	src.num_tiles = 4 * width * height;
	dst = tmp = src;

	src.bo = bo.src;
	tmp.bo = bo.tmp;
	dst.bo = bo.dst;

	/* The ring we've been using is 128k, and each rendering op
	 * will use at least 8 dwords:
	 *
	 * BATCH_START
	 * BATCH_START offset
	 * MI_FLUSH
	 * STORE_DATA_INDEX
	 * STORE_DATA_INDEX offset
	 * STORE_DATA_INDEX value
	 * MI_USER_INTERRUPT
	 * (padding)
	 *
	 * So iterate just a little more than that -- if we don't fill the ring
	 * doing this, we aren't likely to with this test.
	 */
	for (i = 0; i < width * height; i++) {
		int x = i % width;
		int y = i / width;

		igt_progress(output, i, width*height);

		igt_assert(y < height);

		/* Dummy load to fill the ring */
		copy(batch, NULL, &src, 0, 0, width, height, &tmp, 0, 0);
		/* And copy the src into dst, pixel by pixel */
		copy(batch, NULL, &src, x, y, 1, 1, &dst, x, y);
	}

	/* verify */
	igt_info("verifying\n");
	i = check_bo(&bo);
	destroy_bo(&bo);

	return i;
}

static void blt_copy(struct intel_batchbuffer *batch,
		     drm_intel_context *context,
		     struct igt_buf *src, unsigned src_x, unsigned src_y,
		     unsigned w, unsigned h,
		     struct igt_buf *dst, unsigned dst_x, unsigned dst_y)
{
	BLIT_COPY_BATCH_START(batch->devid, 0);
	OUT_BATCH((3 << 24) | /* 32 bits */
		  (0xcc << 16) | /* copy ROP */
		  dst->stride);
	OUT_BATCH((dst_y << 16) | dst_x); /* dst x1,y1 */
	OUT_BATCH(((dst_y + h) << 16) | (dst_x + w)); /* dst x2,y2 */
	OUT_RELOC(dst->bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	BLIT_RELOC_UDW(batch->devid);
	OUT_BATCH((src_y << 16) | src_x); /* src x1,y1 */
	OUT_BATCH(src->stride);
	OUT_RELOC(src->bo, I915_GEM_DOMAIN_RENDER, 0, 0);
	BLIT_RELOC_UDW(batch->devid);
	ADVANCE_BATCH();

	intel_batchbuffer_flush(batch);
}

drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
int fd;

igt_main
{
	igt_skip_on_simulation();

	igt_fixture {
		fd = drm_open_any();

		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		drm_intel_bufmgr_gem_enable_reuse(bufmgr);
		batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
	}

	igt_subtest("blitter")
		check_ring(bufmgr, batch, "blt", blt_copy);

	/* Strictly only required on architectures with a separate BLT ring,
	 * but lets stress everybody.
	 */
	igt_subtest("render") {
		igt_render_copyfunc_t copy;

		copy = igt_get_render_copyfunc(batch->devid);
		igt_require(copy);

		check_ring(bufmgr, batch, "render", copy);
	}

	igt_fixture {
		intel_batchbuffer_free(batch);
		drm_intel_bufmgr_destroy(bufmgr);

		close(fd);
	}
}
