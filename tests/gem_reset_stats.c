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
#include "igt.h"
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


#define RS_NO_ERROR      0
#define RS_BATCH_ACTIVE  (1 << 0)
#define RS_BATCH_PENDING (1 << 1)
#define RS_UNKNOWN       (1 << 2)


static uint32_t devid;

struct local_drm_i915_reset_stats {
	__u32 ctx_id;
	__u32 flags;
	__u32 reset_count;
	__u32 batch_active;
	__u32 batch_pending;
	__u32 pad;
};

#define MAX_FD 32

#define GET_RESET_STATS_IOCTL DRM_IOWR(DRM_COMMAND_BASE + 0x32, struct local_drm_i915_reset_stats)

#define LOCAL_I915_EXEC_VEBOX	(4 << 0)

static void sync_gpu(void)
{
	int fd = drm_open_driver(DRIVER_INTEL);
	gem_quiescent_gpu(fd);
	close(fd);
}

static int noop(int fd, uint32_t ctx, const struct intel_execution_engine *e)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 eb;
	struct drm_i915_gem_exec_object2 exec;
	int ret;

	memset(&exec, 0, sizeof(exec));
	exec.handle = gem_create(fd, 4096);
	igt_assert((int)exec.handle > 0);
	gem_write(fd, exec.handle, 0, &bbe, sizeof(bbe));

	memset(&eb, 0, sizeof(eb));
	eb.buffers_ptr = (uintptr_t)&exec;
	eb.buffer_count = 1;
	eb.flags = e->exec_id | e->flags;
	i915_execbuffer2_set_context_id(eb, ctx);

	ret = __gem_execbuf(fd, &eb);
	if (ret < 0) {
		gem_close(fd, exec.handle);
		return ret;
	}

	return exec.handle;
}

static int has_engine(int fd,
		      uint32_t ctx,
		      const struct intel_execution_engine *e)
{
	int handle = noop(fd, ctx, e);
	if (handle < 0)
		return 0;
	gem_close(fd, handle);
	return 1;
}

static void check_context(const struct intel_execution_engine *e)
{
	int fd = drm_open_driver(DRIVER_INTEL);
	igt_require(has_engine(fd, gem_context_create(fd), e));
	close(fd);
}

static int gem_reset_stats(int fd, int ctx_id,
			   struct local_drm_i915_reset_stats *rs)
{
	memset(rs, 0, sizeof(*rs));
	rs->ctx_id = ctx_id;
	rs->reset_count = -1;

	if (drmIoctl(fd, GET_RESET_STATS_IOCTL, rs))
		return -errno;

	igt_assert(rs->reset_count != -1);
	return 0;
}

static int gem_reset_status(int fd, int ctx_id)
{
	struct local_drm_i915_reset_stats rs;
	int ret;

	ret = gem_reset_stats(fd, ctx_id, &rs);
	if (ret)
		return ret;

	if (rs.batch_active)
		return RS_BATCH_ACTIVE;
	if (rs.batch_pending)
		return RS_BATCH_PENDING;

	return RS_NO_ERROR;
}

#define BAN HANG_ALLOW_BAN
#define ASYNC 2
static void inject_hang(int fd, uint32_t ctx,
			const struct intel_execution_engine *e,
			unsigned flags)
{
	igt_hang_ring_t hang;

	hang = igt_hang_ctx(fd, ctx, e->exec_id | e->flags, flags & BAN, NULL);
	if ((flags & ASYNC) == 0)
		igt_post_hang_ring(fd, hang);
}

static const char *status_to_string(int x)
{
	const char *strings[] = {
		"No error",
		"Guilty",
		"Pending",
	};
	if (x >= ARRAY_SIZE(strings))
		return "Unknown";
	return strings[x];
}

