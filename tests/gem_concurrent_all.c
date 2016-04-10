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
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <drm.h>

#include "intel_bufmgr.h"

IGT_TEST_DESCRIPTION("Test of pread/pwrite/mmap behavior when writing to active"
		     " buffers.");

int fd, devid, gen;
int all;
int pass;

struct create {
	const char *name;
	void (*require)(const struct create *, unsigned);
	drm_intel_bo *(*create)(drm_intel_bufmgr *, uint64_t size);
};

struct size {
	const char *name;
	int width, height;
};

struct buffers {
	const char *name;
	const struct create *create;
	const struct access_mode *mode;
	const struct size *size;
	drm_intel_bufmgr *bufmgr;
	struct intel_batchbuffer *batch;
	drm_intel_bo **src, **dst;
	drm_intel_bo *snoop, *spare;
	uint32_t *tmp;
	int width, height, npixels;
	int count, num_buffers;
};

#define MIN_BUFFERS 3

static void blt_copy_bo(struct buffers *b, drm_intel_bo *dst, drm_intel_bo *src);

static void
nop_release_bo(drm_intel_bo *bo)
{
	drm_intel_bo_unreference(bo);
}

static void
prw_set_bo(struct buffers *b, drm_intel_bo *bo, uint32_t val)
{
	for (int i = 0; i < b->npixels; i++)
		b->tmp[i] = val;
	drm_intel_bo_subdata(bo, 0, 4*b->npixels, b->tmp);
}

static void
prw_cmp_bo(struct buffers *b, drm_intel_bo *bo, uint32_t val)
{
	uint32_t *vaddr;

	vaddr = b->tmp;
	do_or_die(drm_intel_bo_get_subdata(bo, 0, 4*b->npixels, vaddr));
	for (int i = 0; i < b->npixels; i++)
		igt_assert_eq_u32(vaddr[i], val);
}

#define pixel(y, width) ((y)*(width) + (((y) + pass)%(width)))

static void
partial_set_bo(struct buffers *b, drm_intel_bo *bo, uint32_t val)
{
	for (int y = 0; y < b->height; y++)
		do_or_die(drm_intel_bo_subdata(bo, 4*pixel(y, b->width), 4, &val));
}

static void
partial_cmp_bo(struct buffers *b, drm_intel_bo *bo, uint32_t val)
{
	for (int y = 0; y < b->height; y++) {
		uint32_t buf;
		do_or_die(drm_intel_bo_get_subdata(bo, 4*pixel(y, b->width), 4, &buf));
		igt_assert_eq_u32(buf, val);
	}
}

static drm_intel_bo *
create_normal_bo(drm_intel_bufmgr *bufmgr, uint64_t size)
{
	drm_intel_bo *bo;

	bo = drm_intel_bo_alloc(bufmgr, "bo", size, 0);
	igt_assert(bo);

	return bo;
}

static void can_create_normal(const struct create *create, unsigned count)
{
}

#if HAVE_CREATE_PRIVATE
static drm_intel_bo *
create_private_bo(drm_intel_bufmgr *bufmgr, uint64_t size)
{
	drm_intel_bo *bo;
	uint32_t handle;

	/* XXX gem_create_with_flags(fd, size, I915_CREATE_PRIVATE); */

	handle = gem_create(fd, size);
	bo = gem_handle_to_libdrm_bo(bufmgr, fd, "stolen", handle);
	gem_close(fd, handle);

	return bo;
}

static void can_create_private(const struct create *create, unsigned count)
{
	igt_require(0);
}
#endif

#if HAVE_CREATE_STOLEN
static drm_intel_bo *
create_stolen_bo(drm_intel_bufmgr *bufmgr, uint64_t size)
{
	drm_intel_bo *bo;
	uint32_t handle;

	/* XXX gem_create_with_flags(fd, size, I915_CREATE_STOLEN); */

	handle = gem_create(fd, size);
	bo = gem_handle_to_libdrm_bo(bufmgr, fd, "stolen", handle);
	gem_close(fd, handle);

	return bo;
}

static void can_create_stolen(const struct create *create, unsigned count)
{
	/* XXX check num_buffers against available stolen */
	igt_require(0);
}
#endif

static void create_cpu_require(const struct create *create, unsigned count)
{
#if HAVE_CREATE_STOLEN
	igt_require(create->create != create_stolen_bo);
#endif
}

static drm_intel_bo *
unmapped_create_bo(const struct buffers *b)
{
	return b->create->create(b->bufmgr, 4*b->npixels);
}

static void create_snoop_require(const struct create *create, unsigned count)
{
	create_cpu_require(create, count);
	igt_require(!gem_has_llc(fd));
}

static drm_intel_bo *
snoop_create_bo(const struct buffers *b)
{
	drm_intel_bo *bo;

	bo = unmapped_create_bo(b);
	gem_set_caching(fd, bo->handle, I915_CACHING_CACHED);
	drm_intel_bo_disable_reuse(bo);

	return bo;
}

static void create_userptr_require(const struct create *create, unsigned count)
{
	static int has_userptr = -1;
	if (has_userptr < 0) {
		struct drm_i915_gem_userptr arg;

		has_userptr = 0;

		memset(&arg, 0, sizeof(arg));
		arg.user_ptr = -4096ULL;
		arg.user_size = 8192;
		errno = 0;
		drmIoctl(fd, LOCAL_IOCTL_I915_GEM_USERPTR, &arg);
		if (errno == EFAULT) {
			igt_assert(posix_memalign((void **)&arg.user_ptr,
						  4096, arg.user_size) == 0);
			has_userptr = drmIoctl(fd,
					 LOCAL_IOCTL_I915_GEM_USERPTR,
					 &arg) == 0;
			free((void *)(uintptr_t)arg.user_ptr);
		}

	}
	igt_require(has_userptr);
}

