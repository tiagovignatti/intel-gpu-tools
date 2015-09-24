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
 */

#include "igt.h"
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <drm.h>

#include "intel_bufmgr.h"
#include "igt_debugfs.h"

#define WIDTH 512
#define STRIDE (WIDTH*4)
#define HEIGHT 512
#define SIZE (HEIGHT*STRIDE)

static bool uses_full_ppgtt(int fd)
{
	struct drm_i915_getparam gp;
	int val = 0;

	memset(&gp, 0, sizeof(gp));
	gp.param = 18; /* HAS_ALIASING_PPGTT */
	gp.value = &val;

	if (drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
		return 0;

	errno = 0;
	return val > 1;
}

static drm_intel_bo *create_bo(drm_intel_bufmgr *bufmgr,
			       uint32_t pixel)
{
	drm_intel_bo *bo;
	uint32_t *v;

	bo = drm_intel_bo_alloc(bufmgr, "surface", SIZE, 4096);
	igt_assert(bo);

	do_or_die(drm_intel_bo_map(bo, 1));
	v = bo->virtual;
	for (int i = 0; i < SIZE/4; i++)
		v[i] = pixel;
	drm_intel_bo_unmap(bo);

	return bo;
}

static void scratch_buf_init(struct igt_buf *buf,
			     drm_intel_bufmgr *bufmgr,
			     uint32_t pixel)
{
	buf->bo = create_bo(bufmgr, pixel);
	buf->stride = STRIDE;
	buf->tiling = I915_TILING_NONE;
	buf->size = SIZE;
}

static void scratch_buf_fini(struct igt_buf *buf)
{
	dri_bo_unreference(buf->bo);
	memset(buf, 0, sizeof(*buf));
}

static void fork_rcs_copy(int target, dri_bo **dst, int count, unsigned flags)
#define CREATE_CONTEXT 0x1
{
	igt_render_copyfunc_t render_copy;
	int devid;

	for (int child = 0; child < count; child++) {
		int fd = drm_open_driver(DRIVER_INTEL);
		drm_intel_bufmgr *bufmgr;

		devid = intel_get_drm_devid(fd);

		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		igt_assert(bufmgr);

		dst[child] = create_bo(bufmgr, ~0);

		if (flags & CREATE_CONTEXT) {
			drm_intel_context *ctx;

			ctx = drm_intel_gem_context_create(dst[child]->bufmgr);
			igt_require(ctx);
		}

		render_copy = igt_get_render_copyfunc(devid);
		igt_require_f(render_copy,
			      "no render-copy function\n");
	}

	igt_fork(child, count) {
		struct intel_batchbuffer *batch;
		struct igt_buf buf;

		batch = intel_batchbuffer_alloc(dst[child]->bufmgr,
						devid);
		igt_assert(batch);

		if (flags & CREATE_CONTEXT) {
			drm_intel_context *ctx;

			ctx = drm_intel_gem_context_create(dst[child]->bufmgr);
			intel_batchbuffer_set_context(batch, ctx);
		}

		buf.bo = dst[child];
		buf.stride = STRIDE;
		buf.tiling = I915_TILING_NONE;
		buf.size = SIZE;

		for (int i = 0; i <= target; i++) {
			struct igt_buf src;

			scratch_buf_init(&src, dst[child]->bufmgr,
					 i | child << 16);

			render_copy(batch, NULL,
				    &src, 0, 0,
				    WIDTH, HEIGHT,
				    &buf, 0, 0);

			scratch_buf_fini(&src);
		}
	}
}

static void fork_bcs_copy(int target, dri_bo **dst, int count)
{
	int devid;

	for (int child = 0; child < count; child++) {
		drm_intel_bufmgr *bufmgr;
		int fd = drm_open_driver(DRIVER_INTEL);

		devid = intel_get_drm_devid(fd);

		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		igt_assert(bufmgr);

		dst[child] = create_bo(bufmgr, ~0);
	}

	igt_fork(child, count) {
		struct intel_batchbuffer *batch;

		batch = intel_batchbuffer_alloc(dst[child]->bufmgr,
						devid);
		igt_assert(batch);

		for (int i = 0; i <= target; i++) {
			dri_bo *src[2];

			src[0] = create_bo(dst[child]->bufmgr,
					   ~0);
			src[1] = create_bo(dst[child]->bufmgr,
					   i | child << 16);

			intel_copy_bo(batch, src[0], src[1], SIZE);
			intel_copy_bo(batch, dst[child], src[0], SIZE);

			dri_bo_unreference(src[1]);
			dri_bo_unreference(src[0]);
		}
	}
}

static void surfaces_check(dri_bo **bo, int count, uint32_t expected)
{
	for (int child = 0; child < count; child++) {
		uint32_t *ptr;

		do_or_die(drm_intel_bo_map(bo[child], 0));
		ptr = bo[child]->virtual;
		for (int j = 0; j < SIZE/4; j++)
			igt_assert_eq(ptr[j], expected | child << 16);
		drm_intel_bo_unmap(bo[child]);
	}
}

static uint64_t exec_and_get_offset(int fd, uint32_t batch)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec[1];
	uint32_t batch_data[2] = { MI_BATCH_BUFFER_END };

	gem_write(fd, batch, 0, batch_data, sizeof(batch_data));

	memset(exec, 0, sizeof(exec));
	exec[0].handle = batch;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)exec;
	execbuf.buffer_count = 1;

	gem_execbuf(fd, &execbuf);
	igt_assert_neq(exec[0].offset, -1);

	return exec[0].offset;
}

