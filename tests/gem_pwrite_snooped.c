/*
 * Copyright © 2015 Intel Corporation
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
 */

#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "drm.h"

IGT_TEST_DESCRIPTION(
   "pwrite to a snooped bo then make it uncached and check that the GPU sees the data.");

static int fd;
static uint32_t devid;
static drm_intel_bufmgr *bufmgr;

static void blit(drm_intel_bo *dst, drm_intel_bo *src,
		 unsigned int width, unsigned int height,
		 unsigned int dst_pitch, unsigned int src_pitch)
{
	struct intel_batchbuffer *batch;

	batch = intel_batchbuffer_alloc(bufmgr, devid);
	igt_assert(batch);

	BLIT_COPY_BATCH_START(0);
	OUT_BATCH((3 << 24) | /* 32 bits */
		  (0xcc << 16) | /* copy ROP */
		  dst_pitch);
	OUT_BATCH(0 << 16 | 0);
	OUT_BATCH(height << 16 | width);
	OUT_RELOC_FENCED(dst, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(0 << 16 | 0);
	OUT_BATCH(src_pitch);
	OUT_RELOC_FENCED(src, I915_GEM_DOMAIN_RENDER, 0, 0);
	ADVANCE_BATCH();

	if (batch->gen >= 6) {
		BEGIN_BATCH(3, 0);
		OUT_BATCH(XY_SETUP_CLIP_BLT_CMD);
		OUT_BATCH(0);
		OUT_BATCH(0);
		ADVANCE_BATCH();
	}

	intel_batchbuffer_flush(batch);
	intel_batchbuffer_free(batch);
}

static void *memchr_inv(const void *s, int c, size_t n)
{
	const unsigned char *us = s;
	unsigned char uc = c;

	while (n--) {
		if (*us != uc)
			return (void *) us;
		us++;
	}

	return NULL;
}

static void test(int w, int h)
{
	int object_size = w * h * 4;
	drm_intel_bo *src, *dst;
	void *buf;

	src = drm_intel_bo_alloc(bufmgr, "src", object_size, 4096);
	igt_assert(src);
	dst = drm_intel_bo_alloc(bufmgr, "dst", object_size, 4096);
	igt_assert(dst);

	buf = malloc(object_size);
	igt_assert(buf);
	memset(buf, 0xff, object_size);

	gem_set_domain(fd, src->handle, I915_GEM_DOMAIN_GTT,
		       I915_GEM_DOMAIN_GTT);

	gem_set_caching(fd, src->handle, I915_CACHING_CACHED);

	gem_write(fd, src->handle, 0, buf, object_size);

	gem_set_caching(fd, src->handle, I915_CACHING_NONE);

	blit(dst, src, w, h, w * 4, h * 4);

	memset(buf, 0x00, object_size);
	gem_read(fd, dst->handle, 0, buf, object_size);

	igt_assert(memchr_inv(buf, 0xff, object_size) == NULL);
}

igt_simple_main
{
	igt_skip_on_simulation();

	fd = drm_open_any();
	devid = intel_get_drm_devid(fd);
	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);

	test(256, 256);

	drm_intel_bufmgr_destroy(bufmgr);
	close(fd);
}