static drm_intel_bo *
userptr_create_bo(const struct buffers *b)
{
	struct local_i915_gem_userptr userptr;
	drm_intel_bo *bo;
	void *ptr;

	memset(&userptr, 0, sizeof(userptr));
	userptr.user_size = b->npixels * 4;
	userptr.user_size = (userptr.user_size + 4095) & -4096;

	ptr = mmap(NULL, userptr.user_size,
		   PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
	igt_assert(ptr != (void *)-1);
	userptr.user_ptr = (uintptr_t)ptr;

#if 0
	do_or_die(drmIoctl(fd, LOCAL_IOCTL_I915_GEM_USERPTR, &userptr));
	bo = gem_handle_to_libdrm_bo(b->bufmgr, fd, "userptr", userptr.handle);
	gem_close(fd, userptr.handle);
#else
	bo = drm_intel_bo_alloc_userptr(b->bufmgr, "name",
					ptr, I915_TILING_NONE, 0,
					userptr.user_size, 0);
	igt_assert(bo);
#endif
	bo->virtual = (void *)(uintptr_t)userptr.user_ptr;

	return bo;
}

static void
userptr_set_bo(struct buffers *b, drm_intel_bo *bo, uint32_t val)
{
	int size = b->npixels;
	uint32_t *vaddr = bo->virtual;

	gem_set_domain(fd, bo->handle,
		       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
	while (size--)
		*vaddr++ = val;
}

static void
userptr_cmp_bo(struct buffers *b, drm_intel_bo *bo, uint32_t val)
{
	int size =  b->npixels;
	uint32_t *vaddr = bo->virtual;

	gem_set_domain(fd, bo->handle,
		       I915_GEM_DOMAIN_CPU, 0);
	while (size--)
		igt_assert_eq_u32(*vaddr++, val);
}

static void
userptr_release_bo(drm_intel_bo *bo)
{
	igt_assert(bo->virtual);

	munmap(bo->virtual, bo->size);
	bo->virtual = NULL;

	drm_intel_bo_unreference(bo);
}

static void create_dmabuf_require(const struct create *create, unsigned count)
{
	static int has_dmabuf = -1;
	if (has_dmabuf < 0) {
		struct drm_prime_handle args;
		void *ptr;

		memset(&args, 0, sizeof(args));
		args.handle = gem_create(fd, 4096);
		args.flags = DRM_RDWR;
		args.fd = -1;

		drmIoctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);
		gem_close(fd, args.handle);

		has_dmabuf = 0;
		ptr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, args.fd, 0);
		if (ptr != MAP_FAILED) {
			has_dmabuf = 1;
			munmap(ptr, 4096);
		}

		close(args.fd);
	}
	igt_require(has_dmabuf);
	intel_require_files(2*count);
}

struct dmabuf {
	int fd;
	void *map;
};

static drm_intel_bo *
dmabuf_create_bo(const struct buffers *b)
{
	struct drm_prime_handle args;
	drm_intel_bo *bo;
	struct dmabuf *dmabuf;
	int size;

	size = 4*b->npixels;
	size = (size + 4095) & -4096;

	memset(&args, 0, sizeof(args));
	args.handle = gem_create(fd, size);
	args.flags = DRM_RDWR;
	args.fd = -1;

	do_ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);
	gem_close(fd, args.handle);

	bo = drm_intel_bo_gem_create_from_prime(b->bufmgr, args.fd, size);
	igt_assert(bo);

	dmabuf = malloc(sizeof(*dmabuf));
	igt_assert(dmabuf);

	dmabuf->fd = args.fd;
	dmabuf->map = mmap(NULL, size,
			   PROT_READ | PROT_WRITE, MAP_SHARED,
			   dmabuf->fd, 0);
	igt_assert(dmabuf->map != (void *)-1);

	bo->virtual = dmabuf;

	return bo;
}

static void
dmabuf_set_bo(struct buffers *b, drm_intel_bo *bo, uint32_t val)
{
	struct dmabuf *dmabuf = bo->virtual;
	uint32_t *v;
	int size;

	prime_sync_start(dmabuf->fd, true);
	for (v = dmabuf->map, size = b->npixels; size--; v++)
		*v = val;
	prime_sync_end(dmabuf->fd, true);
}

static void
dmabuf_cmp_bo(struct buffers *b, drm_intel_bo *bo, uint32_t val)
{
	struct dmabuf *dmabuf = bo->virtual;
	uint32_t *v;
	int size;

	prime_sync_start(dmabuf->fd, false);
	for (v = dmabuf->map, size = b->npixels; size--; v++)
		igt_assert_eq_u32(*v, val);
	prime_sync_end(dmabuf->fd, false);
}

static void
dmabuf_release_bo(drm_intel_bo *bo)
{
	struct dmabuf *dmabuf = bo->virtual;
	igt_assert(dmabuf);

	munmap(dmabuf->map, bo->size);
	close(dmabuf->fd);
	free(dmabuf);

	bo->virtual = NULL;
	drm_intel_bo_unreference(bo);
}

static void
gtt_set_bo(struct buffers *b, drm_intel_bo *bo, uint32_t val)
{
	uint32_t *vaddr = bo->virtual;
	int size = b->npixels;

	drm_intel_gem_bo_start_gtt_access(bo, true);
	while (size--)
		*vaddr++ = val;
}

