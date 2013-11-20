/*
 * Copyright (c) 2013 Intel Corporation
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
 *  Mika Kuoppala <mika.kuoppala@intel.com>
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>

#include "i915_drm.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_gpu_tools.h"
#include "rendercopy.h"

#define RS_NO_ERROR      0
#define RS_BATCH_ACTIVE  (1 << 0)
#define RS_BATCH_PENDING (1 << 1)
#define RS_UNKNOWN       (1 << 2)

struct local_drm_i915_reset_stats {
	__u32 ctx_id;
	__u32 flags;
	__u32 reset_count;
	__u32 batch_active;
	__u32 batch_pending;
	__u32 pad;
};

struct local_drm_i915_gem_context_create {
	__u32 ctx_id;
	__u32 pad;
};

struct local_drm_i915_gem_context_destroy {
	__u32 ctx_id;
	__u32 pad;
};

#define MAX_FD 32

#define CONTEXT_CREATE_IOCTL DRM_IOWR(DRM_COMMAND_BASE + 0x2d, struct local_drm_i915_gem_context_create)
#define CONTEXT_DESTROY_IOCTL DRM_IOWR(DRM_COMMAND_BASE + 0x2e, struct local_drm_i915_gem_context_destroy)
#define GET_RESET_STATS_IOCTL DRM_IOWR(DRM_COMMAND_BASE + 0x32, struct local_drm_i915_reset_stats)

static uint32_t context_create(int fd)
{
	struct local_drm_i915_gem_context_create create;
	int ret;

	create.ctx_id = rand();
	create.pad = rand();

	ret = drmIoctl(fd, CONTEXT_CREATE_IOCTL, &create);
	igt_assert(ret == 0);

	return create.ctx_id;
}

static int context_destroy(int fd, uint32_t ctx_id)
{
	int ret;
	struct local_drm_i915_gem_context_destroy destroy;

	destroy.ctx_id = ctx_id;
	destroy.pad = rand();

	ret = drmIoctl(fd, CONTEXT_DESTROY_IOCTL, &destroy);
	if (ret != 0)
		return -errno;

	return 0;
}

static int gem_reset_stats(int fd, int ctx_id,
			   struct local_drm_i915_reset_stats *rs)
{
	int ret;

	rs->ctx_id = ctx_id;
	rs->flags = 0;
	rs->reset_count = rand();
	rs->batch_active = rand();
	rs->batch_pending = rand();
	rs->pad = 0;

	do {
		ret = ioctl(fd, GET_RESET_STATS_IOCTL, rs);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));

	if (ret < 0)
		return -errno;

	return 0;
}

static int gem_reset_status(int fd, int ctx_id)
{
	int ret;
	struct local_drm_i915_reset_stats rs;

	ret = gem_reset_stats(fd, ctx_id, &rs);
	if (ret)
		return ret;

	if (rs.batch_active)
		return RS_BATCH_ACTIVE;
	if (rs.batch_pending)
		return RS_BATCH_PENDING;

	return RS_NO_ERROR;
}

static int gem_exec(int fd, struct drm_i915_gem_execbuffer2 *execbuf)
{
	int ret;

	ret = ioctl(fd,
		    DRM_IOCTL_I915_GEM_EXECBUFFER2,
		    execbuf);

	if (ret < 0)
		return -errno;

	return 0;
}

static int exec_valid(int fd, int ctx)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec;
	int ret;

	uint32_t buf[2] = { MI_BATCH_BUFFER_END, 0 };

	exec.handle = gem_create(fd, 4096);
	gem_write(fd, exec.handle, 0, buf, sizeof(buf));
	exec.relocation_count = 0;
	exec.relocs_ptr = 0;
	exec.alignment = 0;
	exec.offset = 0;
	exec.flags = 0;
	exec.rsvd1 = 0;
	exec.rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)&exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = sizeof(buf);
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = 0;
	i915_execbuffer2_set_context_id(execbuf, ctx);
	execbuf.rsvd2 = 0;

	ret = gem_exec(fd, &execbuf);
	if (ret < 0)
		return ret;

	return exec.handle;
}

#define BUFSIZE (4 * 1024)
#define ITEMS   (BUFSIZE >> 2)

static int inject_hang(int fd, int ctx)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec;
	uint64_t gtt_off;
	uint32_t *buf;
	int roff, i;
	unsigned cmd_len = 2;

	srandom(time(NULL));

	if (intel_gen(intel_get_drm_devid(fd)) >= 8)
		cmd_len = 3;

	buf = malloc(BUFSIZE);
	igt_assert(buf != NULL);

	buf[0] = MI_BATCH_BUFFER_END;
	buf[1] = MI_NOOP;

	exec.handle = gem_create(fd, BUFSIZE);
	gem_write(fd, exec.handle, 0, buf, BUFSIZE);
	exec.relocation_count = 0;
	exec.relocs_ptr = 0;
	exec.alignment = 0;
	exec.offset = 0;
	exec.flags = 0;
	exec.rsvd1 = 0;
	exec.rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)&exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = BUFSIZE;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = 0;
	i915_execbuffer2_set_context_id(execbuf, ctx);
	execbuf.rsvd2 = 0;

	igt_assert(gem_exec(fd, &execbuf) == 0);

	gtt_off = exec.offset;

	for (i = 0; i < ITEMS; i++)
		buf[i] = MI_NOOP;

	roff = random() % (ITEMS - cmd_len);
	buf[roff] = MI_BATCH_BUFFER_START | (cmd_len - 2);
	buf[roff + 1] = (gtt_off & 0xfffffffc) + (roff << 2);
	if (cmd_len == 3)
		buf[roff + 2] = gtt_off & 0xffffffff00000000ull;

#ifdef VERBOSE
	printf("loop injected at 0x%lx (off 0x%x, bo_start 0x%lx, bo_end 0x%lx)\n",
	       (long unsigned int)((roff << 2) + gtt_off),
	       roff << 2, (long unsigned int)gtt_off,
	       (long unsigned int)(gtt_off + BUFSIZE - 1));
#endif
	gem_write(fd, exec.handle, 0, buf, BUFSIZE);

	exec.relocation_count = 0;
	exec.relocs_ptr = 0;
	exec.alignment = 0;
	exec.offset = 0;
	exec.flags = 0;
	exec.rsvd1 = 0;
	exec.rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)&exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = BUFSIZE;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = 0;
	i915_execbuffer2_set_context_id(execbuf, ctx);
	execbuf.rsvd2 = 0;

	igt_assert(gem_exec(fd, &execbuf) == 0);

	igt_assert(gtt_off == exec.offset);

	free(buf);

	return exec.handle;
}

static int _assert_reset_status(int fd, int ctx, int status)
{
	int rs;

	rs = gem_reset_status(fd, ctx);
	if (rs < 0) {
		printf("reset status for %d ctx %d returned %d\n",
		       fd, ctx, rs);
		return rs;
	}

	if (rs != status) {
		printf("%d:%d reset status %d differs from assumed %d\n",
		       fd, ctx, rs, status);

		return 1;
	}

	return 0;
}

#define assert_reset_status(fd, ctx, status) \
	igt_assert(_assert_reset_status(fd, ctx, status) == 0)

static void test_rs(int num_fds, int hang_index, int rs_assumed_no_hang)
{
	int i;
	int fd[MAX_FD];
	int h[MAX_FD];

	igt_assert (num_fds <= MAX_FD);
	igt_assert (hang_index < MAX_FD);

	for (i = 0; i < num_fds; i++) {
		fd[i] = drm_open_any();
		igt_assert(fd[i]);
	}

	for (i = 0; i < num_fds; i++)
		assert_reset_status(fd[i], 0, RS_NO_ERROR);

	for (i = 0; i < num_fds; i++) {
		if (i == hang_index)
			h[i] = inject_hang(fd[i], 0);
		else
			h[i] = exec_valid(fd[i], 0);
	}

	gem_sync(fd[num_fds - 1], h[num_fds - 1]);

	for (i = 0; i < num_fds; i++) {
		if (hang_index < 0) {
			assert_reset_status(fd[i], 0, rs_assumed_no_hang);
			continue;
		}

		if (i < hang_index)
			assert_reset_status(fd[i], 0, RS_NO_ERROR);
		if (i == hang_index)
			assert_reset_status(fd[i], 0, RS_BATCH_ACTIVE);
		if (i > hang_index)
			assert_reset_status(fd[i], 0, RS_BATCH_PENDING);
	}

	for (i = 0; i < num_fds; i++) {
		gem_close(fd[i], h[i]);
		close(fd[i]);
	}
}

#define MAX_CTX 100
static void test_rs_ctx(int num_fds, int num_ctx, int hang_index,
			int hang_context)
{
	int i, j;
	int fd[MAX_FD];
	int h[MAX_FD][MAX_CTX];
	int ctx[MAX_FD][MAX_CTX];

	igt_assert (num_fds <= MAX_FD);
	igt_assert (hang_index < MAX_FD);

	igt_assert (num_ctx <= MAX_CTX);
	igt_assert (hang_context < MAX_CTX);

	test_rs(num_fds, -1, RS_NO_ERROR);

	for (i = 0; i < num_fds; i++) {
		fd[i] = drm_open_any();
		igt_assert(fd[i]);
		assert_reset_status(fd[i], 0, RS_NO_ERROR);

		for (j = 0; j < num_ctx; j++) {
			ctx[i][j] = context_create(fd[i]);

		}

		assert_reset_status(fd[i], 0, RS_NO_ERROR);
	}

	for (i = 0; i < num_fds; i++) {

		assert_reset_status(fd[i], 0, RS_NO_ERROR);

		for (j = 0; j < num_ctx; j++)
			assert_reset_status(fd[i], ctx[i][j], RS_NO_ERROR);

		assert_reset_status(fd[i], 0, RS_NO_ERROR);
	}

	for (i = 0; i < num_fds; i++) {
		for (j = 0; j < num_ctx; j++) {
			if (i == hang_index && j == hang_context)
				h[i][j] = inject_hang(fd[i], ctx[i][j]);
			else
				h[i][j] = exec_valid(fd[i], ctx[i][j]);
		}
	}

	gem_sync(fd[num_fds - 1], ctx[num_fds - 1][num_ctx - 1]);

	for (i = 0; i < num_fds; i++)
		assert_reset_status(fd[i], 0, RS_NO_ERROR);

	for (i = 0; i < num_fds; i++) {
		for (j = 0; j < num_ctx; j++) {
			if (i < hang_index)
				assert_reset_status(fd[i], ctx[i][j], RS_NO_ERROR);
			if (i == hang_index && j < hang_context)
				assert_reset_status(fd[i], ctx[i][j], RS_NO_ERROR);
			if (i == hang_index && j == hang_context)
				assert_reset_status(fd[i], ctx[i][j],
						    RS_BATCH_ACTIVE);
			if (i == hang_index && j > hang_context)
				assert_reset_status(fd[i], ctx[i][j],
						    RS_BATCH_PENDING);
			if (i > hang_index)
				assert_reset_status(fd[i], ctx[i][j],
						    RS_BATCH_PENDING);
		}
	}

	for (i = 0; i < num_fds; i++) {
		for (j = 0; j < num_ctx; j++) {
			gem_close(fd[i], h[i][j]);
			igt_assert(context_destroy(fd[i], ctx[i][j]) == 0);
		}

		assert_reset_status(fd[i], 0, RS_NO_ERROR);

		close(fd[i]);
	}
}

static void test_ban(void)
{
	int h1,h2,h3,h4,h5,h6,h7;
	int ctx_good, ctx_bad;
	int fd;
	int retry = 10;
	int active_count = 0, pending_count = 0;
	struct local_drm_i915_reset_stats rs_bad, rs_good;

	fd = drm_open_any();
	igt_assert(fd >= 0);

	assert_reset_status(fd, 0, RS_NO_ERROR);

	ctx_good = context_create(fd);
	ctx_bad = context_create(fd);

	assert_reset_status(fd, 0, RS_NO_ERROR);
	assert_reset_status(fd, ctx_good, RS_NO_ERROR);
	assert_reset_status(fd, ctx_bad, RS_NO_ERROR);

	h1 = exec_valid(fd, ctx_bad);
	igt_assert(h1 >= 0);
	h5 = exec_valid(fd, ctx_good);
	igt_assert(h5 >= 0);

	assert_reset_status(fd, ctx_good, RS_NO_ERROR);
	assert_reset_status(fd, ctx_bad, RS_NO_ERROR);

	h2 = inject_hang(fd, ctx_bad);
	igt_assert(h2 >= 0);
	active_count++;
	/* Second hang will be pending for this */
	pending_count++;

	h6 = exec_valid(fd, ctx_good);
	h7 = exec_valid(fd, ctx_good);

        while (retry--) {
                h3 = inject_hang(fd, ctx_bad);
                igt_assert(h3 >= 0);
                gem_sync(fd, h3);
		active_count++;
		/* This second hand will count as pending */
                assert_reset_status(fd, ctx_bad, RS_BATCH_ACTIVE);

                h4 = exec_valid(fd, ctx_bad);
                if (h4 == -EIO) {
                        gem_close(fd, h3);
                        break;
                }

                /* Should not happen often but sometimes hang is declared too slow
                 * due to our way of faking hang using loop */

                igt_assert(h4 >= 0);
                gem_close(fd, h3);
                gem_close(fd, h4);

                printf("retrying for ban (%d)\n", retry);
        }

	igt_assert(h4 == -EIO);
	assert_reset_status(fd, ctx_bad, RS_BATCH_ACTIVE);

	gem_sync(fd, h7);
	assert_reset_status(fd, ctx_good, RS_BATCH_PENDING);

	igt_assert(gem_reset_stats(fd, ctx_good, &rs_good) == 0);
	igt_assert(gem_reset_stats(fd, ctx_bad, &rs_bad) == 0);

	igt_assert(rs_bad.batch_active == active_count);
	igt_assert(rs_bad.batch_pending == pending_count);
	igt_assert(rs_good.batch_active == 0);
	igt_assert(rs_good.batch_pending == 2);

	gem_close(fd, h1);
	gem_close(fd, h2);
	gem_close(fd, h6);
	gem_close(fd, h7);

	h1 = exec_valid(fd, ctx_good);
	igt_assert(h1 >= 0);
	gem_close(fd, h1);

	igt_assert(context_destroy(fd, ctx_good) == 0);
	igt_assert(context_destroy(fd, ctx_bad) == 0);
	igt_assert(gem_reset_status(fd, ctx_good) < 0);
	igt_assert(gem_reset_status(fd, ctx_bad) < 0);
	igt_assert(exec_valid(fd, ctx_good) < 0);
	igt_assert(exec_valid(fd, ctx_bad) < 0);

	close(fd);
}

