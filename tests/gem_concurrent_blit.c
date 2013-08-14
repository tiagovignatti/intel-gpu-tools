/*
 * Copyright © 2009,2012,2013 Intel Corporation
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
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

/** @file gem_concurrent_blit.c
 *
 * This is a test of pread/pwrite behavior when writing to active
 * buffers.
 *
 * Based on gem_gtt_concurrent_blt.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_gpu_tools.h"

static void
prw_set_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
	int size = width * height;
	uint32_t *vaddr, *tmp;

	vaddr = tmp = malloc(size*4);
	while (size--)
		*vaddr++ = val;
	drm_intel_bo_subdata(bo, 0, width*height*4, tmp);
	free(tmp);
}

static void
prw_cmp_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
	int size = width * height;
	uint32_t *vaddr, *tmp;

	vaddr = tmp = malloc(size*4);
	drm_intel_bo_get_subdata(bo, 0, size*4, tmp);
	while (size--)
		igt_assert(*vaddr++ == val);
	free(tmp);
}

static drm_intel_bo *
unmapped_create_bo(drm_intel_bufmgr *bufmgr, uint32_t val, int width, int height)
{
	drm_intel_bo *bo;

	bo = drm_intel_bo_alloc(bufmgr, "bo", 4*width*height, 0);
	igt_assert(bo);

	return bo;
}

static void
gtt_set_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
	int size = width * height;
	uint32_t *vaddr;

	drm_intel_gem_bo_start_gtt_access(bo, true);
	vaddr = bo->virtual;
	while (size--)
		*vaddr++ = val;
}

static void
gtt_cmp_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
	int size = width * height;
	uint32_t *vaddr;

	drm_intel_gem_bo_start_gtt_access(bo, false);
	vaddr = bo->virtual;
	while (size--)
		igt_assert(*vaddr++ == val);
}

static drm_intel_bo *
gtt_create_bo(drm_intel_bufmgr *bufmgr, uint32_t val, int width, int height)
{
	drm_intel_bo *bo;

	bo = drm_intel_bo_alloc(bufmgr, "bo", 4*width*height, 0);
	igt_assert(bo);

	/* gtt map doesn't have a write parameter, so just keep the mapping
	 * around (to avoid the set_domain with the gtt write domain set) and
	 * manually tell the kernel when we start access the gtt. */
	do_or_die(drm_intel_gem_bo_map_gtt(bo));

	return bo;
}

static void
cpu_set_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
	int size = width * height;
	uint32_t *vaddr;

	do_or_die(drm_intel_bo_map(bo, true));
	vaddr = bo->virtual;
	while (size--)
		*vaddr++ = val;
	drm_intel_bo_unmap(bo);
}

static void
cpu_cmp_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
	int size = width * height;
	uint32_t *vaddr;

	do_or_die(drm_intel_bo_map(bo, false));
	vaddr = bo->virtual;
	while (size--)
		igt_assert(*vaddr++ == val);
	drm_intel_bo_unmap(bo);
}

struct access_mode {
	void (*set_bo)(drm_intel_bo *bo, uint32_t val, int w, int h);
	void (*cmp_bo)(drm_intel_bo *bo, uint32_t val, int w, int h);
	drm_intel_bo *(*create_bo)(drm_intel_bufmgr *bufmgr,
				   uint32_t val, int width, int height);
	const char *name;
};

struct access_mode access_modes[] = {
	{ .set_bo = prw_set_bo, .cmp_bo = prw_cmp_bo,
		.create_bo = unmapped_create_bo, .name = "prw" },
	{ .set_bo = cpu_set_bo, .cmp_bo = cpu_cmp_bo,
		.create_bo = unmapped_create_bo, .name = "cpu" },
	{ .set_bo = gtt_set_bo, .cmp_bo = gtt_cmp_bo,
		.create_bo = gtt_create_bo, .name = "gtt" },
};

int num_buffers = 128, fd;
drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;

