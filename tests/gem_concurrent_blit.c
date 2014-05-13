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

#include <drm.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"
#include "intel_chipset.h"
#include "igt_aux.h"

static void
prw_set_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
	int size = width * height, i;
	uint32_t *tmp;

	tmp = malloc(4*size);
	if (tmp) {
		for (i = 0; i < size; i++)
			tmp[i] = val;
		drm_intel_bo_subdata(bo, 0, 4*size, tmp);
		free(tmp);
	} else {
		for (i = 0; i < size; i++)
			drm_intel_bo_subdata(bo, 4*i, 4, &val);
	}
}

static void
prw_cmp_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
	int size = width * height, i;
	uint32_t *tmp;

	tmp = malloc(4*size);
	if (tmp) {
		memset(tmp, 0, 4*size);
		do_or_die(drm_intel_bo_get_subdata(bo, 0, 4*size, tmp));
		for (i = 0; i < size; i++)
			igt_assert(tmp[i] == val);
		free(tmp);
	} else {
		uint32_t t;
		for (i = 0; i < size; i++) {
			t = 0;
			do_or_die(drm_intel_bo_get_subdata(bo, 4*i, 4, &t));
			igt_assert(t == val);
		}
	}
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

#define MAX_NUM_BUFFERS 1024
int num_buffers = MAX_NUM_BUFFERS, fd;
drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
int width = 512, height = 512;

static void do_overwrite_source(struct access_mode *mode,
				drm_intel_bo **src, drm_intel_bo **dst,
				drm_intel_bo *dummy)
{
	int i;

	gem_quiescent_gpu(fd);
	for (i = 0; i < num_buffers; i++) {
		mode->set_bo(src[i], i, width, height);
		mode->set_bo(dst[i], i, width, height);
	}
	for (i = 0; i < num_buffers; i++)
		intel_copy_bo(batch, dst[i], src[i], width*height*4);
	for (i = num_buffers; i--; )
		mode->set_bo(src[i], 0xdeadbeef, width, height);
	for (i = 0; i < num_buffers; i++)
		mode->cmp_bo(dst[i], i, width, height);
}

static void do_early_read(struct access_mode *mode,
			  drm_intel_bo **src, drm_intel_bo **dst,
			  drm_intel_bo *dummy)
{
	int i;

	gem_quiescent_gpu(fd);
	for (i = num_buffers; i--; )
		mode->set_bo(src[i], 0xdeadbeef, width, height);
	for (i = 0; i < num_buffers; i++)
		intel_copy_bo(batch, dst[i], src[i], width*height*4);
	for (i = num_buffers; i--; )
		mode->cmp_bo(dst[i], 0xdeadbeef, width, height);
}

static void do_gpu_read_after_write(struct access_mode *mode,
				    drm_intel_bo **src, drm_intel_bo **dst,
				    drm_intel_bo *dummy)
{
	int i;

	gem_quiescent_gpu(fd);
	for (i = num_buffers; i--; )
		mode->set_bo(src[i], 0xabcdabcd, width, height);
	for (i = 0; i < num_buffers; i++)
		intel_copy_bo(batch, dst[i], src[i], width*height*4);
	for (i = num_buffers; i--; )
		intel_copy_bo(batch, dummy, dst[i], width*height*4);
	for (i = num_buffers; i--; )
		mode->cmp_bo(dst[i], 0xabcdabcd, width, height);
}

typedef void (*do_test)(struct access_mode *mode,
			drm_intel_bo **src, drm_intel_bo **dst,
			drm_intel_bo *dummy);

typedef void (*run_wrap)(struct access_mode *mode,
			 drm_intel_bo **src, drm_intel_bo **dst,
			 drm_intel_bo *dummy,
			 do_test do_test_func);

static void run_single(struct access_mode *mode,
		       drm_intel_bo **src, drm_intel_bo **dst,
		       drm_intel_bo *dummy,
		       do_test do_test_func)
{
	do_test_func(mode, src, dst, dummy);
}


static void run_interruptible(struct access_mode *mode,
			      drm_intel_bo **src, drm_intel_bo **dst,
			      drm_intel_bo *dummy,
			      do_test do_test_func)
{
	int loop;

	igt_fork_signal_helper();

	for (loop = 0; loop < 10; loop++)
		do_test_func(mode, src, dst, dummy);

	igt_stop_signal_helper();
}