static void
gtt_cmp_bo(struct buffers *b, drm_intel_bo *bo, uint32_t val)
{
	uint32_t *vaddr = bo->virtual;

	/* GTT access is slow. So we just compare a few points */
	drm_intel_gem_bo_start_gtt_access(bo, false);
	for (int y = 0; y < b->height; y++)
		igt_assert_eq_u32(vaddr[pixel(y, b->width)], val);
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
gtt_create_bo(const struct buffers *b)
{
	return map_bo(unmapped_create_bo(b));
}

static drm_intel_bo *
gttX_create_bo(const struct buffers *b)
{
	return tile_bo(gtt_create_bo(b), b->width);
}

static void bit17_require(void)
{
	static struct drm_i915_gem_get_tiling2 {
		uint32_t handle;
		uint32_t tiling_mode;
		uint32_t swizzle_mode;
		uint32_t phys_swizzle_mode;
	} arg;
#define DRM_IOCTL_I915_GEM_GET_TILING2	DRM_IOWR (DRM_COMMAND_BASE + DRM_I915_GEM_GET_TILING, struct drm_i915_gem_get_tiling2)

	if (arg.handle == 0) {
		arg.handle = gem_create(fd, 4096);
		gem_set_tiling(fd, arg.handle, I915_TILING_X, 512);

		do_ioctl(fd, DRM_IOCTL_I915_GEM_GET_TILING2, &arg);
		gem_close(fd, arg.handle);
	}
	igt_require(arg.phys_swizzle_mode == arg.swizzle_mode);
}

static void wc_require(void)
{
	bit17_require();
	gem_require_mmap_wc(fd);
}

static void
wc_create_require(const struct create *create, unsigned count)
{
	wc_require();
}

static drm_intel_bo *
wc_create_bo(const struct buffers *b)
{
	drm_intel_bo *bo;

	bo = unmapped_create_bo(b);
	bo->virtual = gem_mmap__wc(fd, bo->handle, 0, bo->size, PROT_READ | PROT_WRITE);
	return bo;
}

static void
wc_release_bo(drm_intel_bo *bo)
{
	igt_assert(bo->virtual);

	munmap(bo->virtual, bo->size);
	bo->virtual = NULL;

	nop_release_bo(bo);
}

static drm_intel_bo *
gpu_create_bo(const struct buffers *b)
{
	return unmapped_create_bo(b);
}

static drm_intel_bo *
gpuX_create_bo(const struct buffers *b)
{
	return tile_bo(gpu_create_bo(b), b->width);
}

static void
cpu_set_bo(struct buffers *b, drm_intel_bo *bo, uint32_t val)
{
	int size = b->npixels;
	uint32_t *vaddr;

	do_or_die(drm_intel_bo_map(bo, true));
	vaddr = bo->virtual;
	while (size--)
		*vaddr++ = val;
	drm_intel_bo_unmap(bo);
}

static void
cpu_cmp_bo(struct buffers *b, drm_intel_bo *bo, uint32_t val)
{
	int size = b->npixels;
	uint32_t *vaddr;

	do_or_die(drm_intel_bo_map(bo, false));
	vaddr = bo->virtual;
	while (size--)
		igt_assert_eq_u32(*vaddr++, val);
	drm_intel_bo_unmap(bo);
}

static void
gpu_set_bo(struct buffers *buffers, drm_intel_bo *bo, uint32_t val)
{
	struct drm_i915_gem_relocation_entry reloc[1];
	struct drm_i915_gem_exec_object2 gem_exec[2];
	struct drm_i915_gem_execbuffer2 execbuf;
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
		*b = buffers->width;
	} else
		*b = buffers->width << 2;
	*b++ |= 0xf0 << 16 | 1 << 25 | 1 << 24;
	*b++ = 0;
	*b++ = buffers->height << 16 | buffers->width;
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

	gem_exec[1].handle = gem_create(fd, 4096);
	gem_exec[1].relocation_count = 1;
	gem_exec[1].relocs_ptr = (uintptr_t)reloc;

	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 2;
	execbuf.batch_len = (b - buf) * sizeof(buf[0]);
	if (gen >= 6)
		execbuf.flags = I915_EXEC_BLT;

	gem_write(fd, gem_exec[1].handle, 0, buf, execbuf.batch_len);
	gem_execbuf(fd, &execbuf);

	gem_close(fd, gem_exec[1].handle);
}

static void
gpu_cmp_bo(struct buffers *b, drm_intel_bo *bo, uint32_t val)
{
	blt_copy_bo(b, b->snoop, bo);
	cpu_cmp_bo(b, b->snoop, val);
}

struct access_mode {
	const char *name;
	void (*require)(const struct create *, unsigned);
	drm_intel_bo *(*create_bo)(const struct buffers *b);
	void (*set_bo)(struct buffers *b, drm_intel_bo *bo, uint32_t val);
	void (*cmp_bo)(struct buffers *b, drm_intel_bo *bo, uint32_t val);
	void (*release_bo)(drm_intel_bo *bo);
};
igt_render_copyfunc_t rendercopy;

static int read_sysctl(const char *path)
{
	FILE *file = fopen(path, "r");
	int max = 0;
	if (file) {
		if (fscanf(file, "%d", &max) != 1)
			max = 0; /* silence! */
		fclose(file);
	}
	return max;
}

static int write_sysctl(const char *path, int value)
{
	FILE *file = fopen(path, "w");
	if (file) {
		fprintf(file, "%d", value);
		fclose(file);
	}
	return read_sysctl(path);
}

static bool set_max_map_count(int num_buffers)
{
	int max = read_sysctl("/proc/sys/vm/max_map_count");
	if (max < num_buffers + 1024)
		max = write_sysctl("/proc/sys/vm/max_map_count",
				   num_buffers + 1024);
	return max > num_buffers;
}

static void buffers_init(struct buffers *b,
			 const char *name,
			 const struct create *create,
			 const struct access_mode *mode,
			 const struct size *size,
			 int num_buffers,
			 int _fd, int enable_reuse)
{
	memset(b, 0, sizeof(*b));
	b->name = name;
	b->create = create;
	b->mode = mode;
	b->size = size;
	b->num_buffers = num_buffers;
	b->count = 0;

	b->width = size->width;
	b->height = size->height;
	b->npixels = size->width * size->height;
	b->tmp = malloc(4*b->npixels);
	igt_assert(b->tmp);

	b->bufmgr = drm_intel_bufmgr_gem_init(_fd, 4096);
	igt_assert(b->bufmgr);

	b->src = malloc(2*sizeof(drm_intel_bo *)*num_buffers);
	igt_assert(b->src);
	b->dst = b->src + num_buffers;

	if (enable_reuse)
		drm_intel_bufmgr_gem_enable_reuse(b->bufmgr);
	b->batch = intel_batchbuffer_alloc(b->bufmgr, devid);
	igt_assert(b->batch);
}

static void buffers_destroy(struct buffers *b)
{
	int count = b->count;
	if (count == 0)
		return;

	/* Be safe so that we can clean up a partial creation */
	b->count = 0;
	for (int i = 0; i < count; i++) {
		if (b->src[i]) {
			b->mode->release_bo(b->src[i]);
			b->src[i] = NULL;
		} else
			break;

		if (b->dst[i]) {
			b->mode->release_bo(b->dst[i]);
			b->dst[i] = NULL;
		}
	}
	if (b->snoop) {
		nop_release_bo(b->snoop);
		b->snoop = NULL;
	}
	if (b->spare) {
		b->mode->release_bo(b->spare);
		b->spare = NULL;
	}
}