static int _assert_reset_status(int idx, int fd, int ctx, int status)
{
	int rs;

	rs = gem_reset_status(fd, ctx);
	if (rs < 0) {
		igt_info("reset status for %d ctx %d returned %d\n",
			 idx, ctx, rs);
		return rs;
	}

	if (rs != status) {
		igt_info("%d:%d expected '%s' [%d], found '%s' [%d]\n",
			 idx, ctx,
			 status_to_string(status), status,
			 status_to_string(rs), rs);

		return 1;
	}

	return 0;
}

#define assert_reset_status(idx, fd, ctx, status) \
	igt_assert(_assert_reset_status(idx, fd, ctx, status) == 0)

static void test_rs(const struct intel_execution_engine *e,
		    int num_fds, int hang_index, int rs_assumed_no_hang)
{
	int fd[MAX_FD];
	int i;

	igt_assert_lte(num_fds, MAX_FD);
	igt_assert_lt(hang_index, MAX_FD);

	igt_debug("num fds=%d, hang index=%d\n", num_fds, hang_index);

	for (i = 0; i < num_fds; i++) {
		fd[i] = drm_open_driver(DRIVER_INTEL);
		assert_reset_status(i, fd[i], 0, RS_NO_ERROR);
	}

	sync_gpu();
	for (i = 0; i < num_fds; i++) {
		if (i == hang_index)
			inject_hang(fd[i], 0, e, ASYNC);
		else
			igt_assert(noop(fd[i], 0, e) > 0);
	}
	sync_gpu();

	for (i = 0; i < num_fds; i++) {
		if (hang_index < 0) {
			assert_reset_status(i, fd[i], 0, rs_assumed_no_hang);
			continue;
		}

		if (i < hang_index)
			assert_reset_status(i, fd[i], 0, RS_NO_ERROR);
		if (i == hang_index)
			assert_reset_status(i, fd[i], 0, RS_BATCH_ACTIVE);
		if (i > hang_index)
			assert_reset_status(i, fd[i], 0, RS_BATCH_PENDING);
	}

	for (i = 0; i < num_fds; i++)
		close(fd[i]);
}

#define MAX_CTX 100
static void test_rs_ctx(const struct intel_execution_engine *e,
			int num_fds, int num_ctx, int hang_index,
			int hang_context)
{
	int i, j;
	int fd[MAX_FD];
	int ctx[MAX_FD][MAX_CTX];

	igt_assert_lte(num_fds, MAX_FD);
	igt_assert_lt(hang_index, MAX_FD);

	igt_assert_lte(num_ctx, MAX_CTX);
	igt_assert_lt(hang_context, MAX_CTX);

	test_rs(e, num_fds, -1, RS_NO_ERROR);

	for (i = 0; i < num_fds; i++) {
		fd[i] = drm_open_driver(DRIVER_INTEL);
		igt_assert(fd[i]);
		assert_reset_status(i, fd[i], 0, RS_NO_ERROR);

		for (j = 0; j < num_ctx; j++) {
			ctx[i][j] = gem_context_create(fd[i]);
		}

		assert_reset_status(i, fd[i], 0, RS_NO_ERROR);
	}

	for (i = 0; i < num_fds; i++) {
		assert_reset_status(i, fd[i], 0, RS_NO_ERROR);

		for (j = 0; j < num_ctx; j++)
			assert_reset_status(i, fd[i], ctx[i][j], RS_NO_ERROR);

		assert_reset_status(i, fd[i], 0, RS_NO_ERROR);
	}

	for (i = 0; i < num_fds; i++) {
		for (j = 0; j < num_ctx; j++) {
			if (i == hang_index && j == hang_context)
				inject_hang(fd[i], ctx[i][j], e, ASYNC);
			else
				igt_assert(noop(fd[i], ctx[i][j], e) > 0);
		}
	}
	sync_gpu();

	for (i = 0; i < num_fds; i++)
		assert_reset_status(i, fd[i], 0, RS_NO_ERROR);

	for (i = 0; i < num_fds; i++) {
		for (j = 0; j < num_ctx; j++) {
			if (i < hang_index)
				assert_reset_status(i, fd[i], ctx[i][j], RS_NO_ERROR);
			if (i == hang_index && j < hang_context)
				assert_reset_status(i, fd[i], ctx[i][j], RS_NO_ERROR);
			if (i == hang_index && j == hang_context)
				assert_reset_status(i, fd[i], ctx[i][j],
						    RS_BATCH_ACTIVE);
			if (i == hang_index && j > hang_context)
				assert_reset_status(i, fd[i], ctx[i][j],
						    RS_BATCH_PENDING);
			if (i > hang_index)
				assert_reset_status(i, fd[i], ctx[i][j],
						    RS_BATCH_PENDING);
		}
	}

	for (i = 0; i < num_fds; i++) {
		assert_reset_status(i, fd[i], 0, RS_NO_ERROR);
		close(fd[i]);
	}
}

