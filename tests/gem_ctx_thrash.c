/*
 * Copyright © 2014 Intel Corporation
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
 */

#include "igt.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/resource.h>


IGT_TEST_DESCRIPTION("Fill the Gobal GTT with context objects and VMs\n");

#define OBJECT_SIZE (1024 * 1024)
#define NUM_THREADS 8

static int fd;
static unsigned devid;
static igt_render_copyfunc_t render_copy;

static dri_bo **all_bo;
static int num_bo;
static int bo_per_ctx;

static drm_intel_context **all_ctx;
static int num_ctx;
static int ctx_per_thread;

static void xchg_ptr(void *array, unsigned i, unsigned j)
{
	void **A = array;
	igt_swap(A[i], A[j]);
}

static void xchg_int(void *array, unsigned i, unsigned j)
{
	int *A = array;
	igt_swap(A[i], A[j]);
}

static int reopen(int _fd)
{
	struct stat st;
	char name[128];

	igt_assert(fstat(_fd, &st) == 0);

	sprintf(name, "/dev/dri/card%u", (unsigned)(st.st_rdev & 0x7f));
	return open(name, O_RDWR);
}

static void *thread(void *bufmgr)
{
	struct intel_batchbuffer *batch;
	dri_bo **bo;
	drm_intel_context **ctx;
	int c, b;

	batch = intel_batchbuffer_alloc(bufmgr, devid);

	bo = malloc(num_bo * sizeof(dri_bo *));
	igt_assert(bo);
	memcpy(bo, all_bo, num_bo * sizeof(dri_bo *));

	ctx = malloc(num_ctx * sizeof(drm_intel_context *));
	igt_assert(ctx);
	memcpy(ctx, all_ctx, num_ctx * sizeof(drm_intel_context *));
	igt_permute_array(ctx, num_ctx, xchg_ptr);

	for (c = 0; c < ctx_per_thread; c++) {
		igt_permute_array(bo, num_bo, xchg_ptr);
		for (b = 0; b < bo_per_ctx; b++) {
			struct igt_buf src, dst;

			src.bo = bo[b % num_bo];
			src.stride = 64;
			src.size = OBJECT_SIZE;
			src.tiling = I915_TILING_NONE;

			dst.bo = bo[(b+1) % num_bo];
			dst.stride = 64;
			dst.size = OBJECT_SIZE;
			dst.tiling = I915_TILING_NONE;

			render_copy(batch, ctx[c % num_ctx],
				    &src, 0, 0, 16, 16, &dst, 0, 0);
		}
	}

	free(ctx);
	free(bo);
	intel_batchbuffer_free(batch);

	return NULL;
}

static int uses_ppgtt(int _fd)
{
	struct drm_i915_getparam gp;
	int val = 0;

	memset(&gp, 0, sizeof(gp));
	gp.param = 18; /* HAS_ALIASING_PPGTT */
	gp.value = &val;

	if (drmIoctl(_fd, DRM_IOCTL_I915_GETPARAM, &gp))
		return 0;

	errno = 0;
	return val;
}