static void test_nonrelated_hang(void)
{
	int h1,h2;
	int fd1,fd2;
	int ctx_guilty, ctx_unrelated;

	fd1 = drm_open_any();
	fd2 = drm_open_any();
	assert_reset_status(fd1, 0, RS_NO_ERROR);
	assert_reset_status(fd2, 0, RS_NO_ERROR);
	ctx_guilty = context_create(fd1);
	ctx_unrelated = context_create(fd2);

	assert_reset_status(fd1, ctx_guilty, RS_NO_ERROR);
	assert_reset_status(fd2, ctx_unrelated, RS_NO_ERROR);

	h1 = inject_hang(fd1, ctx_guilty);
	igt_assert(h1 >= 0);
	gem_sync(fd1, h1);
	assert_reset_status(fd1, ctx_guilty, RS_BATCH_ACTIVE);
	assert_reset_status(fd2, ctx_unrelated, RS_NO_ERROR);

	h2 = exec_valid(fd2, ctx_unrelated);
	igt_assert(h2 >= 0);
	gem_sync(fd2, h2);
	assert_reset_status(fd1, ctx_guilty, RS_BATCH_ACTIVE);
	assert_reset_status(fd2, ctx_unrelated, RS_NO_ERROR);
	gem_close(fd1, h1);
	gem_close(fd2, h2);

	igt_assert(context_destroy(fd1, ctx_guilty) == 0);
	igt_assert(context_destroy(fd2, ctx_unrelated) == 0);

	close(fd1);
	close(fd2);
}

