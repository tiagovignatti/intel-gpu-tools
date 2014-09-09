/*
 * Copyright © 2014 Intel Corporation
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
 */

#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
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
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"
#include "intel_chipset.h"
#include "igt_aux.h"

#define WIDTH 512
#define STRIDE (WIDTH*4)
#define HEIGHT 512
#define SIZE (HEIGHT*STRIDE)

static drm_intel_bo *create_bo(drm_intel_bufmgr *bufmgr,
			       uint32_t pixel)
{
	drm_intel_bo *bo;
	uint32_t *v;

	bo = drm_intel_bo_alloc(bufmgr, "surface", SIZE, 4096);
	igt_assert(bo);

	do_or_die(drm_intel_bo_map(bo, 1));
	v = bo->virtual;
	for (int i = 0; i < SIZE/4; i++)
		v[i] = pixel;
	drm_intel_bo_unmap(bo);

	return bo;
}

static void scratch_buf_init(struct igt_buf *buf,
			     drm_intel_bufmgr *bufmgr,
			     uint32_t pixel)
{
	buf->bo = create_bo(bufmgr, pixel);
	buf->stride = STRIDE;
	buf->tiling = I915_TILING_NONE;
	buf->size = SIZE;
}

static void scratch_buf_fini(struct igt_buf *buf)
{
	dri_bo_unreference(buf->bo);
	memset(buf, 0, sizeof(*buf));
}

static void fork_rcs_copy(int target, dri_bo **dst, int count)
{
	igt_render_copyfunc_t render_copy;
	int devid;

	for (int child = 0; child < count; child++) {
		int fd = drm_open_any();
		drm_intel_bufmgr *bufmgr;

		devid = intel_get_drm_devid(fd);

		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		igt_assert(bufmgr);

		dst[child] = create_bo(bufmgr, ~0);

		render_copy = igt_get_render_copyfunc(devid);
		igt_require_f(render_copy,
			      "no render-copy function\n");
	}

	igt_fork(child, count) {
		struct intel_batchbuffer *batch;
		struct igt_buf buf;

		batch = intel_batchbuffer_alloc(dst[child]->bufmgr,
						devid);
		igt_assert(batch);

		buf.bo = dst[child];
		buf.stride = STRIDE;
		buf.tiling = I915_TILING_NONE;
		buf.size = SIZE;

		for (int i = 0; i <= target; i++) {
			struct igt_buf src;

			scratch_buf_init(&src, dst[child]->bufmgr,
					 i | child << 16);

			render_copy(batch, NULL,
				    &src, 0, 0,
				    WIDTH, HEIGHT,
				    &buf, 0, 0);

			scratch_buf_fini(&src);
		}
	}
}

static void fork_bcs_copy(int target, dri_bo **dst, int count)
{
	int devid;

	for (int child = 0; child < count; child++) {
		drm_intel_bufmgr *bufmgr;
		int fd = drm_open_any();

		devid = intel_get_drm_devid(fd);

		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		igt_assert(bufmgr);

		dst[child] = create_bo(bufmgr, ~0);
	}

	igt_fork(child, count) {
		struct intel_batchbuffer *batch;

		batch = intel_batchbuffer_alloc(dst[child]->bufmgr,
						devid);
		igt_assert(batch);

		for (int i = 0; i <= target; i++) {
			dri_bo *src[2];

			src[0] = create_bo(dst[child]->bufmgr,
					   ~0);
			src[1] = create_bo(dst[child]->bufmgr,
					   i | child << 16);

			intel_copy_bo(batch, src[0], src[1], SIZE);
			intel_copy_bo(batch, dst[child], src[0], SIZE);

			dri_bo_unreference(src[1]);
			dri_bo_unreference(src[0]);
		}
	}
}

static void surfaces_check(dri_bo **bo, int count, uint32_t expected)
{
	for (int child = 0; child < count; child++) {
		uint32_t *ptr;

		do_or_die(drm_intel_bo_map(bo[child], 0));
		ptr = bo[child]->virtual;
		for (int j = 0; j < SIZE/4; j++)
			igt_assert_eq(ptr[j], expected | child << 16);
		drm_intel_bo_unmap(bo[child]);
	}
}

int main(int argc, char **argv)
{
	igt_subtest_init(argc, argv);

	igt_subtest("bcs-vs-rcs") {
#define N_CHILD 8
		dri_bo *bcs[1], *rcs[N_CHILD];

		fork_bcs_copy(0x4000, bcs, 1);
		fork_rcs_copy(0x4000 / N_CHILD, rcs, N_CHILD);

		igt_waitchildren();

		surfaces_check(bcs, 1, 0x4000);
		surfaces_check(rcs, N_CHILD, 0x4000 / N_CHILD);
	}

	igt_exit();

	return 0;
}
