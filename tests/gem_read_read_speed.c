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
 */

/** @file gem_read_read_speed.c
 *
 * This is a test of performance with multiple readers from the same source.
 */

#include "igt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <drm.h>

#include "intel_bufmgr.h"

IGT_TEST_DESCRIPTION("Test speed of concurrent reads between engines.");

igt_render_copyfunc_t rendercopy;
struct intel_batchbuffer *batch;
int width, height;

static int gem_param(int fd, int name)
{
	drm_i915_getparam_t gp;
	int v = -1; /* No param uses the sign bit, reserve it for errors */

	memset(&gp, 0, sizeof(gp));
	gp.param = name;
	gp.value = &v;
	if (drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
		return -1;

	return v;
}

static int semaphores_enabled(int fd)
{
	FILE *file;
	int detected = -1;
	int ret;

	ret = gem_param(fd, 20);
	if (ret != -1)
		return ret > 0;

	file = fopen("/sys/module/i915/parameters/semaphores", "r");
	if (file) {
		int value;
		if (fscanf(file, "%d", &value) == 1)
			detected = value;
		fclose(file);
	}

	return detected;
}

static drm_intel_bo *rcs_copy_bo(drm_intel_bo *dst, drm_intel_bo *src)
{
	struct igt_buf d = {
		.bo = dst,
		.size = width * height * 4,
		.num_tiles = width * height * 4,
		.stride = width * 4,
	}, s = {
		.bo = src,
		.size = width * height * 4,
		.num_tiles = width * height * 4,
		.stride = width * 4,
	};
	uint32_t swizzle;
	drm_intel_bo *bo = batch->bo;
	drm_intel_bo_reference(bo);

	drm_intel_bo_get_tiling(dst, &d.tiling, &swizzle);
	drm_intel_bo_get_tiling(src, &s.tiling, &swizzle);

	rendercopy(batch, NULL,
		   &s, 0, 0,
		   width, height,
		   &d, 0, 0);

	return bo;
}

static drm_intel_bo *bcs_copy_bo(drm_intel_bo *dst, drm_intel_bo *src)
{
	drm_intel_bo *bo = batch->bo;
	drm_intel_bo_reference(bo);

	intel_blt_copy(batch,
		       src, 0, 0, 4*width,
		       dst, 0, 0, 4*width,
		       width, height, 32);

	return bo;
}

static void
set_bo(drm_intel_bo *bo, uint32_t val)
{
	int size = width * height;
	uint32_t *vaddr;

	do_or_die(drm_intel_bo_map(bo, 1));
	vaddr = bo->virtual;
	while (size--)
		*vaddr++ = val;
	drm_intel_bo_unmap(bo);
}

static double elapsed(const struct timespec *start,
		      const struct timespec *end,
		      int loop)
{
	return (1e6*(end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec)/1000)/loop;
}

static drm_intel_bo *create_bo(drm_intel_bufmgr *bufmgr,
			       const char *name)
{
	uint32_t tiling_mode = I915_TILING_X;
	unsigned long pitch;
	return drm_intel_bo_alloc_tiled(bufmgr, name,
					width, height, 4,
					&tiling_mode, &pitch, 0);
}

static void run(drm_intel_bufmgr *bufmgr, int _width, int _height,
		bool write_bcs, bool write_rcs)
{
	drm_intel_bo *src = NULL, *bcs = NULL, *rcs = NULL;
	drm_intel_bo *bcs_batch, *rcs_batch;
	struct timespec start, end;
	int loops = 1000;

	width = _width;
	height = _height;

	src = create_bo(bufmgr, "src");
	bcs = create_bo(bufmgr, "bcs");
	rcs = create_bo(bufmgr, "rcs");

	set_bo(src, 0xdeadbeef);

	if (write_bcs) {
		bcs_batch = bcs_copy_bo(src, bcs);
	} else {
		bcs_batch = bcs_copy_bo(bcs, src);
	}
	if (write_rcs) {
		rcs_batch = rcs_copy_bo(src, rcs);
	} else {
		rcs_batch = rcs_copy_bo(rcs, src);
	}

	drm_intel_bo_unreference(rcs);
	drm_intel_bo_unreference(bcs);

	drm_intel_gem_bo_start_gtt_access(src, true);
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (int i = 0; i < loops; i++) {
		drm_intel_gem_bo_context_exec(rcs_batch, NULL, 4096, I915_EXEC_RENDER);
		drm_intel_gem_bo_context_exec(bcs_batch, NULL, 4096, I915_EXEC_BLT);
	}
	drm_intel_gem_bo_start_gtt_access(src, true);
	clock_gettime(CLOCK_MONOTONIC, &end);

	igt_info("Time to %s-%s %dx%d [%dk]:		%7.3fµs\n",
		 write_bcs ? "write" : "read",
		 write_rcs ? "write" : "read",
		 width, height, 4*width*height/1024,
		 elapsed(&start, &end, loops));

	drm_intel_bo_unreference(rcs_batch);
	drm_intel_bo_unreference(bcs_batch);

	drm_intel_bo_unreference(src);
}

igt_main
{
	const int sizes[] = {1, 128, 256, 512, 1024, 2048, 4096, 8192, 0};
	drm_intel_bufmgr *bufmgr = NULL;
	int fd, i;

	igt_skip_on_simulation();

	igt_fixture {
		int devid;

		fd = drm_open_driver(DRIVER_INTEL);

		devid = intel_get_drm_devid(fd);
		igt_require(intel_gen(devid) >= 6);

		rendercopy = igt_get_render_copyfunc(devid);
		igt_require(rendercopy);

		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		igt_assert(bufmgr);

		batch =  intel_batchbuffer_alloc(bufmgr, devid);

		igt_info("Semaphores: %d\n", semaphores_enabled(fd));
	}

	for (i = 0; sizes[i] != 0; i++) {
		igt_subtest_f("read-read-%dx%d", sizes[i], sizes[i])
			run(bufmgr, sizes[i], sizes[i], false, false);
		igt_subtest_f("read-write-%dx%d", sizes[i], sizes[i])
			run(bufmgr, sizes[i], sizes[i], false, true);
		igt_subtest_f("write-read-%dx%d", sizes[i], sizes[i])
			run(bufmgr, sizes[i], sizes[i], true, false);
		igt_subtest_f("write-write-%dx%d", sizes[i], sizes[i])
			run(bufmgr, sizes[i], sizes[i], true, true);
	}
}
