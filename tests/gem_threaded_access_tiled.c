/*
 * Copyright (c) 2012 Intel Corporation
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
 *    Mika Kuoppala <mika.kuoppala@intel.com>
 */

#include "igt.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include "intel_bufmgr.h"

IGT_TEST_DESCRIPTION("Check parallel access to tiled memory.");

/* Testcase: check parallel access to tiled memory
 *
 * Parallel access to tiled memory caused sigbus
 */

#define NUM_THREADS 2
#define WIDTH 4096
#define HEIGHT 4096

struct thread_ctx {
	drm_intel_bo *bo;
};

static drm_intel_bufmgr *bufmgr;
static struct thread_ctx tctx[NUM_THREADS];

static void *copy_fn(void *p)
{
	unsigned char *buf;
	struct thread_ctx *c = p;

	buf = malloc(WIDTH * HEIGHT);
	if (buf == NULL)
		return (void *)1;

	memcpy(buf, c->bo->virtual, WIDTH * HEIGHT);

	free(buf);
	return (void *)0;
}

static int copy_tile_threaded(drm_intel_bo *bo)
{
	int i;
	int r;
	pthread_t thr[NUM_THREADS];
	void *status;

	for (i = 0; i < NUM_THREADS; i++) {
		tctx[i].bo = bo;
		r = pthread_create(&thr[i], NULL, copy_fn, (void *)&tctx[i]);
		igt_assert_eq(r, 0);
	}

	for (i = 0;  i < NUM_THREADS; i++) {
		pthread_join(thr[i], &status);
		igt_assert(status == 0);
	}

	return 0;
}

igt_simple_main
{
	int fd;
	drm_intel_bo *bo;
	uint32_t tiling_mode = I915_TILING_Y;
	unsigned long pitch = 0;
	int r;

	igt_skip_on_simulation();

	fd = drm_open_driver(DRIVER_INTEL);
	igt_assert(fd >= 0);

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	igt_assert(bufmgr);

	bo = drm_intel_bo_alloc_tiled(bufmgr, "mmap bo", WIDTH, HEIGHT, 1,
				      &tiling_mode, &pitch, 0);
	igt_assert(bo);

	r = drm_intel_gem_bo_map_gtt(bo);
	igt_assert(!r);

	r = copy_tile_threaded(bo);
	igt_assert(!r);

	r = drm_intel_gem_bo_unmap_gtt(bo);
	igt_assert(!r);

	drm_intel_bo_unreference(bo);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);
}