static void flink_and_close(void)
{
	uint32_t fd, fd2;
	uint32_t bo, flinked_bo, new_bo, name;
	uint64_t offset, offset_new;

	fd = drm_open_driver(DRIVER_INTEL);
	igt_require(uses_full_ppgtt(fd));

	bo = gem_create(fd, 4096);
	name = gem_flink(fd, bo);

	fd2 = drm_open_driver(DRIVER_INTEL);

	flinked_bo = gem_open(fd2, name);
	offset = exec_and_get_offset(fd2, flinked_bo);
	gem_sync(fd2, flinked_bo);
	gem_close(fd2, flinked_bo);

	/* the flinked bo VMA should have been cleared now, so a new bo of the
	 * same size should get the same offset
	 */
	new_bo = gem_create(fd2, 4096);
	offset_new = exec_and_get_offset(fd2, new_bo);
	gem_close(fd2, new_bo);

	igt_assert_eq(offset, offset_new);

	gem_close(fd, bo);
	close(fd);
	close(fd2);
}

static bool grep_name(const char *fname, const char *match)
{
	int fd;
	FILE *fh;
	size_t n = 0;
	char *line = NULL;
	char *matched = NULL;

	fd = igt_debugfs_open(fname, O_RDONLY);
	igt_assert(fd >= 0);

	fh = fdopen(fd, "r");
	igt_assert(fh);

	while (getline(&line, &n, fh) >= 0) {
		matched = strstr(line, match);
		if (line) {
			free(line);
			line = NULL;
		}
		if (matched)
			break;
	}

	if (line)
		free(line);
	fclose(fh);

	return matched != NULL;
}

static void flink_and_exit(void)
{
	uint32_t fd, fd2;
	uint32_t bo, flinked_bo, name;
	char match[100];
	int to_match;
	bool matched;
	int retry = 0;
	const int retries = 50;

	fd = drm_open_driver(DRIVER_INTEL);
	igt_require(uses_full_ppgtt(fd));

	bo = gem_create(fd, 4096);
	name = gem_flink(fd, bo);

	to_match  = snprintf(match, sizeof(match), "(name: %u)", name);
	igt_assert(to_match < sizeof(match));

	fd2 = drm_open_driver(DRIVER_INTEL);

	flinked_bo = gem_open(fd2, name);
	exec_and_get_offset(fd2, flinked_bo);
	gem_sync(fd2, flinked_bo);

	/* Verify looking for string works OK. */
	matched = grep_name("i915_gem_gtt", match);
	igt_assert_eq(matched, true);

	gem_close(fd2, flinked_bo);

	/* Close the context. */
	close(fd2);

retry:
	/* Give cleanup some time to run. */
	usleep(100000);

	/* The flinked bo VMA should have been cleared now, so list of VMAs
	 * in debugfs should not contain the one for the imported object.
	 */
	matched = grep_name("i915_gem_gtt", match);
	if (matched && retry++ < retries)
		goto retry;

	igt_assert_eq(matched, false);

	gem_close(fd, bo);
	close(fd);
}

#define N_CHILD 8
int main(int argc, char **argv)
{
	igt_subtest_init(argc, argv);

	igt_subtest("blt-vs-render-ctx0") {
		dri_bo *bcs[1], *rcs[N_CHILD];

		fork_bcs_copy(0x4000, bcs, 1);
		fork_rcs_copy(0x8000 / N_CHILD, rcs, N_CHILD, 0);

		igt_waitchildren();

		surfaces_check(bcs, 1, 0x4000);
		surfaces_check(rcs, N_CHILD, 0x8000 / N_CHILD);
	}

	igt_subtest("blt-vs-render-ctxN") {
		dri_bo *bcs[1], *rcs[N_CHILD];

		fork_rcs_copy(0x8000 / N_CHILD, rcs, N_CHILD, CREATE_CONTEXT);
		fork_bcs_copy(0x4000, bcs, 1);

		igt_waitchildren();

		surfaces_check(bcs, 1, 0x4000);
		surfaces_check(rcs, N_CHILD, 0x8000 / N_CHILD);
	}

	igt_subtest("flink-and-close-vma-leak")
		flink_and_close();

	igt_subtest("flink-and-exit-vma-leak")
		flink_and_exit();

	igt_exit();
}