static void
processes(void)
{
	int *all_fds;
	uint64_t aperture;
	struct rlimit rlim;
	int ppgtt_mode;
	int ctx_size;
	int obj_size;
	int n;

	igt_skip_on_simulation();

	fd = drm_open_driver_render(DRIVER_INTEL);
	devid = intel_get_drm_devid(fd);
	aperture = gem_aperture_size(fd);

	ppgtt_mode = uses_ppgtt(fd);
	igt_require(ppgtt_mode);

	render_copy = igt_get_render_copyfunc(devid);
	igt_require_f(render_copy, "no render-copy function\n");

	if (ppgtt_mode > 1)
		ctx_size = aperture >> 10; /* Assume full-ppgtt of maximum size */
	else
		ctx_size = 64 << 10; /* Most gen require at least 64k for ctx */
	num_ctx = 3 * (aperture / ctx_size) / 2;
	igt_info("Creating %d contexts (assuming of size %d)\n",
		 num_ctx, ctx_size);
	intel_require_memory(num_ctx, ctx_size, CHECK_RAM | CHECK_SWAP);

	/* tweak rlimits to allow us to create this many files */
	igt_assert(getrlimit(RLIMIT_NOFILE, &rlim) == 0);
	if (rlim.rlim_cur < ALIGN(num_ctx + 1024, 1024)) {
		rlim.rlim_cur = ALIGN(num_ctx + 1024, 1024);
		if (rlim.rlim_cur > rlim.rlim_max)
			rlim.rlim_max = rlim.rlim_cur;
		igt_assert(setrlimit(RLIMIT_NOFILE, &rlim) == 0);
	}

	all_fds = malloc(num_ctx * sizeof(int));
	igt_assert(all_fds);
	for (n = 0; n < num_ctx; n++) {
		all_fds[n] = reopen(fd);
		if (all_fds[n] == -1) {
			int err = errno;
			for (int i = n; i--; )
				close(all_fds[i]);
			free(all_fds);
			errno = err;
			igt_assert_f(0, "failed to create context %d/%d\n", n, num_ctx);
		}
	}

	num_bo = 2 * num_ctx;
	obj_size = (2 * aperture / num_bo + 4095) & -4096;
	igt_info("Creating %d surfaces (of size %d)\n", num_bo, obj_size);
	intel_require_memory(num_bo, obj_size, CHECK_RAM);

	igt_fork(child, NUM_THREADS) {
		drm_intel_bufmgr *bufmgr;
		struct intel_batchbuffer *batch;
		int c;

		igt_permute_array(all_fds, num_ctx, xchg_int);

		for (c = 0; c < num_ctx; c++) {
			struct igt_buf src, dst;

			bufmgr = drm_intel_bufmgr_gem_init(all_fds[c], 4096);
			igt_assert(bufmgr);
			batch = intel_batchbuffer_alloc(bufmgr, devid);

			src.bo = drm_intel_bo_alloc(bufmgr, "", obj_size, 0);
			igt_assert(src.bo);
			src.stride = 64;
			src.size = obj_size;
			src.tiling = I915_TILING_NONE;

			dst.bo = drm_intel_bo_alloc(bufmgr, "", obj_size, 0);
			igt_assert(dst.bo);
			dst.stride = 64;
			dst.size = obj_size;
			dst.tiling = I915_TILING_NONE;

			render_copy(batch, NULL,
				    &src, 0, 0, 16, 16, &dst, 0, 0);

			intel_batchbuffer_free(batch);
			drm_intel_bo_unreference(src.bo);
			drm_intel_bo_unreference(dst.bo);
			drm_intel_bufmgr_destroy(bufmgr);
		}
	}
	igt_waitchildren();

	for (n = 0; n < num_ctx; n++)
		close(all_fds[n]);
	free(all_fds);
	close(fd);
}

static void
threads(void)
{
	pthread_t threads[NUM_THREADS];
	drm_intel_bufmgr *bufmgr;
	uint64_t aperture;
	int ppgtt_mode;
	int ctx_size;
	int n;

	igt_skip_on_simulation();

	fd = drm_open_driver_render(DRIVER_INTEL);
	devid = intel_get_drm_devid(fd);
	aperture = gem_aperture_size(fd);

	ppgtt_mode = uses_ppgtt(fd);
	igt_require(ppgtt_mode);

	render_copy = igt_get_render_copyfunc(devid);
	igt_require_f(render_copy, "no render-copy function\n");

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	igt_assert(bufmgr);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	if (ppgtt_mode > 1)
		ctx_size = aperture >> 10; /* Assume full-ppgtt of maximum size */
	else
		ctx_size = 64 << 10; /* Most gen require at least 64k for ctx */
	num_ctx = 3 * (aperture / ctx_size) / 2;
	igt_info("Creating %d contexts (assuming of size %d)\n",
		 num_ctx, ctx_size);
	intel_require_memory(num_ctx, ctx_size, CHECK_RAM | CHECK_SWAP);
	all_ctx = malloc(num_ctx * sizeof(drm_intel_context *));
	igt_assert(all_ctx);
	for (n = 0; n < num_ctx; n++) {
		all_ctx[n] = drm_intel_gem_context_create(bufmgr);
		igt_assert(all_ctx[n]);
	}

	num_bo = 3 * (aperture / OBJECT_SIZE) / 2;
	igt_info("Creating %d surfaces (of size %d)\n", num_bo, OBJECT_SIZE);
	intel_require_memory(num_bo, OBJECT_SIZE, CHECK_RAM);
	all_bo = malloc(num_bo * sizeof(dri_bo *));
	igt_assert(all_bo);
	for (n = 0; n < num_bo; n++) {
		all_bo[n] = drm_intel_bo_alloc(bufmgr, "", OBJECT_SIZE, 0);
		igt_assert(all_bo[n]);
	}

	ctx_per_thread = 3 * num_ctx / NUM_THREADS / 2;
	bo_per_ctx = 3 * num_bo / NUM_THREADS / 2;

	for (n = 0; n < NUM_THREADS; n++)
		pthread_create(&threads[n], NULL, thread, bufmgr);

	for (n = 0; n < NUM_THREADS; n++)
		pthread_join(threads[n], NULL);

	drm_intel_bufmgr_destroy(bufmgr);
	close(fd);
}

igt_main
{
	igt_subtest("processes")
		processes();

	igt_subtest("threads")
		threads();
}
