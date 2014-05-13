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

#define _GNU_SOURCE
#include <stdbool.h>
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
#include <signal.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "igt_debugfs.h"
#include "intel_chipset.h"
#include "intel_io.h"
#include "igt_aux.h"

#define RS_NO_ERROR      0
#define RS_BATCH_ACTIVE  (1 << 0)
#define RS_BATCH_PENDING (1 << 1)
#define RS_UNKNOWN       (1 << 2)

static uint32_t devid;
static bool hw_contexts;

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

#define LOCAL_I915_EXEC_VEBOX	(4 << 0)

struct target_ring;

static bool gem_has_render(int fd)
{
	return true;
}

static bool has_context(const struct target_ring *ring);

static const struct target_ring {
	uint32_t exec;
	bool (*present)(int fd);
	bool (*contexts)(const struct target_ring *ring);
	const char *name;
} rings[] = {
	{ I915_EXEC_RENDER, gem_has_render, has_context, "render" },
	{ I915_EXEC_BLT, gem_has_blt, has_context, "blt" },
	{ I915_EXEC_BSD, gem_has_bsd, has_context, "bsd" },
	{ LOCAL_I915_EXEC_VEBOX, gem_has_vebox, has_context, "vebox" },
};

static bool has_context(const struct target_ring *ring)
{
	if (!hw_contexts)
		return false;

	if(ring->exec == I915_EXEC_RENDER)
		return true;

	return false;
}

#define NUM_RINGS (sizeof(rings)/sizeof(struct target_ring))

static const struct target_ring *current_ring;

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

static int exec_valid_ring(int fd, int ctx, int ring)
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
	execbuf.flags = ring;
	i915_execbuffer2_set_context_id(execbuf, ctx);
	execbuf.rsvd2 = 0;

	ret = gem_exec(fd, &execbuf);
	if (ret < 0)
		return ret;

	return exec.handle;
}

static int exec_valid(int fd, int ctx)
{
	return exec_valid_ring(fd, ctx, current_ring->exec);
}

#define BUFSIZE (4 * 1024)
#define ITEMS   (BUFSIZE >> 2)

static int inject_hang_ring(int fd, int ctx, int ring, bool ignore_ban_error)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec;
	uint64_t gtt_off;
	uint32_t *buf;
	int roff, i;
	unsigned cmd_len = 2;
	enum stop_ring_flags flags;

	srandom(time(NULL));

	if (intel_gen(devid) >= 8)
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
	execbuf.flags = ring;
	i915_execbuffer2_set_context_id(execbuf, ctx);
	execbuf.rsvd2 = 0;

	igt_assert(gem_exec(fd, &execbuf) == 0);

	gtt_off = exec.offset;

	for (i = 0; i < ITEMS; i++)
		buf[i] = MI_NOOP;

	roff = random() % (ITEMS - cmd_len - 1);
	buf[roff] = MI_BATCH_BUFFER_START | (cmd_len - 2);
	buf[roff + 1] = (gtt_off & 0xfffffffc) + (roff << 2);
	if (cmd_len == 3)
		buf[roff + 2] = (gtt_off & 0xffffffff00000000ull) >> 32;

	buf[roff + cmd_len] = MI_BATCH_BUFFER_END;

	igt_debug("loop injected at 0x%lx (off 0x%x, bo_start 0x%lx, bo_end 0x%lx)\n",
		  (long unsigned int)((roff << 2) + gtt_off),
		  roff << 2, (long unsigned int)gtt_off,
		  (long unsigned int)(gtt_off + BUFSIZE - 1));
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
	execbuf.flags = ring;
	i915_execbuffer2_set_context_id(execbuf, ctx);
	execbuf.rsvd2 = 0;

	igt_assert(gem_exec(fd, &execbuf) == 0);

	igt_assert(gtt_off == exec.offset);

	free(buf);

	flags = igt_to_stop_ring_flag(ring);

	flags |= STOP_RING_ALLOW_BAN;

	if (!ignore_ban_error)
		flags |= STOP_RING_ALLOW_ERRORS;

	igt_set_stop_rings(flags);

	return exec.handle;
}

static int inject_hang(int fd, int ctx)
{
	return inject_hang_ring(fd, ctx, current_ring->exec, false);
}

