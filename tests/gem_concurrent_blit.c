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

int fd, devid, gen;
struct intel_batchbuffer *batch;

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
			igt_assert_eq_u32(tmp[i], val);
		free(tmp);
	} else {
		uint32_t t;
		for (i = 0; i < size; i++) {
			t = 0;
			do_or_die(drm_intel_bo_get_subdata(bo, 4*i, 4, &t));
			igt_assert_eq_u32(t, val);
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
		igt_assert_eq_u32(*vaddr++, val);
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
		igt_assert_eq_u32(*vaddr++, val);
	drm_intel_bo_unmap(bo);
}

static void
gpu_set_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
	struct drm_i915_gem_relocation_entry reloc[1];
	struct drm_i915_gem_exec_object2 gem_exec[2];
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_pwrite gem_pwrite;
	struct drm_i915_gem_create create;
	uint32_t buf[10], *b;

	memset(reloc, 0, sizeof(reloc));
	memset(gem_exec, 0, sizeof(gem_exec));
	memset(&execbuf, 0, sizeof(execbuf));

	b = buf;
	*b++ = XY_COLOR_BLT_CMD_NOLEN |
		((gen >= 8) ? 5 : 4) |
		COLOR_BLT_WRITE_ALPHA | XY_COLOR_BLT_WRITE_RGB;
	*b++ = 0xf0 << 16 | 1 << 25 | 1 << 24 | width << 2;
	*b++ = 0;
	*b++ = height << 16 | width;
	reloc[0].offset = (b - buf) * sizeof(uint32_t);
	reloc[0].target_handle = bo->handle;
	reloc[0].read_domains = I915_GEM_DOMAIN_RENDER;
	reloc[0].write_domain = I915_GEM_DOMAIN_RENDER;
	*b++ = 0;
	if (gen >= 8)
		*b++ = 0;
	*b++ = val;
	*b++ = MI_BATCH_BUFFER_END;
	if ((b - buf) & 1)
		*b++ = 0;

	gem_exec[0].handle = bo->handle;
	gem_exec[0].flags = EXEC_OBJECT_NEEDS_FENCE;

	create.handle = 0;
	create.size = 4096;
	drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);
	gem_exec[1].handle = create.handle;
	gem_exec[1].relocation_count = 1;
	gem_exec[1].relocs_ptr = (uintptr_t)reloc;

	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 2;
	execbuf.batch_len = (b - buf) * sizeof(buf[0]);
	execbuf.flags = 1 << 11;
	if (HAS_BLT_RING(devid))
		execbuf.flags |= I915_EXEC_BLT;

	gem_pwrite.handle = gem_exec[1].handle;
	gem_pwrite.offset = 0;
	gem_pwrite.size = execbuf.batch_len;
	gem_pwrite.data_ptr = (uintptr_t)buf;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite) == 0)
		drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);

	drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &create.handle);
}

static void
gpu_cmp_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
	dri_bo *tmp = drm_intel_bo_alloc(bo->bufmgr, "tmp", 4*width*height, 0);
	intel_copy_bo(batch, tmp, bo, width*height*4);
	cpu_cmp_bo(tmp, val, width, height);
	drm_intel_bo_unreference(tmp);
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
	{ .set_bo = gpu_set_bo, .cmp_bo = gpu_cmp_bo,
		.create_bo = unmapped_create_bo, .name = "gpu" },
};

#define MAX_NUM_BUFFERS 1024
int num_buffers = MAX_NUM_BUFFERS;
drm_intel_bufmgr *bufmgr;
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

	for (loop = 0; loop < 10; loop++)
		do_test_func(mode, src, dst, dummy);
}

static void run_forked(struct access_mode *mode,
		       drm_intel_bo **src, drm_intel_bo **dst,
		       drm_intel_bo *dummy,
		       do_test do_test_func)
{
	const int old_num_buffers = num_buffers;

	num_buffers /= 16;
	num_buffers += 2;

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

	igt_fork_signal_helper();
	run_basic_modes(mode, src, dst, dummy, "-interruptible", run_interruptible);
	igt_stop_signal_helper();

	igt_fixture {
		for (int i = 0; i < num_buffers; i++) {
			drm_intel_bo_unreference(src[i]);
			drm_intel_bo_unreference(dst[i]);
		}
		drm_intel_bo_unreference(dummy);
		intel_batchbuffer_free(batch);
		drm_intel_bufmgr_destroy(bufmgr);
	}

	igt_fork_signal_helper();
	run_basic_modes(mode, src, dst, dummy, "-forked", run_forked);
	igt_stop_signal_helper();
}

igt_main
{
	int max, i;

	igt_skip_on_simulation();

	igt_fixture {
		fd = drm_open_any();
		devid = intel_get_drm_devid(fd);
		gen = intel_gen(devid);

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
