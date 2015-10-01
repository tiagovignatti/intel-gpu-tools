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

/**
 * Roughly simulates Mesa's current vertex buffer behavior: do a series of
 * small pwrites on a moderately-sized buffer, then render using it.
 *
 * The vertex buffer uploads
 *
 * You might think of this like a movie player, but that wouldn't be entirely
 * accurate, since the access patterns of the memory would be different
 * (generally, smaller source image, upscaled, an thus different memory access
 * pattern in both texel fetch for the stretching and the destination writes).
 * However, some things like swfdec would be doing something like this since
 * they compute their data in host memory and upload the full sw rendered
 * frame.
 */

#include "igt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

/* Happens to be 128k, the size of the VBOs used by i965's Mesa driver. */
#define OBJECT_WIDTH	256
#define OBJECT_HEIGHT	128

static double
get_time_in_secs(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return (double)tv.tv_sec + tv.tv_usec / 1000000.0;
}

static void
do_render(drm_intel_bufmgr *bufmgr, struct intel_batchbuffer *batch,
	  drm_intel_bo *dst_bo, int width, int height)
{
	uint32_t data[64];
	drm_intel_bo *src_bo;
	int i;
	static uint32_t seed = 1;

	src_bo = drm_intel_bo_alloc(bufmgr, "src", width * height * 4, 4096);

	/* Upload some junk.  Real workloads would be doing a lot more
	 * work to generate the junk.
	 */
	for (i = 0; i < width * height;) {
		int size, j;

		/* Choose a size from 1 to 64 dwords to upload.
		 * Normal workloads have a distribution of sizes with a
		 * large tail (something in your scene's going to have a big
		 * pile of vertices, most likely), but I'm trying to get at
		 * the cost of the small uploads here.
		 */
		size = random() % 64 + 1;
		if (i + size > width * height)
			size = width * height - i;

		for (j = 0; j < size; j++)
			data[j] = seed++;

		/* Upload the junk. */
		drm_intel_bo_subdata(src_bo, i * 4, size * 4, data);

		i += size;
	}

	/* Render the junk to the dst. */
	BLIT_COPY_BATCH_START(0);
	OUT_BATCH((3 << 24) | /* 32 bits */
		  (0xcc << 16) | /* copy ROP */
		  (width * 4) /* dst pitch */);
	OUT_BATCH(0); /* dst x1,y1 */
	OUT_BATCH((height << 16) | width); /* dst x2,y2 */
	OUT_RELOC(dst_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(0); /* src x1,y1 */
	OUT_BATCH(width * 4); /* src pitch */
	OUT_RELOC(src_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
	ADVANCE_BATCH();

	intel_batchbuffer_flush(batch);

	drm_intel_bo_unreference(src_bo);
}

int main(int argc, char **argv)
{
	int fd;
	int object_size = OBJECT_WIDTH * OBJECT_HEIGHT * 4;
	double start_time, end_time;
	drm_intel_bo *dst_bo;
	drm_intel_bufmgr *bufmgr;
	struct intel_batchbuffer *batch;
	int i;

	fd = drm_open_driver(DRIVER_INTEL);

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));

	dst_bo = drm_intel_bo_alloc(bufmgr, "dst", object_size, 4096);

	/* Prep loop to get us warmed up. */
	for (i = 0; i < 20; i++) {
		do_render(bufmgr, batch, dst_bo, OBJECT_WIDTH, OBJECT_HEIGHT);
	}
	drm_intel_bo_wait_rendering(dst_bo);

	/* Do the actual timing. */
	start_time = get_time_in_secs();
	for (i = 0; i < 1000; i++) {
		do_render(bufmgr, batch, dst_bo, OBJECT_WIDTH, OBJECT_HEIGHT);
	}
	drm_intel_bo_wait_rendering(dst_bo);
	end_time = get_time_in_secs();

	printf("%d iterations in %.03f secs: %.01f MB/sec\n", i,
	       end_time - start_time,
	       (double)i * OBJECT_WIDTH * OBJECT_HEIGHT * 4 / 1024.0 / 1024.0 /
	       (end_time - start_time));

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