static void buffers_create(struct buffers *b)
{
	int count = b->num_buffers;
	igt_assert(b->bufmgr);

	buffers_destroy(b);
	igt_assert(b->count == 0);
	b->count = count;

	for (int i = 0; i < count; i++) {
		b->src[i] = b->mode->create_bo(b);
		b->dst[i] = b->mode->create_bo(b);
	}
	b->spare = b->mode->create_bo(b);
	b->snoop = snoop_create_bo(b);
}

static void buffers_reset(struct buffers *b, bool enable_reuse)
{
	buffers_destroy(b);

	igt_assert(b->count == 0);
	igt_assert(b->tmp);
	igt_assert(b->src);
	igt_assert(b->dst);

	intel_batchbuffer_free(b->batch);
	drm_intel_bufmgr_destroy(b->bufmgr);

	b->bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	igt_assert(b->bufmgr);

	if (enable_reuse)
		drm_intel_bufmgr_gem_enable_reuse(b->bufmgr);
	b->batch = intel_batchbuffer_alloc(b->bufmgr, devid);
	igt_assert(b->batch);
}

static void buffers_fini(struct buffers *b)
{
	if (b->bufmgr == NULL)
		return;

	buffers_destroy(b);

	free(b->tmp);
	free(b->src);

	intel_batchbuffer_free(b->batch);
	drm_intel_bufmgr_destroy(b->bufmgr);

	memset(b, 0, sizeof(*b));
}

typedef void (*do_copy)(struct buffers *b, drm_intel_bo *dst, drm_intel_bo *src);
typedef struct igt_hang_ring (*do_hang)(void);

static void render_copy_bo(struct buffers *b, drm_intel_bo *dst, drm_intel_bo *src)
{
	struct igt_buf d = {
		.bo = dst,
		.size = b->npixels * 4,
		.num_tiles = b->npixels * 4,
		.stride = b->width * 4,
	}, s = {
		.bo = src,
		.size = b->npixels * 4,
		.num_tiles = b->npixels * 4,
		.stride = b->width * 4,
	};
	uint32_t swizzle;

	drm_intel_bo_get_tiling(dst, &d.tiling, &swizzle);
	drm_intel_bo_get_tiling(src, &s.tiling, &swizzle);

	rendercopy(b->batch, NULL,
		   &s, 0, 0,
		   b->width, b->height,
		   &d, 0, 0);
}

static void blt_copy_bo(struct buffers *b, drm_intel_bo *dst, drm_intel_bo *src)
{
	intel_blt_copy(b->batch,
		       src, 0, 0, 4*b->width,
		       dst, 0, 0, 4*b->width,
		       b->width, b->height, 32);
}

