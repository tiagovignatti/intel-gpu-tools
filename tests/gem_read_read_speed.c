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

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "igt_aux.h"
#include "igt_core.h"
#include "igt_gt.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"
#include "intel_chipset.h"

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

static void rcs_copy_bo(drm_intel_bo *dst, drm_intel_bo *src)
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

	drm_intel_bo_get_tiling(dst, &d.tiling, &swizzle);
	drm_intel_bo_get_tiling(src, &s.tiling, &swizzle);

	rendercopy(batch, NULL,
		   &s, 0, 0,
		   width, height,
		   &d, 0, 0);
}

static void bcs_copy_bo(drm_intel_bo *dst, drm_intel_bo *src)
{
	intel_blt_copy(batch,
		       src, 0, 0, 4*width,
		       dst, 0, 0, 4*width,
		       width, height, 32);
}

static void
set_bo(drm_intel_bo *bo, uint32_t val)
{
	int size = width * height;
	uint32_t *vaddr;

	do_or_die(drm_intel_gem_bo_map_gtt(bo));
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

static void run(drm_intel_bufmgr *bufmgr, int _width, int _height)
{
	drm_intel_bo *src = NULL, *bcs = NULL, *rcs = NULL;
	struct timespec start, end;
	int loops = 1000;

	width = _width;
	height = _height;

	src = drm_intel_bo_alloc(bufmgr, "src", 4*width*height, 0);
	bcs = drm_intel_bo_alloc(bufmgr, "bcs", 4*width*height, 0);
	rcs = drm_intel_bo_alloc(bufmgr, "rcs", 4*width*height, 0);

	set_bo(src, 0xdeadbeef);

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (int i = 0; i < loops; i++) {
		rcs_copy_bo(rcs, src);
		bcs_copy_bo(bcs, src);
	}
	drm_intel_gem_bo_start_gtt_access(src, true);
	clock_gettime(CLOCK_MONOTONIC, &end);

	igt_info("Time to read-read %dx%d [%dk]:		%7.3fµs\n",
		 width, height, 4*width*height/1024,
		 elapsed(&start, &end, loops));

	drm_intel_bo_unreference(rcs);
	drm_intel_bo_unreference(bcs);
	drm_intel_bo_unreference(src);
}

igt_main
{
	drm_intel_bufmgr *bufmgr;
	int fd;

	igt_skip_on_simulation();

	igt_fixture {
		int devid;

		fd = drm_open_any();
		igt_info("Semaphores: %d\n", semaphores_enabled(fd));

		devid = intel_get_drm_devid(fd);

		rendercopy = igt_get_render_copyfunc(devid);
		igt_require(rendercopy);

		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		igt_assert(bufmgr);

		batch =  intel_batchbuffer_alloc(bufmgr, devid);
	}

	igt_subtest("read-read-1x1")
		run(bufmgr, 1, 1);

	igt_subtest("read-read-512x512")
		run(bufmgr, 512, 512);

	igt_subtest("read-read-4096x4096")
		run(bufmgr, 4096, 4096);
}
