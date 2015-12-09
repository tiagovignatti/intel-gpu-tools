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

/** @file gem_concurrent.c
 *
 * This is a test of pread/pwrite/mmap behavior when writing to active
 * buffers.
 *
 * Based on gem_gtt_concurrent_blt.
 */

#include "igt.h"
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

#include "intel_bufmgr.h"

IGT_TEST_DESCRIPTION("Test of pread/pwrite/mmap behavior when writing to active"
		     " buffers.");

int fd, devid, gen;
struct intel_batchbuffer *batch;
int all;

static void
nop_release_bo(drm_intel_bo *bo)
{
	drm_intel_bo_unreference(bo);
}

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

static drm_intel_bo *
snoop_create_bo(drm_intel_bufmgr *bufmgr, int width, int height)
{
	drm_intel_bo *bo;

	igt_skip_on(gem_has_llc(fd));

	bo = unmapped_create_bo(bufmgr, width, height);
	gem_set_caching(fd, bo->handle, I915_CACHING_CACHED);
	drm_intel_bo_disable_reuse(bo);

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
wc_create_bo(drm_intel_bufmgr *bufmgr, int width, int height)
{
	drm_intel_bo *bo;

	gem_require_mmap_wc(fd);

	bo = unmapped_create_bo(bufmgr, width, height);
	bo->virtual = __gem_mmap__wc(fd, bo->handle, 0, bo->size, PROT_READ | PROT_WRITE);
	return bo;
}

static void
wc_release_bo(drm_intel_bo *bo)
{
	munmap(bo->virtual, bo->size);
	bo->virtual = NULL;

	nop_release_bo(bo);
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
	do_ioctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite);
	do_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);

	drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &create.handle);
}

static void
gpu_cmp_bo(drm_intel_bo *bo, uint32_t val, int width, int height, drm_intel_bo *tmp)
{
	intel_blt_copy(batch,
		       bo, 0, 0, 4*width,
		       tmp, 0, 0, 4*width,
		       width, height, 32);
	cpu_cmp_bo(tmp, val, width, height, NULL);
}

const struct access_mode {
	const char *name;
	void (*set_bo)(drm_intel_bo *bo, uint32_t val, int w, int h);
	void (*cmp_bo)(drm_intel_bo *bo, uint32_t val, int w, int h, drm_intel_bo *tmp);
	drm_intel_bo *(*create_bo)(drm_intel_bufmgr *bufmgr, int width, int height);
	void (*release_bo)(drm_intel_bo *bo);
} access_modes[] = {
	{
		.name = "prw",
		.set_bo = prw_set_bo,
		.cmp_bo = prw_cmp_bo,
		.create_bo = unmapped_create_bo,
		.release_bo = nop_release_bo,
	},
	{
		.name = "cpu",
		.set_bo = cpu_set_bo,
		.cmp_bo = cpu_cmp_bo,
		.create_bo = unmapped_create_bo,
		.release_bo = nop_release_bo,
	},
	{
		.name = "snoop",
		.set_bo = cpu_set_bo,
		.cmp_bo = cpu_cmp_bo,
		.create_bo = snoop_create_bo,
		.release_bo = nop_release_bo,
	},
	{
		.name = "gtt",
		.set_bo = gtt_set_bo,
		.cmp_bo = gtt_cmp_bo,
		.create_bo = gtt_create_bo,
		.release_bo = nop_release_bo,
	},
	{
		.name = "gttX",
		.set_bo = gtt_set_bo,
		.cmp_bo = gtt_cmp_bo,
		.create_bo = gttX_create_bo,
		.release_bo = nop_release_bo,
	},
	{
		.name = "wc",
		.set_bo = gtt_set_bo,
		.cmp_bo = gtt_cmp_bo,
		.create_bo = wc_create_bo,
		.release_bo = wc_release_bo,
	},
	{
		.name = "gpu",
		.set_bo = gpu_set_bo,
		.cmp_bo = gpu_cmp_bo,
		.create_bo = gpu_create_bo,
		.release_bo = nop_release_bo,
	},
	{
		.name = "gpuX",
		.set_bo = gpu_set_bo,
		.cmp_bo = gpu_cmp_bo,
		.create_bo = gpuX_create_bo,
		.release_bo = nop_release_bo,
	},
};

