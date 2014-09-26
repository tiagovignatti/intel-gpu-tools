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
prw_cmp_bo(drm_intel_bo *bo, uint32_t val, int width, int height, drm_intel_bo *tmp)
{
	int size = width * height, i;
	uint32_t *vaddr;

	do_or_die(drm_intel_bo_map(tmp, true));
	do_or_die(drm_intel_bo_get_subdata(bo, 0, 4*size, tmp->virtual));
	vaddr = tmp->virtual;
	for (i = 0; i < size; i++)
		igt_assert_eq_u32(vaddr[i], val);
	drm_intel_bo_unmap(tmp);
}

static drm_intel_bo *
unmapped_create_bo(drm_intel_bufmgr *bufmgr, int width, int height)
{
	drm_intel_bo *bo;

	bo = drm_intel_bo_alloc(bufmgr, "bo", 4*width*height, 0);
	igt_assert(bo);

	return bo;
}

static void
gtt_set_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
	uint32_t *vaddr = bo->virtual;
	int size = width * height;

	drm_intel_gem_bo_start_gtt_access(bo, true);
	while (size--)
		*vaddr++ = val;
}

static void
gtt_cmp_bo(drm_intel_bo *bo, uint32_t val, int width, int height, drm_intel_bo *tmp)
{
	uint32_t *vaddr = bo->virtual;
	int y;

	/* GTT access is slow. So we just compare a few points */
	drm_intel_gem_bo_start_gtt_access(bo, false);
	for (y = 0; y < height; y++)
		igt_assert_eq_u32(vaddr[y*width+y], val);
}

static drm_intel_bo *
map_bo(drm_intel_bo *bo)
{
	/* gtt map doesn't have a write parameter, so just keep the mapping
	 * around (to avoid the set_domain with the gtt write domain set) and
	 * manually tell the kernel when we start access the gtt. */
	do_or_die(drm_intel_gem_bo_map_gtt(bo));

	return bo;
}

static drm_intel_bo *
tile_bo(drm_intel_bo *bo, int width)
{
	uint32_t tiling = I915_TILING_X;
	uint32_t stride = width * 4;

	do_or_die(drm_intel_bo_set_tiling(bo, &tiling, stride));

	return bo;
}

static drm_intel_bo *
gtt_create_bo(drm_intel_bufmgr *bufmgr, int width, int height)
{
	return map_bo(unmapped_create_bo(bufmgr, width, height));
}

static drm_intel_bo *
gttX_create_bo(drm_intel_bufmgr *bufmgr, int width, int height)
{
	return tile_bo(gtt_create_bo(bufmgr, width, height), width);
}

static drm_intel_bo *
gpu_create_bo(drm_intel_bufmgr *bufmgr, int width, int height)
{
	return unmapped_create_bo(bufmgr, width, height);
}


static drm_intel_bo *
gpuX_create_bo(drm_intel_bufmgr *bufmgr, int width, int height)
{
	return tile_bo(gpu_create_bo(bufmgr, width, height), width);
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
cpu_cmp_bo(drm_intel_bo *bo, uint32_t val, int width, int height, drm_intel_bo *tmp)
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
	uint32_t tiling, swizzle;

	drm_intel_bo_get_tiling(bo, &tiling, &swizzle);

	memset(reloc, 0, sizeof(reloc));
	memset(gem_exec, 0, sizeof(gem_exec));
	memset(&execbuf, 0, sizeof(execbuf));

	b = buf;
	*b++ = XY_COLOR_BLT_CMD_NOLEN |
		((gen >= 8) ? 5 : 4) |
		COLOR_BLT_WRITE_ALPHA | XY_COLOR_BLT_WRITE_RGB;
	if (gen >= 4 && tiling) {
		b[-1] |= XY_COLOR_BLT_TILED;
		*b = width;
	} else
		*b = width << 2;
	*b++ |= 0xf0 << 16 | 1 << 25 | 1 << 24;
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
	if (gen >= 6)
		execbuf.flags = I915_EXEC_BLT;

	gem_pwrite.handle = gem_exec[1].handle;
	gem_pwrite.offset = 0;
	gem_pwrite.size = execbuf.batch_len;
	gem_pwrite.data_ptr = (uintptr_t)buf;
	do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite));
	do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));

	drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &create.handle);
}