static void cpu_copy_bo(struct buffers *b, drm_intel_bo *dst, drm_intel_bo *src)
{
	const int size = b->npixels * sizeof(uint32_t);
	void *d, *s;

	gem_set_domain(fd, src->handle, I915_GEM_DOMAIN_CPU, 0);
	gem_set_domain(fd, dst->handle, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
	s = gem_mmap__cpu(fd, src->handle, 0, size, PROT_READ);
	d = gem_mmap__cpu(fd, dst->handle, 0, size, PROT_WRITE);

	memcpy(d, s, size);

	munmap(d, size);
	munmap(s, size);
}

static void gtt_copy_bo(struct buffers *b, drm_intel_bo *dst, drm_intel_bo *src)
{
	const int size = b->npixels * sizeof(uint32_t);
	void *d, *s;

	gem_set_domain(fd, src->handle, I915_GEM_DOMAIN_GTT, 0);
	gem_set_domain(fd, dst->handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);

	s = gem_mmap__gtt(fd, src->handle, size, PROT_READ);
	d = gem_mmap__gtt(fd, dst->handle, size, PROT_WRITE);

	memcpy(d, s, size);

	munmap(d, size);
	munmap(s, size);
}

static void wc_copy_bo(struct buffers *b, drm_intel_bo *dst, drm_intel_bo *src)
{
	const int size = b->width * sizeof(uint32_t);
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

static struct igt_hang_ring all_hang(void)
{
	uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	struct igt_hang_ring hang;
	unsigned engine;

	memset(&obj, 0, sizeof(obj));
	obj.handle = gem_create(fd, 4096);
	gem_write(fd, obj.handle, 0, &bbe, sizeof(&bbe));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&obj;
	execbuf.buffer_count = 1;

	for_each_engine(fd, engine) {
		hang = igt_hang_ring(fd, engine);

		execbuf.flags = engine;
		__gem_execbuf(fd, &execbuf);

		gem_close(fd, hang.handle);
	}

	hang.handle = obj.handle;
	return hang;
}

static void do_basic0(struct buffers *buffers,
		      do_copy do_copy_func,
		      do_hang do_hang_func)
{
	gem_quiescent_gpu(fd);

	buffers->mode->set_bo(buffers, buffers->src[0], 0xdeadbeef);
	for (int i = 0; i < buffers->count; i++) {
		struct igt_hang_ring hang = do_hang_func();

		do_copy_func(buffers, buffers->dst[i], buffers->src[0]);
		buffers->mode->cmp_bo(buffers, buffers->dst[i], 0xdeadbeef);

		igt_post_hang_ring(fd, hang);
	}
}

static void do_basic1(struct buffers *buffers,
		      do_copy do_copy_func,
		      do_hang do_hang_func)
{
	gem_quiescent_gpu(fd);

	for (int i = 0; i < buffers->count; i++) {
		struct igt_hang_ring hang = do_hang_func();

		buffers->mode->set_bo(buffers, buffers->src[i], i);
		buffers->mode->set_bo(buffers, buffers->dst[i], ~i);

		do_copy_func(buffers, buffers->dst[i], buffers->src[i]);
		usleep(0); /* let someone else claim the mutex */
		buffers->mode->cmp_bo(buffers, buffers->dst[i], i);

		igt_post_hang_ring(fd, hang);
	}
}

static void do_basicN(struct buffers *buffers,
		      do_copy do_copy_func,
		      do_hang do_hang_func)
{
	struct igt_hang_ring hang;

	gem_quiescent_gpu(fd);

	for (int i = 0; i < buffers->count; i++) {
		buffers->mode->set_bo(buffers, buffers->src[i], i);
		buffers->mode->set_bo(buffers, buffers->dst[i], ~i);
	}

	hang = do_hang_func();

	for (int i = 0; i < buffers->count; i++) {
		do_copy_func(buffers, buffers->dst[i], buffers->src[i]);
		usleep(0); /* let someone else claim the mutex */
	}

	for (int i = 0; i < buffers->count; i++)
		buffers->mode->cmp_bo(buffers, buffers->dst[i], i);

	igt_post_hang_ring(fd, hang);
}

static void do_overwrite_source(struct buffers *buffers,
				do_copy do_copy_func,
				do_hang do_hang_func)
{
	struct igt_hang_ring hang;
	int i;

	gem_quiescent_gpu(fd);
	for (i = 0; i < buffers->count; i++) {
		buffers->mode->set_bo(buffers, buffers->src[i], i);
		buffers->mode->set_bo(buffers, buffers->dst[i], ~i);
	}
	for (i = 0; i < buffers->count; i++)
		do_copy_func(buffers, buffers->dst[i], buffers->src[i]);
	hang = do_hang_func();
	for (i = buffers->count; i--; )
		buffers->mode->set_bo(buffers, buffers->src[i], 0xdeadbeef);
	for (i = 0; i < buffers->count; i++)
		buffers->mode->cmp_bo(buffers, buffers->dst[i], i);
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
		buffers->mode->set_bo(buffers, buffers->src[i], i);
		buffers->mode->set_bo(buffers, buffers->dst[i], ~i);
		buffers->mode->set_bo(buffers, buffers->dst[i+half], ~i);
	}
	for (i = 0; i < half; i++) {
		do_copy_func(buffers, buffers->dst[i], buffers->src[i]);
		if (do_rcs)
			render_copy_bo(buffers, buffers->dst[i+half], buffers->src[i]);
		else
			blt_copy_bo(buffers, buffers->dst[i+half], buffers->src[i]);
	}
	hang = do_hang_func();
	for (i = half; i--; )
		buffers->mode->set_bo(buffers, buffers->src[i], 0xdeadbeef);
	for (i = 0; i < half; i++) {
		buffers->mode->cmp_bo(buffers, buffers->dst[i], i);
		buffers->mode->cmp_bo(buffers, buffers->dst[i+half], i);
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
		buffers->mode->set_bo(buffers, buffers->src[i], i);
		buffers->mode->set_bo(buffers, buffers->dst[i], ~i);
	}
	for (i = 0; i < buffers->count; i++)
		do_copy_func(buffers, buffers->dst[i], buffers->src[i]);
	hang = do_hang_func();
	for (i = 0; i < buffers->count; i++)
		buffers->mode->set_bo(buffers, buffers->src[i], 0xdeadbeef);
	for (i = buffers->count; i--; )
		buffers->mode->cmp_bo(buffers, buffers->dst[i], i);
	igt_post_hang_ring(fd, hang);
}

static void do_overwrite_source__one(struct buffers *buffers,
				     do_copy do_copy_func,
				     do_hang do_hang_func)
{
	struct igt_hang_ring hang;

	gem_quiescent_gpu(fd);
	buffers->mode->set_bo(buffers, buffers->src[0], 0);
	buffers->mode->set_bo(buffers, buffers->dst[0], ~0);
	do_copy_func(buffers, buffers->dst[0], buffers->src[0]);
	hang = do_hang_func();
	buffers->mode->set_bo(buffers, buffers->src[0], 0xdeadbeef);
	buffers->mode->cmp_bo(buffers, buffers->dst[0], 0);
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
		buffers->mode->set_bo(buffers, buffers->src[i], 0xdeadbeef^~i);
		buffers->mode->set_bo(buffers, buffers->dst[i], i);
	}
	for (i = 0; i < half; i++) {
		if (do_rcs == 1 || (do_rcs == -1 && i & 1))
			render_copy_bo(buffers, buffers->dst[i], buffers->src[i]);
		else
			blt_copy_bo(buffers, buffers->dst[i], buffers->src[i]);

		do_copy_func(buffers, buffers->dst[i+half], buffers->src[i]);

		if (do_rcs == 1 || (do_rcs == -1 && (i & 1) == 0))
			render_copy_bo(buffers, buffers->dst[i], buffers->dst[i+half]);
		else
			blt_copy_bo(buffers, buffers->dst[i], buffers->dst[i+half]);

		do_copy_func(buffers, buffers->dst[i+half], buffers->src[i+half]);
	}
	hang = do_hang_func();
	for (i = 0; i < 2*half; i++)
		buffers->mode->cmp_bo(buffers, buffers->dst[i], 0xdeadbeef^~i);
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
		buffers->mode->set_bo(buffers, buffers->src[i], 0xdeadbeef);
	for (i = 0; i < buffers->count; i++)
		do_copy_func(buffers, buffers->dst[i], buffers->src[i]);
	hang = do_hang_func();
	for (i = buffers->count; i--; )
		buffers->mode->cmp_bo(buffers, buffers->dst[i], 0xdeadbeef);
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
		buffers->mode->set_bo(buffers, buffers->src[i], 0xdeadbeef ^ i);
	for (i = 0; i < buffers->count; i++) {
		do_copy_func(buffers, buffers->dst[i], buffers->src[i]);
		blt_copy_bo(buffers, buffers->spare, buffers->src[i]);
	}
	buffers->mode->cmp_bo(buffers, buffers->spare, 0xdeadbeef^(buffers->count-1));
	hang = do_hang_func();
	for (i = buffers->count; i--; )
		buffers->mode->cmp_bo(buffers, buffers->dst[i], 0xdeadbeef ^ i);
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
		buffers->mode->set_bo(buffers, buffers->src[i], 0xdeadbeef ^ i);
	for (i = 0; i < buffers->count; i++) {
		blt_copy_bo(buffers, buffers->spare, buffers->src[i]);
		do_copy_func(buffers, buffers->dst[i], buffers->spare);
	}
	hang = do_hang_func();
	for (i = buffers->count; i--; )
		buffers->mode->cmp_bo(buffers, buffers->dst[i], 0xdeadbeef ^ i);
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
		buffers->mode->set_bo(buffers, buffers->src[i], 0xdeadbeef ^ i);
	for (i = 0; i < buffers->count; i++) {
		do_copy_func(buffers, buffers->dst[i], buffers->src[i]);
		render_copy_bo(buffers, buffers->spare, buffers->src[i]);
	}
	buffers->mode->cmp_bo(buffers, buffers->spare, 0xdeadbeef^(buffers->count-1));
	hang = do_hang_func();
	for (i = buffers->count; i--; )
		buffers->mode->cmp_bo(buffers, buffers->dst[i], 0xdeadbeef ^ i);
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
		buffers->mode->set_bo(buffers, buffers->src[i], 0xdeadbeef ^ i);
	for (i = 0; i < buffers->count; i++) {
		render_copy_bo(buffers, buffers->spare, buffers->src[i]);
		do_copy_func(buffers, buffers->dst[i], buffers->spare);
	}
	hang = do_hang_func();
	for (i = buffers->count; i--; )
		buffers->mode->cmp_bo(buffers, buffers->dst[i], 0xdeadbeef ^ i);
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
		buffers->mode->set_bo(buffers, buffers->src[i], 0xabcdabcd);
	for (i = 0; i < buffers->count; i++)
		do_copy_func(buffers, buffers->dst[i], buffers->src[i]);
	for (i = buffers->count; i--; )
		do_copy_func(buffers, buffers->spare, buffers->dst[i]);
	hang = do_hang_func();
	for (i = buffers->count; i--; )
		buffers->mode->cmp_bo(buffers, buffers->dst[i], 0xabcdabcd);
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
	pass = 0;
	do_test_func(buffers, do_copy_func, do_hang_func);
	igt_assert_eq(intel_detect_and_clear_missed_interrupts(fd), 0);
}