#define MAX_NUM_BUFFERS 1024
int num_buffers = MAX_NUM_BUFFERS;
const int width = 512, height = 512;
igt_render_copyfunc_t rendercopy;

struct buffers {
	const struct access_mode *mode;
	drm_intel_bufmgr *bufmgr;
	drm_intel_bo *src[MAX_NUM_BUFFERS], *dst[MAX_NUM_BUFFERS];
	drm_intel_bo *dummy, *spare;
	int count;
};

static void *buffers_init(struct buffers *data,
			  const struct access_mode *mode,
			  int _fd)
{
	data->mode = mode;
	data->count = 0;

	data->bufmgr = drm_intel_bufmgr_gem_init(_fd, 4096);
	igt_assert(data->bufmgr);

	drm_intel_bufmgr_gem_enable_reuse(data->bufmgr);
	return intel_batchbuffer_alloc(data->bufmgr, devid);
}

static void buffers_destroy(struct buffers *data)
{
	if (data->count == 0)
		return;

	for (int i = 0; i < data->count; i++) {
		data->mode->release_bo(data->src[i]);
		data->mode->release_bo(data->dst[i]);
	}
	data->mode->release_bo(data->dummy);
	data->mode->release_bo(data->spare);
	data->count = 0;
}

static void buffers_create(struct buffers *data,
			   int count)
{
	igt_assert(data->bufmgr);

	buffers_destroy(data);

	for (int i = 0; i < count; i++) {
		data->src[i] =
			data->mode->create_bo(data->bufmgr, width, height);
		data->dst[i] =
			data->mode->create_bo(data->bufmgr, width, height);
	}
	data->dummy = data->mode->create_bo(data->bufmgr, width, height);
	data->spare = data->mode->create_bo(data->bufmgr, width, height);
	data->count = count;
}

static void buffers_fini(struct buffers *data)
{
	if (data->bufmgr == NULL)
		return;

	buffers_destroy(data);

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(data->bufmgr);
	data->bufmgr = NULL;
}

typedef void (*do_copy)(drm_intel_bo *dst, drm_intel_bo *src);
typedef struct igt_hang_ring (*do_hang)(void);

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