static void
gpu_cmp_bo(drm_intel_bo *bo, uint32_t val, int width, int height, drm_intel_bo *tmp)
{
	intel_copy_bo(batch, tmp, bo, width*height*4);
	cpu_cmp_bo(tmp, val, width, height, NULL);
}

struct access_mode {
	void (*set_bo)(drm_intel_bo *bo, uint32_t val, int w, int h);
	void (*cmp_bo)(drm_intel_bo *bo, uint32_t val, int w, int h, drm_intel_bo *tmp);
	drm_intel_bo *(*create_bo)(drm_intel_bufmgr *bufmgr, int width, int height);
	const char *name;
};

struct access_mode access_modes[] = {
	{ .set_bo = prw_set_bo, .cmp_bo = prw_cmp_bo,
		.create_bo = unmapped_create_bo, .name = "prw" },
	{ .set_bo = cpu_set_bo, .cmp_bo = cpu_cmp_bo,
		.create_bo = unmapped_create_bo, .name = "cpu" },
	{ .set_bo = gtt_set_bo, .cmp_bo = gtt_cmp_bo,
		.create_bo = gtt_create_bo, .name = "gtt" },
	{ .set_bo = gtt_set_bo, .cmp_bo = gtt_cmp_bo,
		.create_bo = gttX_create_bo, .name = "gttX" },
	{ .set_bo = gpu_set_bo, .cmp_bo = gpu_cmp_bo,
		.create_bo = gpu_create_bo, .name = "gpu" },
	{ .set_bo = gpu_set_bo, .cmp_bo = gpu_cmp_bo,
		.create_bo = gpuX_create_bo, .name = "gpuX" },
};

#define MAX_NUM_BUFFERS 1024
int num_buffers = MAX_NUM_BUFFERS;
const int width = 512, height = 512;
igt_render_copyfunc_t rendercopy;

typedef void (*do_copy)(drm_intel_bo *dst, drm_intel_bo *src);

static void render_copy_bo(drm_intel_bo *dst, drm_intel_bo *src)
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

static void blt_copy_bo(drm_intel_bo *dst, drm_intel_bo *src)
{
	intel_blt_copy(batch,
		       src, 0, 0, 4*width,
		       dst, 0, 0, 4*width,
		       width, height, 32);
}

static void do_overwrite_source(struct access_mode *mode,
				drm_intel_bo **src, drm_intel_bo **dst,
				drm_intel_bo *dummy,
				do_copy do_copy_func)
{
	int i;

	gem_quiescent_gpu(fd);
	for (i = 0; i < num_buffers; i++) {
		mode->set_bo(src[i], i, width, height);
		mode->set_bo(dst[i], ~i, width, height);
	}
	for (i = 0; i < num_buffers; i++)
		do_copy_func(dst[i], src[i]);
	for (i = num_buffers; i--; )
		mode->set_bo(src[i], 0xdeadbeef, width, height);
	for (i = 0; i < num_buffers; i++)
		mode->cmp_bo(dst[i], i, width, height, dummy);
}

static void do_early_read(struct access_mode *mode,
			  drm_intel_bo **src, drm_intel_bo **dst,
			  drm_intel_bo *dummy,
			  do_copy do_copy_func)
{
	int i;

	gem_quiescent_gpu(fd);
	for (i = num_buffers; i--; )
		mode->set_bo(src[i], 0xdeadbeef, width, height);
	for (i = 0; i < num_buffers; i++)
		do_copy_func(dst[i], src[i]);
	for (i = num_buffers; i--; )
		mode->cmp_bo(dst[i], 0xdeadbeef, width, height, dummy);
}

static void do_gpu_read_after_write(struct access_mode *mode,
				    drm_intel_bo **src, drm_intel_bo **dst,
				    drm_intel_bo *dummy,
				    do_copy do_copy_func)
{
	int i;

	gem_quiescent_gpu(fd);
	for (i = num_buffers; i--; )
		mode->set_bo(src[i], 0xabcdabcd, width, height);
	for (i = 0; i < num_buffers; i++)
		do_copy_func(dst[i], src[i]);
	for (i = num_buffers; i--; )
		do_copy_func(dummy, dst[i]);
	for (i = num_buffers; i--; )
		mode->cmp_bo(dst[i], 0xabcdabcd, width, height, dummy);
}