static void run_interruptible(struct buffers *buffers,
			      do_test do_test_func,
			      do_copy do_copy_func,
			      do_hang do_hang_func)
{
	pass = 0;
	igt_interruptible(true)
		do_test_func(buffers, do_copy_func, do_hang_func);
	igt_assert_eq(intel_detect_and_clear_missed_interrupts(fd), 0);
}

static void run_child(struct buffers *buffers,
		      do_test do_test_func,
		      do_copy do_copy_func,
		      do_hang do_hang_func)

{
	/* We inherit the buffers from the parent, but the bufmgr/batch
	 * needs to be local as the cache of reusable itself will be COWed,
	 * leading to the child closing an object without the parent knowing.
	 */
	pass = 0;
	igt_fork(child, 1)
		do_test_func(buffers, do_copy_func, do_hang_func);
	igt_waitchildren();
	igt_assert_eq(intel_detect_and_clear_missed_interrupts(fd), 0);
}

static void __run_forked(struct buffers *buffers,
			 int num_children, int loops, bool interrupt,
			 do_test do_test_func,
			 do_copy do_copy_func,
			 do_hang do_hang_func)

{
	/* purge the libdrm caches before cloing the process */
	buffers_reset(buffers, true);

	igt_fork(child, num_children) {
		/* recreate process local variables */
		fd = drm_open_driver(DRIVER_INTEL);

		buffers->num_buffers /= num_children;
		buffers->num_buffers += MIN_BUFFERS;

		buffers_reset(buffers, true);
		buffers_create(buffers);

		igt_interruptible(interrupt) {
			for (pass = 0; pass < loops; pass++)
				do_test_func(buffers,
					     do_copy_func,
					     do_hang_func);
		}
	}
	igt_waitchildren();
	igt_assert_eq(intel_detect_and_clear_missed_interrupts(fd), 0);
}

static void run_forked(struct buffers *buffers,
		       do_test do_test_func,
		       do_copy do_copy_func,
		       do_hang do_hang_func)
{
	const int ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	__run_forked(buffers, ncpus, ncpus, false,
		     do_test_func, do_copy_func, do_hang_func);
}

static void run_bomb(struct buffers *buffers,
		     do_test do_test_func,
		     do_copy do_copy_func,
		     do_hang do_hang_func)
{
	const int ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	__run_forked(buffers, 8*ncpus, 2, true,
		     do_test_func, do_copy_func, do_hang_func);
}

static void cpu_require(void)
{
	bit17_require();
}

static void gtt_require(void)
{
}

static void bcs_require(void)
{
}

static void rcs_require(void)
{
	igt_require(rendercopy);
}