static int inject_hang_no_ban_error(int fd, int ctx)
{
	return inject_hang_ring(fd, ctx, current_ring->exec, true);
}

static int _assert_reset_status(int fd, int ctx, int status)
{
	int rs;

	rs = gem_reset_status(fd, ctx);
	if (rs < 0) {
		igt_info("reset status for %d ctx %d returned %d\n",
			 fd, ctx, rs);
		return rs;
	}

	if (rs != status) {
		igt_info("%d:%d reset status %d differs from assumed %d\n",
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
	int fd_bad, fd_good;
	int retry = 10;
	int active_count = 0, pending_count = 0;
	struct local_drm_i915_reset_stats rs_bad, rs_good;

	fd_bad = drm_open_any();
	igt_assert(fd_bad >= 0);

	fd_good = drm_open_any();
	igt_assert(fd_good >= 0);

	assert_reset_status(fd_bad, 0, RS_NO_ERROR);
	assert_reset_status(fd_good, 0, RS_NO_ERROR);

	h1 = exec_valid(fd_bad, 0);
	igt_assert(h1 >= 0);
	h5 = exec_valid(fd_good, 0);
	igt_assert(h5 >= 0);

	assert_reset_status(fd_bad, 0, RS_NO_ERROR);
	assert_reset_status(fd_good, 0, RS_NO_ERROR);

	h2 = inject_hang_no_ban_error(fd_bad, 0);
	igt_assert(h2 >= 0);
	active_count++;
	/* Second hang will be pending for this */
	pending_count++;

	h6 = exec_valid(fd_good, 0);
	h7 = exec_valid(fd_good, 0);

        while (retry--) {
                h3 = inject_hang_no_ban_error(fd_bad, 0);
                igt_assert(h3 >= 0);
                gem_sync(fd_bad, h3);
		active_count++;
		/* This second hand will count as pending */
                assert_reset_status(fd_bad, 0, RS_BATCH_ACTIVE);

                h4 = exec_valid(fd_bad, 0);
                if (h4 == -EIO) {
                        gem_close(fd_bad, h3);
                        break;
                }

                /* Should not happen often but sometimes hang is declared too slow
                 * due to our way of faking hang using loop */

                igt_assert(h4 >= 0);
                gem_close(fd_bad, h3);
                gem_close(fd_bad, h4);

                igt_info("retrying for ban (%d)\n", retry);
        }

	igt_assert(h4 == -EIO);
	assert_reset_status(fd_bad, 0, RS_BATCH_ACTIVE);

	gem_sync(fd_good, h7);
	assert_reset_status(fd_good, 0, RS_BATCH_PENDING);

	igt_assert(gem_reset_stats(fd_good, 0, &rs_good) == 0);
	igt_assert(gem_reset_stats(fd_bad, 0, &rs_bad) == 0);

	igt_assert(rs_bad.batch_active == active_count);
	igt_assert(rs_bad.batch_pending == pending_count);
	igt_assert(rs_good.batch_active == 0);
	igt_assert(rs_good.batch_pending == 2);

	gem_close(fd_bad, h1);
	gem_close(fd_bad, h2);
	gem_close(fd_good, h6);
	gem_close(fd_good, h7);

	h1 = exec_valid(fd_good, 0);
	igt_assert(h1 >= 0);
	gem_close(fd_good, h1);

	close(fd_bad);
	close(fd_good);

	igt_assert(gem_reset_status(fd_bad, 0) < 0);
	igt_assert(gem_reset_status(fd_good, 0) < 0);
}

static void test_ban_ctx(void)
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

	h2 = inject_hang_no_ban_error(fd, ctx_bad);
	igt_assert(h2 >= 0);
	active_count++;
	/* Second hang will be pending for this */
	pending_count++;

	h6 = exec_valid(fd, ctx_good);
	h7 = exec_valid(fd, ctx_good);

        while (retry--) {
                h3 = inject_hang_no_ban_error(fd, ctx_bad);
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

                igt_info("retrying for ban (%d)\n", retry);
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

static void test_unrelated_ctx(void)
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

static void test_close_pending_ctx(void)
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

static void exec_noop_on_each_ring(int fd, const bool reverse)
{
	uint32_t batch[2] = {MI_BATCH_BUFFER_END, 0};
	uint32_t handle;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec[1];

	handle = gem_create(fd, 4096);
	gem_write(fd, handle, 0, batch, sizeof(batch));

	exec[0].handle = handle;
	exec[0].relocation_count = 0;
	exec[0].relocs_ptr = 0;
	exec[0].alignment = 0;
	exec[0].offset = 0;
	exec[0].flags = 0;
	exec[0].rsvd1 = 0;
	exec[0].rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = 8;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = 0;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	for (unsigned i = 0; i < NUM_RINGS; i++) {
		const struct target_ring *ring;

		ring = reverse ? &rings[NUM_RINGS - 1 - i] : &rings[i];

		if (ring->present(fd)) {
			execbuf.flags = ring->exec;
			do_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
		}
	}

	gem_sync(fd, handle);
	gem_close(fd, handle);
}

static void test_close_pending_fork(const bool reverse)
{
	int pid;
	int fd, h;

	fd = drm_open_any();
	igt_assert(fd >= 0);

	assert_reset_status(fd, 0, RS_NO_ERROR);

	h = inject_hang(fd, 0);
	igt_assert(h >= 0);

	sleep(1);

	/* Avoid helpers as we need to kill the child
	 * without any extra signal handling on behalf of
	 * lib/drmtest.c
	 */
	pid = fork();
	if (pid == 0) {
		const int fd2 = drm_open_any();
		igt_assert(fd2 >= 0);

		/* The crucial component is that we schedule the same noop batch
		 * on each ring. This exercises batch_obj reference counting,
		 * when gpu is reset and ring lists are cleared.
		 */
		exec_noop_on_each_ring(fd2, reverse);

		close(fd2);
		return;
	} else {
		igt_assert(pid > 0);
		sleep(1);

		/* Kill the child to reduce refcounts on
		   batch_objs */
		kill(pid, SIGKILL);
	}

	gem_close(fd, h);
	close(fd);

	/* Then we just wait on hang to happen */
	fd = drm_open_any();
	igt_assert(fd >= 0);

	h = exec_valid(fd, 0);
	igt_assert(h >= 0);

	gem_sync(fd, h);
	gem_close(fd, h);
	close(fd);
}

static void test_reset_count(const bool create_ctx)
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

	igt_fork(child, 1) {
		igt_drop_root();

		c2 = get_reset_count(fd, ctx);

		if (ctx == 0)
			igt_assert(c2 == -EPERM);
		else
			igt_assert(c2 == 0);
	}

	igt_waitchildren();

	gem_close(fd, h);

	if (create_ctx)
		context_destroy(fd, ctx);

	close(fd);
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

typedef enum { root = 0, user } cap_t;

static void _check_param_ctx(const int fd, const int ctx, const cap_t cap)
{
	const uint32_t bad = rand() + 1;

	if (ctx == 0) {
		if (cap == root)
			igt_assert(_test_params(fd, ctx, 0, 0) == 0);
		else
			igt_assert(_test_params(fd, ctx, 0, 0) == -EPERM);
	}

	igt_assert(_test_params(fd, ctx, 0, bad) == -EINVAL);
	igt_assert(_test_params(fd, ctx, bad, 0) == -EINVAL);
	igt_assert(_test_params(fd, ctx, bad, bad) == -EINVAL);
}

static void check_params(const int fd, const int ctx, cap_t cap)
{
	igt_assert(ioctl(fd, GET_RESET_STATS_IOCTL, 0) == -1);
	igt_assert(_test_params(fd, 0xbadbad, 0, 0) == -ENOENT);

	_check_param_ctx(fd, ctx, cap);
}

static void _test_param(const int fd, const int ctx)
{
	check_params(fd, ctx, root);

	igt_fork(child, 1) {
		check_params(fd, ctx, root);

		igt_drop_root();

		check_params(fd, ctx, user);
	}

	check_params(fd, ctx, root);

	igt_waitchildren();
}

static void test_params_ctx(void)
{
	int fd, ctx;

	fd = drm_open_any();
	igt_assert(fd >= 0);
	ctx = context_create(fd);

	_test_param(fd, ctx);

	close(fd);
}

static void test_params(void)
{
	int fd;

	fd = drm_open_any();
	igt_assert(fd >= 0);

	_test_param(fd, 0);

	close(fd);

}

static bool gem_has_hw_contexts(int fd)
{
	struct local_drm_i915_gem_context_create create;
	int ret;

	memset(&create, 0, sizeof(create));
	ret = drmIoctl(fd, CONTEXT_CREATE_IOCTL, &create);

	if (ret == 0) {
		drmIoctl(fd, CONTEXT_DESTROY_IOCTL, &create);
		return true;
	}

	return false;
}

static bool gem_has_reset_stats(int fd)
{
	struct local_drm_i915_reset_stats rs;
	int ret;

	/* Carefully set flags and pad to zero, otherwise
	   we get -EINVAL
	*/
	memset(&rs, 0, sizeof(rs));

	ret = drmIoctl(fd, GET_RESET_STATS_IOCTL, &rs);
	if (ret == 0)
		return true;

	/* If we get EPERM, we have support but did not
	   have CAP_SYSADM */
	if (ret == -1 && errno == EPERM)
		return true;

	return false;
}

static void check_gpu_ok(void)
{
	int retry_count = 30;
	enum stop_ring_flags flags;
	int fd;

	igt_debug("checking gpu state\n");

	while (retry_count--) {
		flags = igt_get_stop_rings();
		if (flags == 0)
			break;

		igt_debug("waiting previous hang to clear\n");
		sleep(1);
	}

	igt_assert(flags == 0);

	fd = drm_open_any();
	gem_quiescent_gpu(fd);
	close(fd);
}

#define RING_HAS_CONTEXTS (current_ring->contexts(current_ring))
#define RUN_TEST(...) do { check_gpu_ok(); __VA_ARGS__; check_gpu_ok(); } while (0)
#define RUN_CTX_TEST(...) do { igt_skip_on(RING_HAS_CONTEXTS == false); RUN_TEST(__VA_ARGS__); } while (0)

igt_main
{
	igt_skip_on_simulation();

	igt_fixture {
		int fd;

		bool has_reset_stats;
		fd = drm_open_any();
		devid = intel_get_drm_devid(fd);

		hw_contexts = gem_has_hw_contexts(fd);
		has_reset_stats = gem_has_reset_stats(fd);

		close(fd);

		igt_require_f(has_reset_stats,
			      "No reset stats ioctl support. Too old kernel?\n");
	}

	igt_subtest("params")
		test_params();

	for (int i = 0; i < NUM_RINGS; i++) {
		const char *name;

		current_ring = &rings[i];
		name = current_ring->name;

		igt_fixture {
			int fd = drm_open_any();
			gem_require_ring(fd, current_ring->exec);
			close(fd);
		}

		igt_fixture
			igt_require_f(intel_gen(devid) >= 4,
				      "gen %d doesn't support reset\n", intel_gen(devid));

		igt_subtest_f("params-ctx-%s", name)
			RUN_CTX_TEST(test_params_ctx());

		igt_subtest_f("reset-stats-%s", name)
			RUN_TEST(test_rs(4, 1, 0));

		igt_subtest_f("reset-stats-ctx-%s", name)
			RUN_CTX_TEST(test_rs_ctx(4, 4, 1, 2));

		igt_subtest_f("ban-%s", name)
			RUN_TEST(test_ban());

		igt_subtest_f("ban-ctx-%s", name)
			RUN_CTX_TEST(test_ban_ctx());

		igt_subtest_f("reset-count-%s", name)
			RUN_TEST(test_reset_count(false));

		igt_subtest_f("reset-count-ctx-%s", name)
			RUN_CTX_TEST(test_reset_count(true));

		igt_subtest_f("unrelated-ctx-%s", name)
			RUN_CTX_TEST(test_unrelated_ctx());

		igt_subtest_f("close-pending-%s", name)
			RUN_TEST(test_close_pending());

		igt_subtest_f("close-pending-ctx-%s", name)
			RUN_CTX_TEST(test_close_pending_ctx());

		igt_subtest_f("close-pending-fork-%s", name)
			RUN_TEST(test_close_pending_fork(false));

		igt_subtest_f("close-pending-fork-reverse-%s", name)
			RUN_TEST(test_close_pending_fork(true));
	}
}