static void cpu_copy_bo(drm_intel_bo *dst, drm_intel_bo *src)
{
	const int size = width * height * sizeof(uint32_t);
	void *d, *s;

	gem_set_domain(fd, src->handle, I915_GEM_DOMAIN_CPU, 0);
	gem_set_domain(fd, dst->handle, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
	s = gem_mmap__cpu(fd, src->handle, 0, size, PROT_READ);
	d = gem_mmap__cpu(fd, dst->handle, 0, size, PROT_WRITE);

	memcpy(d, s, size);

	munmap(d, size);
	munmap(s, size);
}

static void gtt_copy_bo(drm_intel_bo *dst, drm_intel_bo *src)
{
	const int size = width * height * sizeof(uint32_t);
	void *d, *s;

	gem_set_domain(fd, src->handle, I915_GEM_DOMAIN_GTT, 0);
	gem_set_domain(fd, dst->handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);

	s = gem_mmap__gtt(fd, src->handle, size, PROT_READ);
	d = gem_mmap__gtt(fd, dst->handle, size, PROT_WRITE);

	memcpy(d, s, size);

	munmap(d, size);
	munmap(s, size);
}

static void wc_copy_bo(drm_intel_bo *dst, drm_intel_bo *src)
{
	const int size = width * height * sizeof(uint32_t);
	void *d, *s;

	gem_set_domain(fd, src->handle, I915_GEM_DOMAIN_GTT, 0);
	gem_set_domain(fd, dst->handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);

	s = gem_mmap__wc(fd, src->handle, 0, size, PROT_READ);
	d = gem_mmap__wc(fd, dst->handle, 0, size, PROT_WRITE);

	memcpy(d, s, size);

	munmap(d, size);
	munmap(s, size);
}

static struct igt_hang_ring no_hang(void)
{
	return (struct igt_hang_ring){0, 0};
}

static struct igt_hang_ring bcs_hang(void)
{
	return igt_hang_ring(fd, I915_EXEC_BLT);
}

static struct igt_hang_ring rcs_hang(void)
{
	return igt_hang_ring(fd, I915_EXEC_RENDER);
}

static void hang_require(void)
{
	igt_require_hang_ring(fd, -1);
}

static void check_gpu(void)
{
	unsigned missed_irq = 0;
	FILE *file;

	gem_quiescent_gpu(fd);

	file = igt_debugfs_fopen("i915_ring_missed_irq", "r");
	if (file) {
		fscanf(file, "%x", &missed_irq);
		fclose(file);
	}
	file = igt_debugfs_fopen("i915_ring_missed_irq", "w");
	if (file) {
		fwrite("0\n", 1, 2, file);
		fclose(file);
	}
	igt_assert_eq(missed_irq, 0);
}

static void do_basic(struct buffers *buffers,
		     do_copy do_copy_func,
		     do_hang do_hang_func)
{
	gem_quiescent_gpu(fd);

	for (int i = 0; i < buffers->count; i++) {
		struct igt_hang_ring hang = do_hang_func();

		buffers->mode->set_bo(buffers->src[i], i, width, height);
		buffers->mode->set_bo(buffers->dst[i], ~i, width, height);
		do_copy_func(buffers->dst[i], buffers->src[i]);
		buffers->mode->cmp_bo(buffers->dst[i], i, width, height, buffers->dummy);

		igt_post_hang_ring(fd, hang);
	}
}

static void do_overwrite_source(struct buffers *buffers,
				do_copy do_copy_func,
				do_hang do_hang_func)
{
	struct igt_hang_ring hang;
	int i;

	gem_quiescent_gpu(fd);
	for (i = 0; i < buffers->count; i++) {
		buffers->mode->set_bo(buffers->src[i], i, width, height);
		buffers->mode->set_bo(buffers->dst[i], ~i, width, height);
	}
	for (i = 0; i < buffers->count; i++)
		do_copy_func(buffers->dst[i], buffers->src[i]);
	hang = do_hang_func();
	for (i = buffers->count; i--; )
		buffers->mode->set_bo(buffers->src[i], 0xdeadbeef, width, height);
	for (i = 0; i < buffers->count; i++)
		buffers->mode->cmp_bo(buffers->dst[i], i, width, height, buffers->dummy);
	igt_post_hang_ring(fd, hang);
}

static void do_overwrite_source_read(struct buffers *buffers,
				     do_copy do_copy_func,
				     do_hang do_hang_func,
				     int do_rcs)
{
	const int half = buffers->count/2;
	struct igt_hang_ring hang;
	int i;

	gem_quiescent_gpu(fd);
	for (i = 0; i < half; i++) {
		buffers->mode->set_bo(buffers->src[i], i, width, height);
		buffers->mode->set_bo(buffers->dst[i], ~i, width, height);
		buffers->mode->set_bo(buffers->dst[i+half], ~i, width, height);
	}
	for (i = 0; i < half; i++) {
		do_copy_func(buffers->dst[i], buffers->src[i]);
		if (do_rcs)
			render_copy_bo(buffers->dst[i+half], buffers->src[i]);
		else
			blt_copy_bo(buffers->dst[i+half], buffers->src[i]);
	}
	hang = do_hang_func();
	for (i = half; i--; )
		buffers->mode->set_bo(buffers->src[i], 0xdeadbeef, width, height);
	for (i = 0; i < half; i++) {
		buffers->mode->cmp_bo(buffers->dst[i], i, width, height, buffers->dummy);
		buffers->mode->cmp_bo(buffers->dst[i+half], i, width, height, buffers->dummy);
	}
	igt_post_hang_ring(fd, hang);
}

static void do_overwrite_source_read_bcs(struct buffers *buffers,
					 do_copy do_copy_func,
					 do_hang do_hang_func)
{
	do_overwrite_source_read(buffers, do_copy_func, do_hang_func, 0);
}

static void do_overwrite_source_read_rcs(struct buffers *buffers,
					 do_copy do_copy_func,
					 do_hang do_hang_func)
{
	do_overwrite_source_read(buffers, do_copy_func, do_hang_func, 1);
}

static void do_overwrite_source__rev(struct buffers *buffers,
				     do_copy do_copy_func,
				     do_hang do_hang_func)
{
	struct igt_hang_ring hang;
	int i;

	gem_quiescent_gpu(fd);
	for (i = 0; i < buffers->count; i++) {
		buffers->mode->set_bo(buffers->src[i], i, width, height);
		buffers->mode->set_bo(buffers->dst[i], ~i, width, height);
	}
	for (i = 0; i < buffers->count; i++)
		do_copy_func(buffers->dst[i], buffers->src[i]);
	hang = do_hang_func();
	for (i = 0; i < buffers->count; i++)
		buffers->mode->set_bo(buffers->src[i], 0xdeadbeef, width, height);
	for (i = buffers->count; i--; )
		buffers->mode->cmp_bo(buffers->dst[i], i, width, height, buffers->dummy);
	igt_post_hang_ring(fd, hang);
}

static void do_overwrite_source__one(struct buffers *buffers,
				     do_copy do_copy_func,
				     do_hang do_hang_func)
{
	struct igt_hang_ring hang;

	gem_quiescent_gpu(fd);
	buffers->mode->set_bo(buffers->src[0], 0, width, height);
	buffers->mode->set_bo(buffers->dst[0], ~0, width, height);
	do_copy_func(buffers->dst[0], buffers->src[0]);
	hang = do_hang_func();
	buffers->mode->set_bo(buffers->src[0], 0xdeadbeef, width, height);
	buffers->mode->cmp_bo(buffers->dst[0], 0, width, height, buffers->dummy);
	igt_post_hang_ring(fd, hang);
}

static void do_intermix(struct buffers *buffers,
			do_copy do_copy_func,
			do_hang do_hang_func,
			int do_rcs)
{
	const int half = buffers->count/2;
	struct igt_hang_ring hang;
	int i;

	gem_quiescent_gpu(fd);
	for (i = 0; i < buffers->count; i++) {
		buffers->mode->set_bo(buffers->src[i], 0xdeadbeef^~i, width, height);
		buffers->mode->set_bo(buffers->dst[i], i, width, height);
	}
	for (i = 0; i < half; i++) {
		if (do_rcs == 1 || (do_rcs == -1 && i & 1))
			render_copy_bo(buffers->dst[i], buffers->src[i]);
		else
			blt_copy_bo(buffers->dst[i], buffers->src[i]);

		do_copy_func(buffers->dst[i+half], buffers->src[i]);

		if (do_rcs == 1 || (do_rcs == -1 && (i & 1) == 0))
			render_copy_bo(buffers->dst[i], buffers->dst[i+half]);
		else
			blt_copy_bo(buffers->dst[i], buffers->dst[i+half]);

		do_copy_func(buffers->dst[i+half], buffers->src[i+half]);
	}
	hang = do_hang_func();
	for (i = 0; i < 2*half; i++)
		buffers->mode->cmp_bo(buffers->dst[i], 0xdeadbeef^~i, width, height, buffers->dummy);
	igt_post_hang_ring(fd, hang);
}

static void do_intermix_rcs(struct buffers *buffers,
			    do_copy do_copy_func,
			    do_hang do_hang_func)
{
	do_intermix(buffers, do_copy_func, do_hang_func, 1);
}

static void do_intermix_bcs(struct buffers *buffers,
			    do_copy do_copy_func,
			    do_hang do_hang_func)
{
	do_intermix(buffers, do_copy_func, do_hang_func, 0);
}

static void do_intermix_both(struct buffers *buffers,
			     do_copy do_copy_func,
			     do_hang do_hang_func)
{
	do_intermix(buffers, do_copy_func, do_hang_func, -1);
}

static void do_early_read(struct buffers *buffers,
			  do_copy do_copy_func,
			  do_hang do_hang_func)
{
	struct igt_hang_ring hang;
	int i;

	gem_quiescent_gpu(fd);
	for (i = buffers->count; i--; )
		buffers->mode->set_bo(buffers->src[i], 0xdeadbeef, width, height);
	for (i = 0; i < buffers->count; i++)
		do_copy_func(buffers->dst[i], buffers->src[i]);
	hang = do_hang_func();
	for (i = buffers->count; i--; )
		buffers->mode->cmp_bo(buffers->dst[i], 0xdeadbeef, width, height, buffers->dummy);
	igt_post_hang_ring(fd, hang);
}

static void do_read_read_bcs(struct buffers *buffers,
			     do_copy do_copy_func,
			     do_hang do_hang_func)
{
	struct igt_hang_ring hang;
	int i;

	gem_quiescent_gpu(fd);
	for (i = buffers->count; i--; )
		buffers->mode->set_bo(buffers->src[i], 0xdeadbeef ^ i, width, height);
	for (i = 0; i < buffers->count; i++) {
		do_copy_func(buffers->dst[i], buffers->src[i]);
		blt_copy_bo(buffers->spare, buffers->src[i]);
	}
	cpu_cmp_bo(buffers->spare, 0xdeadbeef^(buffers->count-1), width, height, NULL);
	hang = do_hang_func();
	for (i = buffers->count; i--; )
		buffers->mode->cmp_bo(buffers->dst[i], 0xdeadbeef ^ i, width, height, buffers->dummy);
	igt_post_hang_ring(fd, hang);
}

static void do_write_read_bcs(struct buffers *buffers,
			      do_copy do_copy_func,
			      do_hang do_hang_func)
{
	struct igt_hang_ring hang;
	int i;

	gem_quiescent_gpu(fd);
	for (i = buffers->count; i--; )
		buffers->mode->set_bo(buffers->src[i], 0xdeadbeef ^ i, width, height);
	for (i = 0; i < buffers->count; i++) {
		blt_copy_bo(buffers->spare, buffers->src[i]);
		do_copy_func(buffers->dst[i], buffers->spare);
	}
	hang = do_hang_func();
	for (i = buffers->count; i--; )
		buffers->mode->cmp_bo(buffers->dst[i], 0xdeadbeef ^ i, width, height, buffers->dummy);
	igt_post_hang_ring(fd, hang);
}

static void do_read_read_rcs(struct buffers *buffers,
			     do_copy do_copy_func,
			     do_hang do_hang_func)
{
	struct igt_hang_ring hang;
	int i;

	gem_quiescent_gpu(fd);
	for (i = buffers->count; i--; )
		buffers->mode->set_bo(buffers->src[i], 0xdeadbeef ^ i, width, height);
	for (i = 0; i < buffers->count; i++) {
		do_copy_func(buffers->dst[i], buffers->src[i]);
		render_copy_bo(buffers->spare, buffers->src[i]);
	}
	cpu_cmp_bo(buffers->spare, 0xdeadbeef^(buffers->count-1), width, height, NULL);
	hang = do_hang_func();
	for (i = buffers->count; i--; )
		buffers->mode->cmp_bo(buffers->dst[i], 0xdeadbeef ^ i, width, height, buffers->dummy);
	igt_post_hang_ring(fd, hang);
}

static void do_write_read_rcs(struct buffers *buffers,
			      do_copy do_copy_func,
			      do_hang do_hang_func)
{
	struct igt_hang_ring hang;
	int i;

	gem_quiescent_gpu(fd);
	for (i = buffers->count; i--; )
		buffers->mode->set_bo(buffers->src[i], 0xdeadbeef ^ i, width, height);
	for (i = 0; i < buffers->count; i++) {
		render_copy_bo(buffers->spare, buffers->src[i]);
		do_copy_func(buffers->dst[i], buffers->spare);
	}
	hang = do_hang_func();
	for (i = buffers->count; i--; )
		buffers->mode->cmp_bo(buffers->dst[i], 0xdeadbeef ^ i, width, height, buffers->dummy);
	igt_post_hang_ring(fd, hang);
}

static void do_gpu_read_after_write(struct buffers *buffers,
				    do_copy do_copy_func,
				    do_hang do_hang_func)
{
	struct igt_hang_ring hang;
	int i;

	gem_quiescent_gpu(fd);
	for (i = buffers->count; i--; )
		buffers->mode->set_bo(buffers->src[i], 0xabcdabcd, width, height);
	for (i = 0; i < buffers->count; i++)
		do_copy_func(buffers->dst[i], buffers->src[i]);
	for (i = buffers->count; i--; )
		do_copy_func(buffers->dummy, buffers->dst[i]);
	hang = do_hang_func();
	for (i = buffers->count; i--; )
		buffers->mode->cmp_bo(buffers->dst[i], 0xabcdabcd, width, height, buffers->dummy);
	igt_post_hang_ring(fd, hang);
}

typedef void (*do_test)(struct buffers *buffers,
			do_copy do_copy_func,
			do_hang do_hang_func);

typedef void (*run_wrap)(struct buffers *buffers,
			 do_test do_test_func,
			 do_copy do_copy_func,
			 do_hang do_hang_func);

static void run_single(struct buffers *buffers,
		       do_test do_test_func,
		       do_copy do_copy_func,
		       do_hang do_hang_func)
{
	do_test_func(buffers, do_copy_func, do_hang_func);
	check_gpu();
}

static void run_interruptible(struct buffers *buffers,
			      do_test do_test_func,
			      do_copy do_copy_func,
			      do_hang do_hang_func)
{
	int loop;

	for (loop = 0; loop < 10; loop++)
		do_test_func(buffers, do_copy_func, do_hang_func);
	check_gpu();
}

static void run_forked(struct buffers *buffers,
		       do_test do_test_func,
		       do_copy do_copy_func,
		       do_hang do_hang_func)
{
	const int old_num_buffers = num_buffers;

	num_buffers /= 16;
	num_buffers += 2;

	igt_fork(child, 16) {
		/* recreate process local variables */
		buffers->count = 0;
		fd = drm_open_driver(DRIVER_INTEL);

		batch = buffers_init(buffers, buffers->mode, fd);

		buffers_create(buffers, num_buffers);
		for (int loop = 0; loop < 10; loop++)
			do_test_func(buffers, do_copy_func, do_hang_func);

		buffers_fini(buffers);
	}

	igt_waitchildren();
	check_gpu();

	num_buffers = old_num_buffers;
}

static void bit17_require(void)
{
	struct drm_i915_gem_get_tiling2 {
		uint32_t handle;
		uint32_t tiling_mode;
		uint32_t swizzle_mode;
		uint32_t phys_swizzle_mode;
	} arg;
#define DRM_IOCTL_I915_GEM_GET_TILING2	DRM_IOWR (DRM_COMMAND_BASE + DRM_I915_GEM_GET_TILING, struct drm_i915_gem_get_tiling2)

	memset(&arg, 0, sizeof(arg));
	arg.handle = gem_create(fd, 4096);
	gem_set_tiling(fd, arg.handle, I915_TILING_X, 512);

	do_ioctl(fd, DRM_IOCTL_I915_GEM_GET_TILING2, &arg);
	gem_close(fd, arg.handle);
	igt_require(arg.phys_swizzle_mode == arg.swizzle_mode);
}

static void cpu_require(void)
{
	bit17_require();
}

static void gtt_require(void)
{
}

static void wc_require(void)
{
	bit17_require();
	gem_require_mmap_wc(fd);
}

static void bcs_require(void)
{
}

static void rcs_require(void)
{
	igt_require(rendercopy);
}

static void no_require(void)
{
}

static void
run_basic_modes(const struct access_mode *mode,
		const char *suffix,
		run_wrap run_wrap_func)
{
	const struct {
		const char *prefix;
		do_copy copy;
		void (*require)(void);
	} pipelines[] = {
		{ "cpu", cpu_copy_bo, cpu_require },
		{ "gtt", gtt_copy_bo, gtt_require },
		{ "wc", wc_copy_bo, wc_require },
		{ "blt", blt_copy_bo, bcs_require },
		{ "render", render_copy_bo, rcs_require },
		{ NULL, NULL }
	}, *pskip = pipelines + 3, *p;
	const struct {
		const char *suffix;
		do_hang hang;
		void (*require)(void);
	} hangs[] = {
		{ "", no_hang, no_require },
		{ "-hang-blt", bcs_hang, hang_require },
		{ "-hang-render", rcs_hang, hang_require },
		{ NULL, NULL },
	}, *h;
	struct buffers buffers;

	for (h = hangs; h->suffix; h++) {
		if (!all && *h->suffix)
			continue;

		for (p = all ? pipelines : pskip; p->prefix; p++) {
			igt_fixture {
				batch = buffers_init(&buffers, mode, fd);
			}

			igt_subtest_f("%s-%s-basic%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers, do_basic,
					      p->copy, h->hang);
			}

			/* try to overwrite the source values */
			igt_subtest_f("%s-%s-overwrite-source-one%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers,
					      do_overwrite_source__one,
					      p->copy, h->hang);
			}

			igt_subtest_f("%s-%s-overwrite-source%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers,
					      do_overwrite_source,
					      p->copy, h->hang);
			}

			igt_subtest_f("%s-%s-overwrite-source-read-bcs%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers,
					      do_overwrite_source_read_bcs,
					      p->copy, h->hang);
			}

			igt_subtest_f("%s-%s-overwrite-source-read-rcs%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				igt_require(rendercopy);
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers,
					      do_overwrite_source_read_rcs,
					      p->copy, h->hang);
			}

			igt_subtest_f("%s-%s-overwrite-source-rev%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers,
					      do_overwrite_source__rev,
					      p->copy, h->hang);
			}

			/* try to intermix copies with GPU copies*/
			igt_subtest_f("%s-%s-intermix-rcs%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				igt_require(rendercopy);
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers,
					      do_intermix_rcs,
					      p->copy, h->hang);
			}
			igt_subtest_f("%s-%s-intermix-bcs%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				igt_require(rendercopy);
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers,
					      do_intermix_bcs,
					      p->copy, h->hang);
			}
			igt_subtest_f("%s-%s-intermix-both%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				igt_require(rendercopy);
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers,
					      do_intermix_both,
					      p->copy, h->hang);
			}

			/* try to read the results before the copy completes */
			igt_subtest_f("%s-%s-early-read%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers,
					      do_early_read,
					      p->copy, h->hang);
			}

			/* concurrent reads */
			igt_subtest_f("%s-%s-read-read-bcs%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers,
					      do_read_read_bcs,
					      p->copy, h->hang);
			}
			igt_subtest_f("%s-%s-read-read-rcs%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				igt_require(rendercopy);
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers,
					      do_read_read_rcs,
					      p->copy, h->hang);
			}

			/* split copying between rings */
			igt_subtest_f("%s-%s-write-read-bcs%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers,
					      do_write_read_bcs,
					      p->copy, h->hang);
			}
			igt_subtest_f("%s-%s-write-read-rcs%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				igt_require(rendercopy);
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers,
					      do_write_read_rcs,
					      p->copy, h->hang);
			}

			/* and finally try to trick the kernel into loosing the pending write */
			igt_subtest_f("%s-%s-gpu-read-after-write%s%s", mode->name, p->prefix, suffix, h->suffix) {
				h->require();
				p->require();
				buffers_create(&buffers, num_buffers);
				run_wrap_func(&buffers,
					      do_gpu_read_after_write,
					      p->copy, h->hang);
			}

			igt_fixture {
				buffers_fini(&buffers);
			}
		}
	}
}

static void
run_modes(const struct access_mode *mode)
{
	if (all) {
		run_basic_modes(mode, "", run_single);

		igt_fork_signal_helper();
		run_basic_modes(mode, "-interruptible", run_interruptible);
		igt_stop_signal_helper();
	}

	igt_fork_signal_helper();
	run_basic_modes(mode, "-forked", run_forked);
	igt_stop_signal_helper();
}

igt_main
{
	int max, i;

	igt_skip_on_simulation();

	if (strstr(igt_test_name(), "all"))
		all = true;

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);
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