static void run_forked(struct access_mode *mode,
		       drm_intel_bo **src, drm_intel_bo **dst,
		       drm_intel_bo *dummy,
		       do_test do_test_func)
{
	const int old_num_buffers = num_buffers;

	num_buffers /= 16;
	num_buffers += 2;

	igt_fork_signal_helper();

	igt_fork(child, 16) {
		/* recreate process local variables */
		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		drm_intel_bufmgr_gem_enable_reuse(bufmgr);
		batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
		for (int i = 0; i < num_buffers; i++) {
			src[i] = mode->create_bo(bufmgr, i, width, height);
			dst[i] = mode->create_bo(bufmgr, ~i, width, height);
		}
		dummy = mode->create_bo(bufmgr, 0, width, height);
		for (int loop = 0; loop < 10; loop++)
			do_test_func(mode, src, dst, dummy);
		/* as we borrow the fd, we need to reap our bo */
		for (int i = 0; i < num_buffers; i++) {
			drm_intel_bo_unreference(src[i]);
			drm_intel_bo_unreference(dst[i]);
		}
		drm_intel_bo_unreference(dummy);
		intel_batchbuffer_free(batch);
		drm_intel_bufmgr_destroy(bufmgr);
	}

	igt_waitchildren();

	igt_stop_signal_helper();

	num_buffers = old_num_buffers;
}

static void
run_basic_modes(struct access_mode *mode,
		drm_intel_bo **src, drm_intel_bo **dst,
		drm_intel_bo *dummy, const char *suffix,
		run_wrap run_wrap_func)
{
	/* try to overwrite the source values */
	igt_subtest_f("%s-overwrite-source%s", mode->name, suffix)
		run_wrap_func(mode, src, dst, dummy, do_overwrite_source);

	/* try to read the results before the copy completes */
	igt_subtest_f("%s-early-read%s", mode->name, suffix)
		run_wrap_func(mode, src, dst, dummy, do_early_read);

	/* and finally try to trick the kernel into loosing the pending write */
	igt_subtest_f("%s-gpu-read-after-write%s", mode->name, suffix)
		run_wrap_func(mode, src, dst, dummy, do_gpu_read_after_write);
}

static void
run_modes(struct access_mode *mode)
{
	drm_intel_bo *src[MAX_NUM_BUFFERS], *dst[MAX_NUM_BUFFERS], *dummy = NULL;

	igt_fixture {
		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		drm_intel_bufmgr_gem_enable_reuse(bufmgr);
		batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));

		for (int i = 0; i < num_buffers; i++) {
			src[i] = mode->create_bo(bufmgr, i, width, height);
			dst[i] = mode->create_bo(bufmgr, ~i, width, height);
		}
		dummy = mode->create_bo(bufmgr, 0, width, height);
	}

	run_basic_modes(mode, src, dst, dummy, "", run_single);
	run_basic_modes(mode, src, dst, dummy, "-interruptible", run_interruptible);

	igt_fixture {
		for (int i = 0; i < num_buffers; i++) {
			drm_intel_bo_unreference(src[i]);
			drm_intel_bo_unreference(dst[i]);
		}
		drm_intel_bo_unreference(dummy);
		intel_batchbuffer_free(batch);
		drm_intel_bufmgr_destroy(bufmgr);
	}

	run_basic_modes(mode, src, dst, dummy, "-forked", run_forked);
}

igt_main
{
	int max, i;

	igt_skip_on_simulation();

	igt_fixture {
		fd = drm_open_any();

		max = gem_aperture_size (fd) / (1024 * 1024) / 2;
		if (num_buffers > max)
			num_buffers = max;

		max = intel_get_total_ram_mb() * 3 / 4;
		if (num_buffers > max)
			num_buffers = max;
		num_buffers /= 2;
		igt_info("using 2x%d buffers, each 1MiB\n", num_buffers);
	}

	for (i = 0; i < ARRAY_SIZE(access_modes); i++)
		run_modes(&access_modes[i]);
}