static void test_ban(const struct intel_execution_engine *e)
{
	struct local_drm_i915_reset_stats rs_bad, rs_good;
	int fd_bad, fd_good;
	int ban, retry = 10;
	int active_count = 0, pending_count = 0;

	fd_bad = drm_open_driver(DRIVER_INTEL);
	fd_good = drm_open_driver(DRIVER_INTEL);

	assert_reset_status(fd_bad, fd_bad, 0, RS_NO_ERROR);
	assert_reset_status(fd_good, fd_good, 0, RS_NO_ERROR);

	noop(fd_bad, 0, e);
	noop(fd_good, 0, e);

	assert_reset_status(fd_bad, fd_bad, 0, RS_NO_ERROR);
	assert_reset_status(fd_good, fd_good, 0, RS_NO_ERROR);

	inject_hang(fd_bad, 0, e, BAN | ASYNC);
	active_count++;

	noop(fd_good, 0, e);
	noop(fd_good, 0, e);

	/* The second hang will count as pending and be discarded */
	active_count--;
	pending_count += 2; /* inject hang does 2 execs (query, then hang) */
	while (retry--) {
		inject_hang(fd_bad, 0, e, BAN);
		active_count++;

		ban = noop(fd_bad, 0, e);
		if (ban == -EIO)
			break;

		/* Should not happen often but sometimes hang is declared too
		 * slow due to our way of faking hang using loop */
		gem_close(fd_bad, ban);

		igt_info("retrying for ban (%d)\n", retry);
	}
	igt_assert_eq(ban, -EIO);
	igt_assert_lt(0, noop(fd_good, 0, e));

	assert_reset_status(fd_bad, fd_bad, 0, RS_BATCH_ACTIVE);
	igt_assert_eq(gem_reset_stats(fd_bad, 0, &rs_bad), 0);
	igt_assert_eq(rs_bad.batch_active, active_count);
	igt_assert_eq(rs_bad.batch_pending, pending_count);

	assert_reset_status(fd_good, fd_good, 0, RS_BATCH_PENDING);
	igt_assert_eq(gem_reset_stats(fd_good, 0, &rs_good), 0);
	igt_assert_eq(rs_good.batch_active, 0);
	igt_assert_eq(rs_good.batch_pending, 2);

	close(fd_bad);
	close(fd_good);
}