static int get_reset_count(int fd, int ctx)
{
	int ret;
	struct local_drm_i915_reset_stats rs;

	ret = gem_reset_stats(fd, ctx, &rs);
	if (ret)
		return ret;

	return rs.reset_count;
}

static void test_double_destroy_pending(void)
{
	int fd, h;
	uint32_t ctx;

	fd = drm_open_any();
	igt_assert(fd >= 0);
	ctx = context_create(fd);

	assert_reset_status(fd, ctx, RS_NO_ERROR);

	h = inject_hang(fd, ctx);
	igt_assert(h >= 0);
	igt_assert(context_destroy(fd, ctx) == 0);
	igt_assert(context_destroy(fd, ctx) == -ENOENT);

	gem_close(fd, h);
	close(fd);
}

static void test_close_pending(void)
{
	int fd, h;

	fd = drm_open_any();
	igt_assert(fd >= 0);

	assert_reset_status(fd, 0, RS_NO_ERROR);

	h = inject_hang(fd, 0);
	igt_assert(h >= 0);

	gem_close(fd, h);
	close(fd);
}

static void __test_count(const bool create_ctx)
{
	int fd, h, ctx;
	long c1, c2;

	fd = drm_open_any();
	igt_assert(fd >= 0);
	if (create_ctx)
		ctx = context_create(fd);
	else
		ctx = 0;

	assert_reset_status(fd, ctx, RS_NO_ERROR);

	c1 = get_reset_count(fd, ctx);
	igt_assert(c1 >= 0);

	h = inject_hang(fd, ctx);
	igt_assert (h >= 0);
	gem_sync(fd, h);

	assert_reset_status(fd, ctx, RS_BATCH_ACTIVE);
	c2 = get_reset_count(fd, ctx);
	igt_assert(c2 >= 0);

	igt_assert(c2 == (c1 + 1));

	gem_close(fd, h);

	if (create_ctx)
		context_destroy(fd, ctx);

	close(fd);
}