static void
run_mode(const char *prefix,
	 const struct create *create,
	 const struct access_mode *mode,
	 const struct size *size,
	 const int num_buffers,
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
	} hangs[] = {
		{ "", no_hang },
		{ "-hang-blt", bcs_hang },
		{ "-hang-render", rcs_hang },
		{ "-hang-all", all_hang },
		{ NULL, NULL },
	}, *h;
	struct buffers buffers;

	igt_fixture
		buffers_init(&buffers, prefix, create, mode,
			     size, num_buffers,
			     fd, run_wrap_func != run_child);

	for (h = hangs; h->suffix; h++) {
		if (!all && *h->suffix)
			continue;

		if (!*h->suffix)
			igt_fork_hang_detector(fd);

		for (p = all ? pipelines : pskip; p->prefix; p++) {
			igt_fixture p->require();

			igt_subtest_f("%s-%s-%s-sanitycheck0%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				buffers_create(&buffers);
				run_wrap_func(&buffers, do_basic0,
					      p->copy, h->hang);
			}

			igt_subtest_f("%s-%s-%s-sanitycheck1%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				buffers_create(&buffers);
				run_wrap_func(&buffers, do_basic1,
					      p->copy, h->hang);
			}

			igt_subtest_f("%s-%s-%s-sanitycheckN%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				buffers_create(&buffers);
				run_wrap_func(&buffers, do_basicN,
					      p->copy, h->hang);
			}

			/* try to overwrite the source values */
			igt_subtest_f("%s-%s-%s-overwrite-source-one%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				buffers_create(&buffers);
				run_wrap_func(&buffers,
					      do_overwrite_source__one,
					      p->copy, h->hang);
			}

			igt_subtest_f("%s-%s-%s-overwrite-source%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				buffers_create(&buffers);
				run_wrap_func(&buffers,
					      do_overwrite_source,
					      p->copy, h->hang);
			}

			igt_subtest_f("%s-%s-%s-overwrite-source-read-bcs%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				buffers_create(&buffers);
				run_wrap_func(&buffers,
					      do_overwrite_source_read_bcs,
					      p->copy, h->hang);
			}

			igt_subtest_f("%s-%s-%s-overwrite-source-read-rcs%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				igt_require(rendercopy);
				buffers_create(&buffers);
				run_wrap_func(&buffers,
					      do_overwrite_source_read_rcs,
					      p->copy, h->hang);
			}

			igt_subtest_f("%s-%s-%s-overwrite-source-rev%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				buffers_create(&buffers);
				run_wrap_func(&buffers,
					      do_overwrite_source__rev,
					      p->copy, h->hang);
			}

			/* try to intermix copies with GPU copies*/
			igt_subtest_f("%s-%s-%s-intermix-rcs%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				igt_require(rendercopy);
				buffers_create(&buffers);
				run_wrap_func(&buffers,
					      do_intermix_rcs,
					      p->copy, h->hang);
			}
			igt_subtest_f("%s-%s-%s-intermix-bcs%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				igt_require(rendercopy);
				buffers_create(&buffers);
				run_wrap_func(&buffers,
					      do_intermix_bcs,
					      p->copy, h->hang);
			}
			igt_subtest_f("%s-%s-%s-intermix-both%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				igt_require(rendercopy);
				buffers_create(&buffers);
				run_wrap_func(&buffers,
					      do_intermix_both,
					      p->copy, h->hang);
			}

			/* try to read the results before the copy completes */
			igt_subtest_f("%s-%s-%s-early-read%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				buffers_create(&buffers);
				run_wrap_func(&buffers,
					      do_early_read,
					      p->copy, h->hang);
			}

			/* concurrent reads */
			igt_subtest_f("%s-%s-%s-read-read-bcs%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				buffers_create(&buffers);
				run_wrap_func(&buffers,
					      do_read_read_bcs,
					      p->copy, h->hang);
			}
			igt_subtest_f("%s-%s-%s-read-read-rcs%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				igt_require(rendercopy);
				buffers_create(&buffers);
				run_wrap_func(&buffers,
					      do_read_read_rcs,
					      p->copy, h->hang);
			}

			/* split copying between rings */
			igt_subtest_f("%s-%s-%s-write-read-bcs%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				buffers_create(&buffers);
				run_wrap_func(&buffers,
					      do_write_read_bcs,
					      p->copy, h->hang);
			}
			igt_subtest_f("%s-%s-%s-write-read-rcs%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				igt_require(rendercopy);
				buffers_create(&buffers);
				run_wrap_func(&buffers,
					      do_write_read_rcs,
					      p->copy, h->hang);
			}

			/* and finally try to trick the kernel into loosing the pending write */
			igt_subtest_f("%s-%s-%s-gpu-read-after-write%s%s", prefix, mode->name, p->prefix, suffix, h->suffix) {
				buffers_create(&buffers);
				run_wrap_func(&buffers,
					      do_gpu_read_after_write,
					      p->copy, h->hang);
			}
		}

		if (!*h->suffix)
			igt_stop_hang_detector();
	}

	igt_fixture
		buffers_fini(&buffers);
}

static void
run_modes(const char *style,
	  const struct create *create,
	  const struct access_mode *mode,
	  const struct size *size,
	  const int num)
{
	const struct wrap {
		const char *suffix;
		run_wrap func;
	} wrappers[] = {
		{ "", run_single },
		{ "-child", run_child },
		{ "-forked", run_forked },
		{ "-interruptible", run_interruptible },
		{ "-bomb", run_bomb },
		{ NULL },
	};

	while (mode->name) {
		igt_subtest_group {
			igt_fixture {
				if (mode->require)
					mode->require(create, num);
			}

			for (const struct wrap *w = wrappers; w->suffix; w++) {
				run_mode(style, create, mode, size, num,
					 w->suffix, w->func);
			}
		}

		mode++;
	}
}

static unsigned
num_buffers(uint64_t max,
	    const struct size *s,
	    const struct create *c,
	    unsigned allow_mem)
{
	unsigned size = 4*s->width*s->height;
	unsigned n;

	if (max == 0)
		n = MIN_BUFFERS;
	else
		n = max / size;

	igt_require(n);
	igt_require(set_max_map_count(2*n));

	if (c->require)
		c->require(c, n);

	intel_require_memory(2*n, size, allow_mem);

	return n;
}

static bool allow_unlimited_files(void)
{
	struct rlimit rlim;
	unsigned nofile_rlim = 1024*1024;

	FILE *file = fopen("/proc/sys/fs/file-max", "r");
	if (file) {
		igt_assert(fscanf(file, "%u", &nofile_rlim) == 1);
		igt_info("System limit for open files is %u\n", nofile_rlim);
		fclose(file);
	}

	if (getrlimit(RLIMIT_NOFILE, &rlim))
		return false;

	rlim.rlim_cur = nofile_rlim;
	rlim.rlim_max = nofile_rlim;
	return setrlimit(RLIMIT_NOFILE, &rlim) == 0;
}