static void test_ban_ctx(const struct intel_execution_engine *e)
{
	struct local_drm_i915_reset_stats rs_bad, rs_good;
	int fd, ban, retry = 10;
	uint32_t ctx_good, ctx_bad;
	int active_count = 0, pending_count = 0;

	fd = drm_open_driver(DRIVER_INTEL);

	assert_reset_status(fd, fd, 0, RS_NO_ERROR);

	ctx_good = gem_context_create(fd);
	ctx_bad = gem_context_create(fd);

	assert_reset_status(fd, fd, 0, RS_NO_ERROR);
	assert_reset_status(fd, fd, ctx_good, RS_NO_ERROR);
	assert_reset_status(fd, fd, ctx_bad, RS_NO_ERROR);

	noop(fd, ctx_bad, e);
	noop(fd, ctx_good, e);

	assert_reset_status(fd, fd, ctx_good, RS_NO_ERROR);
	assert_reset_status(fd, fd, ctx_bad, RS_NO_ERROR);

	inject_hang(fd, ctx_bad, e, BAN | ASYNC);
	active_count++;

	noop(fd, ctx_good, e);
	noop(fd, ctx_good, e);

	/* This second hang will count as pending and be discarded */
	active_count--;
	pending_count++;
	while (retry--) {
		inject_hang(fd, ctx_bad, e, BAN);
		active_count++;

		ban = noop(fd, ctx_bad, e);
		if (ban == -EIO)
			break;

		/* Should not happen often but sometimes hang is declared too
		 * slow due to our way of faking hang using loop */
		gem_close(fd, ban);

		igt_info("retrying for ban (%d)\n", retry);
	}
	igt_assert_eq(ban, -EIO);
	igt_assert_lt(0, noop(fd, ctx_good, e));

	assert_reset_status(fd, fd, ctx_bad, RS_BATCH_ACTIVE);
	igt_assert_eq(gem_reset_stats(fd, ctx_bad, &rs_bad), 0);
	igt_assert_eq(rs_bad.batch_active, active_count);
	igt_assert_eq(rs_bad.batch_pending, pending_count);

	assert_reset_status(fd, fd, ctx_good, RS_BATCH_PENDING);
	igt_assert_eq(gem_reset_stats(fd, ctx_good, &rs_good), 0);
	igt_assert_eq(rs_good.batch_active, 0);
	igt_assert_eq(rs_good.batch_pending, 2);

	close(fd);
}

static void test_unrelated_ctx(const struct intel_execution_engine *e)
{
	int fd1,fd2;
	int ctx_guilty, ctx_unrelated;

	fd1 = drm_open_driver(DRIVER_INTEL);
	fd2 = drm_open_driver(DRIVER_INTEL);
	assert_reset_status(0, fd1, 0, RS_NO_ERROR);
	assert_reset_status(1, fd2, 0, RS_NO_ERROR);
	ctx_guilty = gem_context_create(fd1);
	ctx_unrelated = gem_context_create(fd2);

	assert_reset_status(0, fd1, ctx_guilty, RS_NO_ERROR);
	assert_reset_status(1, fd2, ctx_unrelated, RS_NO_ERROR);

	inject_hang(fd1, ctx_guilty, e, 0);
	assert_reset_status(0, fd1, ctx_guilty, RS_BATCH_ACTIVE);
	assert_reset_status(1, fd2, ctx_unrelated, RS_NO_ERROR);

	gem_sync(fd2, noop(fd2, ctx_unrelated, e));
	assert_reset_status(0, fd1, ctx_guilty, RS_BATCH_ACTIVE);
	assert_reset_status(1, fd2, ctx_unrelated, RS_NO_ERROR);

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

static void test_close_pending_ctx(const struct intel_execution_engine *e)
{
	int fd = drm_open_driver(DRIVER_INTEL);
	uint32_t ctx = gem_context_create(fd);

	assert_reset_status(fd, fd, ctx, RS_NO_ERROR);

	inject_hang(fd, ctx, e, 0);
	gem_context_destroy(fd, ctx);
	igt_assert_eq(__gem_context_destroy(fd, ctx), -ENOENT);

	close(fd);
}

static void test_close_pending(const struct intel_execution_engine *e)
{
	int fd = drm_open_driver(DRIVER_INTEL);

	assert_reset_status(fd, fd, 0, RS_NO_ERROR);

	inject_hang(fd, 0, e, 0);
	close(fd);
}

static void noop_on_each_ring(int fd, const bool reverse)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 eb;
	struct drm_i915_gem_exec_object2 obj;
	const struct intel_execution_engine *e;

	memset(&obj, 0, sizeof(obj));
	obj.handle = gem_create(fd, 4096);
	gem_write(fd, obj.handle, 0, &bbe, sizeof(bbe));

	memset(&eb, 0, sizeof(eb));
	eb.buffers_ptr = (uintptr_t)&obj;
	eb.buffer_count = 1;

	if (reverse) {
		for (e = intel_execution_engines; e->name; e++)
			;
		while (--e >= intel_execution_engines) {
			eb.flags = e->exec_id | e->flags;
			__gem_execbuf(fd, &eb);
		}
	} else {
		for (e = intel_execution_engines; e->name; e++) {
			eb.flags = e->exec_id | e->flags;
			__gem_execbuf(fd, &eb);
		}
	}

	gem_sync(fd, obj.handle);
	gem_close(fd, obj.handle);
}