typedef void (*do_test)(struct access_mode *mode,
			drm_intel_bo **src, drm_intel_bo **dst,
			drm_intel_bo *dummy,
			do_copy do_copy_func);

typedef void (*run_wrap)(struct access_mode *mode,
			 drm_intel_bo **src, drm_intel_bo **dst,
			 drm_intel_bo *dummy,
			 do_test do_test_func,
			 do_copy do_copy_func);

static void run_single(struct access_mode *mode,
		       drm_intel_bo **src, drm_intel_bo **dst,
		       drm_intel_bo *dummy,
		       do_test do_test_func,
		       do_copy do_copy_func)
{
	do_test_func(mode, src, dst, dummy, do_copy_func);
}

static void run_interruptible(struct access_mode *mode,
			      drm_intel_bo **src, drm_intel_bo **dst,
			      drm_intel_bo *dummy,
			      do_test do_test_func,
			      do_copy do_copy_func)
{
	int loop;

	for (loop = 0; loop < 10; loop++)
		do_test_func(mode, src, dst, dummy, do_copy_func);
}

static void run_forked(struct access_mode *mode,
		       drm_intel_bo **src, drm_intel_bo **dst,
		       drm_intel_bo *dummy,
		       do_test do_test_func,
		       do_copy do_copy_func)
{
	const int old_num_buffers = num_buffers;
	drm_intel_bufmgr *bufmgr;

	num_buffers /= 16;
	num_buffers += 2;

	igt_fork(child, 16) {
		/* recreate process local variables */
		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		drm_intel_bufmgr_gem_enable_reuse(bufmgr);
		batch = intel_batchbuffer_alloc(bufmgr, devid);
		for (int i = 0; i < num_buffers; i++) {
			src[i] = mode->create_bo(bufmgr, width, height);
			dst[i] = mode->create_bo(bufmgr, width, height);
		}
		dummy = mode->create_bo(bufmgr, width, height);
		for (int loop = 0; loop < 10; loop++)
			do_test_func(mode, src, dst, dummy, do_copy_func);
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

static void bcs_require(void)
{
}

static void rcs_require(void)
{
	igt_require(rendercopy);
}

static void
run_basic_modes(struct access_mode *mode,
		drm_intel_bo **src, drm_intel_bo **dst,
		drm_intel_bo *dummy, const char *suffix,
		run_wrap run_wrap_func)
{
	struct {
		const char *prefix;
		do_copy copy;
		void (*require)(void);
	} pipelines[] = {
		{ "bcs", blt_copy_bo, bcs_require },
		{ "rcs", render_copy_bo, rcs_require },
		{ NULL, NULL }
	}, *p;

	for (p = pipelines; p->prefix; p++) {
		/* try to overwrite the source values */
		igt_subtest_f("%s-%s-overwrite-source%s", mode->name, p->prefix, suffix) {
			p->require();
			run_wrap_func(mode, src, dst, dummy,
				      do_overwrite_source, p->copy);
		}

		/* try to read the results before the copy completes */
		igt_subtest_f("%s-%s-early-read%s", mode->name, p->prefix, suffix) {
			p->require();
			run_wrap_func(mode, src, dst, dummy,
				      do_early_read, p->copy);
		}

		/* and finally try to trick the kernel into loosing the pending write */
		igt_subtest_f("%s-%s-gpu-read-after-write%s", mode->name, p->prefix, suffix) {
			p->require();
			run_wrap_func(mode, src, dst, dummy,
				      do_gpu_read_after_write, p->copy);
		}
	}
}

static void
run_modes(struct access_mode *mode)
{
	drm_intel_bo *src[MAX_NUM_BUFFERS], *dst[MAX_NUM_BUFFERS], *dummy = NULL;
	drm_intel_bufmgr *bufmgr;

	igt_fixture {
		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		drm_intel_bufmgr_gem_enable_reuse(bufmgr);
		batch = intel_batchbuffer_alloc(bufmgr, devid);

		for (int i = 0; i < num_buffers; i++) {
			src[i] = mode->create_bo(bufmgr, width, height);
			dst[i] = mode->create_bo(bufmgr, width, height);
		}
		dummy = mode->create_bo(bufmgr, width, height);
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
		rendercopy = igt_get_render_copyfunc(devid);

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