igt_main
{
	const struct access_mode modes[] = {
		{
			.name = "prw",
			.create_bo = unmapped_create_bo,
			.set_bo = prw_set_bo,
			.cmp_bo = prw_cmp_bo,
			.release_bo = nop_release_bo,
		},
		{
			.name = "partial",
			.create_bo = unmapped_create_bo,
			.set_bo = partial_set_bo,
			.cmp_bo = partial_cmp_bo,
			.release_bo = nop_release_bo,
		},
		{
			.name = "cpu",
			.create_bo = unmapped_create_bo,
			.require = create_cpu_require,
			.set_bo = cpu_set_bo,
			.cmp_bo = cpu_cmp_bo,
			.release_bo = nop_release_bo,
		},
		{
			.name = "snoop",
			.create_bo = snoop_create_bo,
			.require = create_snoop_require,
			.set_bo = cpu_set_bo,
			.cmp_bo = cpu_cmp_bo,
			.release_bo = nop_release_bo,
		},
		{
			.name = "userptr",
			.create_bo = userptr_create_bo,
			.require = create_userptr_require,
			.set_bo = userptr_set_bo,
			.cmp_bo = userptr_cmp_bo,
			.release_bo = userptr_release_bo,
		},
		{
			.name = "dmabuf",
			.create_bo = dmabuf_create_bo,
			.require = create_dmabuf_require,
			.set_bo = dmabuf_set_bo,
			.cmp_bo = dmabuf_cmp_bo,
			.release_bo = dmabuf_release_bo,
		},
		{
			.name = "gtt",
			.create_bo = gtt_create_bo,
			.set_bo = gtt_set_bo,
			.cmp_bo = gtt_cmp_bo,
			.release_bo = nop_release_bo,
		},
		{
			.name = "gttX",
			.create_bo = gttX_create_bo,
			.set_bo = gtt_set_bo,
			.cmp_bo = gtt_cmp_bo,
			.release_bo = nop_release_bo,
		},
		{
			.name = "wc",
			.require = wc_create_require,
			.create_bo = wc_create_bo,
			.set_bo = gtt_set_bo,
			.cmp_bo = gtt_cmp_bo,
			.release_bo = wc_release_bo,
		},
		{
			.name = "gpu",
			.create_bo = gpu_create_bo,
			.set_bo = gpu_set_bo,
			.cmp_bo = gpu_cmp_bo,
			.release_bo = nop_release_bo,
		},
		{
			.name = "gpuX",
			.create_bo = gpuX_create_bo,
			.set_bo = gpu_set_bo,
			.cmp_bo = gpu_cmp_bo,
			.release_bo = nop_release_bo,
		},
		{ NULL },
	};
	const struct create create[] = {
		{ "", can_create_normal, create_normal_bo},
#if HAVE_CREATE_PRIVATE
		{ "private-", can_create_private, create_private_bo},
#endif
#if HAVE_CREATE_STOLEN
		{ "stolen-", can_create_stolen, create_stolen_bo},
#endif
		{ NULL, NULL }
	};
	const struct size sizes[] = {
		{ "4KiB", 128, 8 },
		{ "256KiB", 128, 128 },
		{ "1MiB", 512, 512 },
		{ "16MiB", 2048, 2048 },
		{ NULL}
	};
	uint64_t pin_sz = 0;
	void *pinned = NULL;
	char name[80];
	int count = 0;

	igt_skip_on_simulation();

	if (strstr(igt_test_name(), "all"))
		all = true;

	igt_fixture {
		allow_unlimited_files();

		fd = drm_open_driver(DRIVER_INTEL);
		intel_detect_and_clear_missed_interrupts(fd);
		devid = intel_get_drm_devid(fd);
		gen = intel_gen(devid);
		rendercopy = igt_get_render_copyfunc(devid);
	}

	for (const struct create *c = create; c->name; c++) {
		for (const struct size *s = sizes; s->name; s++) {
			/* Minimum test set */
			snprintf(name, sizeof(name), "%s%s-%s",
				 c->name, s->name, "tiny");
			igt_subtest_group {
				igt_fixture {
					count = num_buffers(0, s, c, CHECK_RAM);
				}
				run_modes(name, c, modes, s, count);
			}

			/* "Average" test set */
			snprintf(name, sizeof(name), "%s%s-%s",
				 c->name, s->name, "small");
			igt_subtest_group {
				igt_fixture {
					count = num_buffers(gem_mappable_aperture_size()/4,
							    s, c, CHECK_RAM);
				}
				run_modes(name, c, modes, s, count);
			}

			/* Use the entire mappable aperture */
			snprintf(name, sizeof(name), "%s%s-%s",
				 c->name, s->name, "thrash");
			igt_subtest_group {
				igt_fixture {
					count = num_buffers(gem_mappable_aperture_size(),
							    s, c, CHECK_RAM);
				}
				run_modes(name, c, modes, s, count);
			}

			/* Use the entire global GTT */
			snprintf(name, sizeof(name), "%s%s-%s",
				 c->name, s->name, "global");
			igt_subtest_group {
				igt_fixture {
					count = num_buffers(gem_global_aperture_size(fd),
							    s, c, CHECK_RAM);
				}
				run_modes(name, c, modes, s, count);
			}

			/* Use the entire per-process GTT */
			snprintf(name, sizeof(name), "%s%s-%s",
				 c->name, s->name, "full");
			igt_subtest_group {
				igt_fixture {
					count = num_buffers(gem_aperture_size(fd),
							    s, c, CHECK_RAM);
				}
				run_modes(name, c, modes, s, count);
			}

			/* Use the entire mappable aperture, force swapping */
			snprintf(name, sizeof(name), "%s%s-%s",
				 c->name, s->name, "swap");
			igt_subtest_group {
				igt_fixture {
					if (intel_get_avail_ram_mb() > gem_mappable_aperture_size()/(1024*1024)) {
						pin_sz = intel_get_avail_ram_mb() - gem_mappable_aperture_size()/(1024*1024);

						igt_debug("Pinning %lld MiB\n", (long long)pin_sz);
						pin_sz *= 1024 * 1024;

						if (posix_memalign(&pinned, 4096, pin_sz) ||
						    mlock(pinned, pin_sz) ||
						    madvise(pinned, pin_sz, MADV_DONTFORK)) {
							free(pinned);
							pinned = NULL;
						}
						igt_require(pinned);
					}

					count = num_buffers(gem_mappable_aperture_size(),
							    s, c, CHECK_RAM | CHECK_SWAP);
				}
				run_modes(name, c, modes, s, count);

				igt_fixture {
					if (pinned) {
						munlock(pinned, pin_sz);
						free(pinned);
						pinned = NULL;
					}
				}
			}
		}
	}
}