static void test_close_pending_fork(const struct intel_execution_engine *e,
				    const bool reverse)
{
	int fd = drm_open_driver(DRIVER_INTEL);
	igt_hang_ring_t hang;
	int pid;

	assert_reset_status(fd, fd, 0, RS_NO_ERROR);

	hang = igt_hang_ctx(fd, 0, e->exec_id | e->flags, 0, NULL);
	sleep(1);

	/* Avoid helpers as we need to kill the child
	 * without any extra signal handling on behalf of
	 * lib/drmtest.c
	 */
	pid = fork();
	if (pid == 0) {
		const int fd2 = drm_open_driver(DRIVER_INTEL);
		igt_assert_lte(0, fd2);

		/* The crucial component is that we schedule the same noop batch
		 * on each ring. This exercises batch_obj reference counting,
		 * when gpu is reset and ring lists are cleared.
		 */
		noop_on_each_ring(fd2, reverse);
		close(fd2);
		pause();
		exit(0);
	} else {
		igt_assert_lt(0, pid);
		sleep(1);

		/* Kill the child to reduce refcounts on
		   batch_objs */
		kill(pid, SIGKILL);
	}

	igt_post_hang_ring(fd, hang);
	close(fd);
}

static void test_reset_count(const struct intel_execution_engine *e,
			     const bool create_ctx)
{
	int fd = drm_open_driver(DRIVER_INTEL);
	int ctx;
	long c1, c2;

	if (create_ctx)
		ctx = gem_context_create(fd);
	else
		ctx = 0;

	assert_reset_status(fd, fd, ctx, RS_NO_ERROR);

	c1 = get_reset_count(fd, ctx);
	igt_assert(c1 >= 0);

	inject_hang(fd, ctx, e, 0);

	assert_reset_status(fd, fd, ctx, RS_BATCH_ACTIVE);
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

	if (create_ctx)
		gem_context_destroy(fd, ctx);

	close(fd);
}

static int _test_params(int fd, int ctx, uint32_t flags, uint32_t pad)
{
	struct local_drm_i915_reset_stats rs;

	memset(&rs, 0, sizeof(rs));
	rs.ctx_id = ctx;
	rs.flags = flags;
	rs.reset_count = rand();
	rs.batch_active = rand();
	rs.batch_pending = rand();
	rs.pad = pad;

	if (drmIoctl(fd, GET_RESET_STATS_IOCTL, &rs))
		return -errno;

	return 0;
}

typedef enum { root = 0, user } cap_t;

static void _check_param_ctx(const int fd, const int ctx, const cap_t cap)
{
	const uint32_t bad = rand() + 1;

	if (ctx == 0) {
		if (cap == root)
			igt_assert_eq(_test_params(fd, ctx, 0, 0), 0);
		else
			igt_assert_eq(_test_params(fd, ctx, 0, 0), -EPERM);
	}

	igt_assert_eq(_test_params(fd, ctx, 0, bad), -EINVAL);
	igt_assert_eq(_test_params(fd, ctx, bad, 0), -EINVAL);
	igt_assert_eq(_test_params(fd, ctx, bad, bad), -EINVAL);
}

