/*
 * Copyright © 2013 Intel Corporation
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
 *    Damien Lespiau <damien.lespiau@intel.com>
 */

/*
 * The goal of this test is to ensure that we respect inter ring dependencies
 *
 * For each pair of rings R1, R2 where we have copy support (i.e. blt,
 * rendercpy and mediafill) do:
 *  - Throw a busy load onto R1. gem_concurrent_blt just uses lots of buffers
 *    for this effect.
 *  - Fill three buffers A, B, C with unique data.
 *  - Copy A to B on ring R1
 *
 * Then come the three different variants.
 *  - Copy B to C on ring R2, check that C now contains what A originally
 *    contained. This is the write->read hazard. gem_concurrent_blt calls this
 *    early read.
 *  - Copy C to A on ring R2, check that B now contains what A originally
 *    contained. This is the read->write hazard, gem_concurrent_blt calls it
 *    overwrite_source.
 *  - Copy C to B on ring R2 and check that B contains what C originally
 *    contained. This is the write/write hazard. gem_concurrent_blt doesn't
 *    have that since for the cpu case it's too boring.
 *
 */

#include <stdlib.h>
#include <stdbool.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_batchbuffer.h"
#include "intel_chipset.h"

#define WIDTH	512
#define HEIGHT	512

typedef struct {
	int drm_fd;
	uint32_t devid;
	drm_intel_bufmgr *bufmgr;
	struct intel_batchbuffer *batch;

	/* number of buffers to keep the ring busy for a while */
	unsigned int n_buffers_load;

	uint32_t linear[WIDTH * HEIGHT];

	struct {
		igt_render_copyfunc_t copy;
		struct igt_buf *srcs;
		struct igt_buf *dsts;
	} render;

	struct {
		drm_intel_bo **srcs;
		drm_intel_bo **dsts;
	} blitter;

} data_t;

enum ring {
	RENDER,
	BLITTER,
};

enum test {
	TEST_WRITE_READ,
	TEST_READ_WRITE,
	TEST_WRITE_WRITE,
};

static const char *ring_name(enum ring ring)
{
	const char *names[] = {
		"render",
		"blitter",
	};

	return names[ring];
}

static drm_intel_bo *bo_create(data_t *data, int width, int height, int val)
{
	drm_intel_bo *bo;
	int i;

	bo = drm_intel_bo_alloc(data->bufmgr, "", 4 * width * height, 4096);
	igt_assert(bo);

	for (i = 0; i < width * height; i++)
		data->linear[i] = val;
	gem_write(data->drm_fd, bo->handle, 0, data->linear,
		  sizeof(data->linear));

	return bo;
}

static void bo_check(data_t *data, drm_intel_bo *bo, uint32_t val)
{
	int i;

	gem_read(data->drm_fd, bo->handle, 0,
		 data->linear, sizeof(data->linear));
	for (i = 0; i < WIDTH * HEIGHT; i++)
		igt_assert_cmpint(data->linear[i], ==, val);
}

static void scratch_buf_init_from_bo(struct igt_buf *buf, drm_intel_bo *bo)
{
	buf->bo = bo;
	buf->stride = 4 * WIDTH;
	buf->tiling = I915_TILING_NONE;
	buf->size = 4 * WIDTH * HEIGHT;
}

static void scratch_buf_init(data_t *data, struct igt_buf *buf,
			     int width, int height, uint32_t color)
{
	drm_intel_bo *bo;

	bo = bo_create(data, width, height, color);
	scratch_buf_init_from_bo(buf, bo);
}

/*
 * Provide a few ring specific vfuncs for run_test().
 *
 * busy()	Queue a n_buffers_load workloads onto the ring to keep it busy
 * busy_fini()	Clean up after busy
 * copy()	Copy one BO to another
 */

/*
 * Render ring
 */

static void render_busy(data_t *data)
{
	size_t array_size;
	int i;

	array_size = data->n_buffers_load * sizeof(struct igt_buf);
	data->render.srcs = malloc(array_size);
	data->render.dsts = malloc(array_size);

	for (i = 0; i < data->n_buffers_load; i++) {
		scratch_buf_init(data, &data->render.srcs[i], WIDTH, HEIGHT,
				 0xdeadbeef);
		scratch_buf_init(data, &data->render.dsts[i], WIDTH, HEIGHT,
				 0xdeadbeef);
	}

	for (i = 0; i < data->n_buffers_load; i++) {
		data->render.copy(data->batch,
				  NULL,			/* context */
				  &data->render.srcs[i],
				  0, 0,			/* src_x, src_y */
				  WIDTH, HEIGHT,
				  &data->render.dsts[i],
				  0, 0			/* dst_x, dst_y */);
	}
}

static void render_busy_fini(data_t *data)
{
	int i;

	for (i = 0; i < data->n_buffers_load; i++) {
		drm_intel_bo_unreference(data->render.srcs[i].bo);
		drm_intel_bo_unreference(data->render.dsts[i].bo);
	}

	free(data->render.srcs);
	free(data->render.dsts);
	data->render.srcs = NULL;
	data->render.dsts = NULL;
}