static void test_count(void)
{
	return __test_count(false);
}

static void test_count_context(void)
{
	return __test_count(true);
}

static void test_global_reset_count(void)
{
	test_count();
	test_count_context();
}

static int _test_params(int fd, int ctx, uint32_t flags, uint32_t pad)
{
	struct local_drm_i915_reset_stats rs;
	int ret;

	rs.ctx_id = ctx;
	rs.flags = flags;
	rs.reset_count = rand();
	rs.batch_active = rand();
	rs.batch_pending = rand();
	rs.pad = pad;

	do {
		ret = ioctl(fd, GET_RESET_STATS_IOCTL, &rs);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));

	if (ret < 0)
		return -errno;

	return 0;
}

static void test_param_ctx(int fd, int ctx)
{
	const uint32_t bad = rand() + 1;

	igt_assert(_test_params(fd, ctx, 0, 0) == 0);
	igt_assert(_test_params(fd, ctx, 0, bad) == -EINVAL);
	igt_assert(_test_params(fd, ctx, bad, 0) == -EINVAL);
	igt_assert(_test_params(fd, ctx, bad, bad) == -EINVAL);
}

static void test_params(void)
{
	int fd, ctx;

	fd = drm_open_any();
	igt_assert(fd >= 0);
	ctx = context_create(fd);

	igt_assert(ioctl(fd, GET_RESET_STATS_IOCTL, 0) == -1);

	igt_assert(_test_params(fd, 0xbadbad, 0, 0) == -ENOENT);

	test_param_ctx(fd, 0);
	test_param_ctx(fd, ctx);

	close(fd);
}


igt_main
{
	struct local_drm_i915_gem_context_create create;
	uint32_t devid;
	int fd;
	int ret;

	igt_skip_on_simulation();

	igt_fixture {
		fd = drm_open_any();
		devid = intel_get_drm_devid(fd);
		igt_require_f(intel_gen(devid) >= 4,
			      "Architecture %d too old\n", intel_gen(devid));

		ret = drmIoctl(fd, CONTEXT_CREATE_IOCTL, &create);
		igt_skip_on_f(ret != 0 && (errno == ENODEV || errno == EINVAL),
			      "Kernel is too old, or contexts not supported: %s\n",
			      strerror(errno));

		close(fd);
	}

	igt_subtest("basic-reset-status")
		test_rs(4, 1, 0);

	igt_subtest("context-reset-status")
		test_rs_ctx(4, 4, 1, 2);

	igt_subtest("ban")
		test_ban();

	igt_subtest("ctx-unrelated")
		test_nonrelated_hang();

	igt_subtest("global-count")
		test_global_reset_count();

	igt_subtest("double-destroy-pending")
		test_double_destroy_pending();

	igt_subtest("close-pending")
		test_close_pending();

	igt_subtest("params")
		test_params();
}