static void check_params(const int fd, const int ctx, cap_t cap)
{
	igt_assert(ioctl(fd, GET_RESET_STATS_IOCTL, 0) == -1);
	igt_assert_eq(_test_params(fd, 0xbadbad, 0, 0), -ENOENT);

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
	int fd;

	fd = drm_open_driver(DRIVER_INTEL);
	_test_param(fd, gem_context_create(fd));
	close(fd);
}

static void test_params(void)
{
	int fd;

	fd = drm_open_driver(DRIVER_INTEL);
	_test_param(fd, 0);
	close(fd);
}

static const struct intel_execution_engine *
next_engine(int fd, const struct intel_execution_engine *e)
{
	do {
		e++;
		if (e->name == NULL)
			e = intel_execution_engines;
		if (e->exec_id == 0)
			e++;
	} while (!has_engine(fd, 0, e));

	return e;
}

static void defer_hangcheck(const struct intel_execution_engine *engine)
{
	const struct intel_execution_engine *next;
	int fd, count_start, count_end;
	int seconds = 30;

	fd = drm_open_driver(DRIVER_INTEL);

	next = next_engine(fd, engine);
	igt_skip_on(next == engine);

	count_start = get_reset_count(fd, 0);
	igt_assert_lte(0, count_start);

	inject_hang(fd, 0, engine, 0);
	while (--seconds) {
		noop(fd, 0, next);

		count_end = get_reset_count(fd, 0);
		igt_assert_lte(0, count_end);

		if (count_end > count_start)
			break;

		sleep(1);
	}

	igt_assert_lt(count_start, count_end);

	close(fd);
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

#define RUN_TEST(...) do { sync_gpu(); __VA_ARGS__; sync_gpu(); } while (0)
#define RUN_CTX_TEST(...) do { check_context(e); RUN_TEST(__VA_ARGS__); } while (0)

igt_main
{
	const struct intel_execution_engine *e;
	igt_skip_on_simulation();

	igt_fixture {
		int fd;

		bool has_reset_stats;
		fd = drm_open_driver(DRIVER_INTEL);
		devid = intel_get_drm_devid(fd);

		has_reset_stats = gem_has_reset_stats(fd);

		close(fd);

		igt_require_f(has_reset_stats,
			      "No reset stats ioctl support. Too old kernel?\n");
	}

	igt_subtest("params")
		test_params();

	igt_subtest_f("params-ctx")
		RUN_TEST(test_params_ctx());

	for (e = intel_execution_engines; e->name; e++) {
		igt_subtest_f("reset-stats-%s", e->name)
			RUN_TEST(test_rs(e, 4, 1, 0));

		igt_subtest_f("reset-stats-ctx-%s", e->name)
			RUN_CTX_TEST(test_rs_ctx(e, 4, 4, 1, 2));

		igt_subtest_f("ban-%s", e->name)
			RUN_TEST(test_ban(e));

		igt_subtest_f("ban-ctx-%s", e->name)
			RUN_CTX_TEST(test_ban_ctx(e));

		igt_subtest_f("reset-count-%s", e->name)
			RUN_TEST(test_reset_count(e, false));

		igt_subtest_f("reset-count-ctx-%s", e->name)
			RUN_CTX_TEST(test_reset_count(e, true));

		igt_subtest_f("unrelated-ctx-%s", e->name)
			RUN_CTX_TEST(test_unrelated_ctx(e));

		igt_subtest_f("close-pending-%s", e->name)
			RUN_TEST(test_close_pending(e));

		igt_subtest_f("close-pending-ctx-%s", e->name)
			RUN_CTX_TEST(test_close_pending_ctx(e));

		igt_subtest_f("close-pending-fork-%s", e->name)
			RUN_TEST(test_close_pending_fork(e, false));

		igt_subtest_f("close-pending-fork-reverse-%s", e->name)
			RUN_TEST(test_close_pending_fork(e, true));

		igt_subtest_f("defer-hangcheck-%s", e->name)
			RUN_TEST(defer_hangcheck(e));
	}
}