static void render_copy(data_t *data, drm_intel_bo *src, drm_intel_bo *dst)
{
	struct igt_buf src_buf, dst_buf;

	scratch_buf_init_from_bo(&src_buf, src);
	scratch_buf_init_from_bo(&dst_buf, dst);

	data->render.copy(data->batch,
			  NULL,			/* context */
			  &src_buf,
			  0, 0,			/* src_x, src_y */
			  WIDTH, HEIGHT,
			  &dst_buf,
			  0, 0			/* dst_x, dst_y */);
}

/*
 * Blitter ring
 */

static void blitter_busy(data_t *data)
{
	size_t array_size;
	int i;

	array_size = data->n_buffers_load * sizeof(drm_intel_bo *);
	data->blitter.srcs = malloc(array_size);
	data->blitter.dsts = malloc(array_size);

	for (i = 0; i < data->n_buffers_load; i++) {
		data->blitter.srcs[i] = bo_create(data,
						  WIDTH, HEIGHT,
						  0xdeadbeef);
		data->blitter.dsts[i] = bo_create(data,
						  WIDTH, HEIGHT,
						  0xdeadbeef);
	}

	for (i = 0; i < data->n_buffers_load; i++) {
		intel_copy_bo(data->batch,
			      data->blitter.srcs[i],
			      data->blitter.dsts[i],
			      WIDTH*HEIGHT*4);
	}
}

static void blitter_busy_fini(data_t *data)
{
	int i;

	for (i = 0; i < data->n_buffers_load; i++) {
		drm_intel_bo_unreference(data->blitter.srcs[i]);
		drm_intel_bo_unreference(data->blitter.dsts[i]);
	}

	free(data->blitter.srcs);
	free(data->blitter.dsts);
	data->blitter.srcs = NULL;
	data->blitter.dsts = NULL;
}

static void blitter_copy(data_t *data, drm_intel_bo *src, drm_intel_bo *dst)
{
	intel_copy_bo(data->batch, dst, src, WIDTH*HEIGHT*4);
}

struct ring_ops {
	void (*busy)(data_t *data);
	void (*busy_fini)(data_t *data);
	void (*copy)(data_t *data, drm_intel_bo *src, drm_intel_bo *dst);
} ops [] = {
	{
		.busy      = render_busy,
		.busy_fini = render_busy_fini,
		.copy      = render_copy,
	},
	{
		.busy      = blitter_busy,
		.busy_fini = blitter_busy_fini,
		.copy      = blitter_copy,
	},
};

static void run_test(data_t *data, enum ring r1, enum ring r2, enum test test)
{
	struct ring_ops *r1_ops = &ops[r1];
	struct ring_ops *r2_ops = &ops[r2];
	drm_intel_bo *a, *b, *c;

	a = bo_create(data, WIDTH, HEIGHT, 0xa);
	b = bo_create(data, WIDTH, HEIGHT, 0xb);
	c = bo_create(data, WIDTH, HEIGHT, 0xc);

	r1_ops->busy(data);
	r1_ops->copy(data, a, b);

	switch (test) {
	case TEST_WRITE_READ:
		r2_ops->copy(data, b, c);
		bo_check(data, c, 0xa);
		break;
	case TEST_READ_WRITE:
		r2_ops->copy(data, c, a);
		bo_check(data, b, 0xa);
		break;
	case TEST_WRITE_WRITE:
		r2_ops->copy(data, c, b);
		bo_check(data, b, 0xc);
		break;
	default:
		abort();
	}

	r1_ops->busy_fini(data);
}

igt_main
{
	data_t data = {0, };
	int i;
	struct combination {
		int r1, r2;
	} ring_combinations [] = {
		{ RENDER, BLITTER },
		{ BLITTER, RENDER },
	};

	igt_fixture {
		data.drm_fd = drm_open_any_render();
		data.devid = intel_get_drm_devid(data.drm_fd);

		data.n_buffers_load = 1000;

		data.bufmgr = drm_intel_bufmgr_gem_init(data.drm_fd, 4096);
		igt_assert(data.bufmgr);
		drm_intel_bufmgr_gem_enable_reuse(data.bufmgr);

		data.render.copy = igt_get_render_copyfunc(data.devid);
		igt_require_f(data.render.copy,
			      "no render-copy function\n");

		data.batch = intel_batchbuffer_alloc(data.bufmgr, data.devid);
		igt_assert(data.batch);
	}

	for (i = 0; i < ARRAY_SIZE(ring_combinations); i++) {
		struct combination *c = &ring_combinations[i];

		igt_subtest_f("sync-%s-%s-write-read",
			      ring_name(c->r1), ring_name(c->r2))
			run_test(&data, c->r1, c->r2, TEST_WRITE_READ);

		igt_subtest_f("sync-%s-%s-read-write",
			      ring_name(c->r1), ring_name(c->r2))
			run_test(&data, c->r1, c->r2, TEST_READ_WRITE);
		igt_subtest_f("sync-%s-%s-write-write",
			      ring_name(c->r1), ring_name(c->r2))
			run_test(&data, c->r1, c->r2, TEST_WRITE_WRITE);
	}

	igt_fixture {
		intel_batchbuffer_free(data.batch);
		drm_intel_bufmgr_destroy(data.bufmgr);
		close(data.drm_fd);
	}
}