static void
run_modes(struct access_mode *mode)
{
	int width = 512, height = 512;
	int loop, i, nc;
	pid_t children[16];

	drm_intel_bo *src[128], *dst[128], *dummy = NULL;

	if (!igt_only_list_subtests()) {
		for (i = 0; i < num_buffers; i++) {
			src[i] = mode->create_bo(bufmgr, i, width, height);
			dst[i] = mode->create_bo(bufmgr, ~i, width, height);
		}
		dummy = mode->create_bo(bufmgr, 0, width, height);
	}

	/* try to overwrite the source values */
	igt_subtest_f("%s-overwrite-source", mode->name) {
		for (i = 0; i < num_buffers; i++) {
			mode->set_bo(src[i], i, width, height);
			mode->set_bo(dst[i], i, width, height);
		}
		for (i = 0; i < num_buffers; i++)
			intel_copy_bo(batch, dst[i], src[i], width, height);
		for (i = num_buffers; i--; )
			mode->set_bo(src[i], 0xdeadbeef, width, height);
		for (i = 0; i < num_buffers; i++)
			mode->cmp_bo(dst[i], i, width, height);
	}

	/* try to read the results before the copy completes */
	igt_subtest_f("%s-early-read", mode->name) {
		for (i = num_buffers; i--; )
			mode->set_bo(src[i], 0xdeadbeef, width, height);
		for (i = 0; i < num_buffers; i++)
			intel_copy_bo(batch, dst[i], src[i], width, height);
		for (i = num_buffers; i--; )
			mode->cmp_bo(dst[i], 0xdeadbeef, width, height);
	}

	/* and finally try to trick the kernel into loosing the pending write */
	igt_subtest_f("%s-gpu-read-after-write", mode->name) {
		for (i = num_buffers; i--; )
			mode->set_bo(src[i], 0xabcdabcd, width, height);
		for (i = 0; i < num_buffers; i++)
			intel_copy_bo(batch, dst[i], src[i], width, height);
		for (i = num_buffers; i--; )
			intel_copy_bo(batch, dummy, dst[i], width, height);
		for (i = num_buffers; i--; )
			mode->cmp_bo(dst[i], 0xabcdabcd, width, height);
	}

	igt_fork_signal_helper();

	/* try to read the results before the copy completes */
	igt_subtest_f("%s-overwrite-source-interruptible", mode->name) {
		for (loop = 0; loop < 10; loop++) {
			gem_quiescent_gpu(fd);
			for (i = 0; i < num_buffers; i++) {
				mode->set_bo(src[i], i, width, height);
				mode->set_bo(dst[i], i, width, height);
			}
			for (i = 0; i < num_buffers; i++)
				intel_copy_bo(batch, dst[i], src[i], width, height);
			for (i = num_buffers; i--; )
				mode->set_bo(src[i], 0xdeadbeef, width, height);
			for (i = 0; i < num_buffers; i++)
				mode->cmp_bo(dst[i], i, width, height);
		}
	}

	/* try to read the results before the copy completes */
	igt_subtest_f("%s-early-read-interruptible", mode->name) {
		for (loop = 0; loop < 10; loop++) {
			gem_quiescent_gpu(fd);
			for (i = num_buffers; i--; )
				mode->set_bo(src[i], 0xdeadbeef, width, height);
			for (i = 0; i < num_buffers; i++)
				intel_copy_bo(batch, dst[i], src[i], width, height);
			for (i = num_buffers; i--; )
				mode->cmp_bo(dst[i], 0xdeadbeef, width, height);
		}
	}

	/* and finally try to trick the kernel into loosing the pending write */
	igt_subtest_f("%s-gpu-read-after-write-interruptible", mode->name) {
		for (loop = 0; loop < 10; loop++) {
			gem_quiescent_gpu(fd);
			for (i = num_buffers; i--; )
				mode->set_bo(src[i], 0xabcdabcd, width, height);
			for (i = 0; i < num_buffers; i++)
				intel_copy_bo(batch, dst[i], src[i], width, height);
			for (i = num_buffers; i--; )
				intel_copy_bo(batch, dummy, dst[i], width, height);
			for (i = num_buffers; i--; )
				mode->cmp_bo(dst[i], 0xabcdabcd, width, height);
		}
	}

	/* try to read the results before the copy completes */
	igt_subtest_f("%s-overwrite-source-forked", mode->name) {
		for (nc = 0; nc < ARRAY_SIZE(children); nc++) {
			switch ((children[nc] = fork())) {
			case -1: igt_assert(0);
			default: break;
			case 0:
				 /* recreate process local variables */
				 bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
				 drm_intel_bufmgr_gem_enable_reuse(bufmgr);
				 batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
				 for (i = 0; i < num_buffers; i++) {
					 src[i] = mode->create_bo(bufmgr, i, width, height);
					 dst[i] = mode->create_bo(bufmgr, ~i, width, height);
				 }
				 for (loop = 0; loop < 10; loop++) {
					 gem_quiescent_gpu(fd);
					 for (i = 0; i < num_buffers; i++) {
						 mode->set_bo(src[i], i, width, height);
						 mode->set_bo(dst[i], i, width, height);
					 }
					 for (i = 0; i < num_buffers; i++)
						 intel_copy_bo(batch, dst[i], src[i], width, height);
					 for (i = num_buffers; i--; )
						 mode->set_bo(src[i], 0xdeadbeef, width, height);
					 for (i = 0; i < num_buffers; i++)
						 mode->cmp_bo(dst[i], i, width, height);
				 }
				 exit(0);
			}
		}
		for (nc = 0; nc < ARRAY_SIZE(children); nc++) {
			int status = -1;
			while (waitpid(children[nc], &status, 0) == -1 &&
			       errno == -EINTR)
				;
			igt_assert(status == 0);
		}
	}

	/* try to read the results before the copy completes */
	igt_subtest_f("%s-early-read-forked", mode->name) {
		for (nc = 0; nc < ARRAY_SIZE(children); nc++) {
			switch ((children[nc] = fork())) {
			case -1: igt_assert(0);
			default: break;
			case 0:
				 /* recreate process local variables */
				 bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
				 drm_intel_bufmgr_gem_enable_reuse(bufmgr);
				 batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
				 for (i = 0; i < num_buffers; i++) {
					 src[i] = mode->create_bo(bufmgr, i, width, height);
					 dst[i] = mode->create_bo(bufmgr, ~i, width, height);
				 }
				 for (loop = 0; loop < 10; loop++) {
					 gem_quiescent_gpu(fd);
					 for (i = num_buffers; i--; )
						 mode->set_bo(src[i], 0xdeadbeef, width, height);
					 for (i = 0; i < num_buffers; i++)
						 intel_copy_bo(batch, dst[i], src[i], width, height);
					 for (i = num_buffers; i--; )
						 mode->cmp_bo(dst[i], 0xdeadbeef, width, height);
				 }
				 exit(0);
			}
		}
		for (nc = 0; nc < ARRAY_SIZE(children); nc++) {
			int status = -1;
			while (waitpid(children[nc], &status, 0) == -1 &&
			       errno == -EINTR)
				;
			igt_assert(status == 0);
		}
	}

	/* and finally try to trick the kernel into loosing the pending write */
	igt_subtest_f("%s-gpu-read-after-write-forked", mode->name) {
		for (nc = 0; nc < ARRAY_SIZE(children); nc++) {
			switch ((children[nc] = fork())) {
			case -1: igt_assert(0);
			default: break;
			case 0:
				 /* recreate process local variables */
				 bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
				 drm_intel_bufmgr_gem_enable_reuse(bufmgr);
				 batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
				 for (i = 0; i < num_buffers; i++) {
					 src[i] = mode->create_bo(bufmgr, i, width, height);
					 dst[i] = mode->create_bo(bufmgr, ~i, width, height);
				 }
				 dummy = mode->create_bo(bufmgr, 0, width, height);
				 for (loop = 0; loop < 10; loop++) {
					 gem_quiescent_gpu(fd);
					 for (i = num_buffers; i--; )
						 mode->set_bo(src[i], 0xabcdabcd, width, height);
					 for (i = 0; i < num_buffers; i++)
						 intel_copy_bo(batch, dst[i], src[i], width, height);
					 for (i = num_buffers; i--; )
						 intel_copy_bo(batch, dummy, dst[i], width, height);
					 for (i = num_buffers; i--; )
						 mode->cmp_bo(dst[i], 0xabcdabcd, width, height);
				 }
				 exit(0);
			}
		}
		for (nc = 0; nc < ARRAY_SIZE(children); nc++) {
			int status = -1;
			while (waitpid(children[nc], &status, 0) == -1 &&
			       errno == -EINTR)
				;
			igt_assert(status == 0);
		}
	}

	igt_stop_signal_helper();

	if (!igt_only_list_subtests()) {
		for (i = 0; i < num_buffers; i++) {
			drm_intel_bo_unreference(src[i]);
			drm_intel_bo_unreference(dst[i]);
		}
		drm_intel_bo_unreference(dummy);
	}
}

int
main(int argc, char **argv)
{
	int max, i;

	igt_subtest_init(argc, argv);
	igt_skip_on_simulation();

	fd = drm_open_any();

	max = gem_aperture_size (fd) / (1024 * 1024) / 2;
	if (num_buffers > max)
		num_buffers = max;

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));

	for (i = 0; i < ARRAY_SIZE(access_modes); i++)
		run_modes(&access_modes[i]);

	return 0;
}
