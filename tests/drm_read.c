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
 * Authors:
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

/*
 * Testcase: boundary testing of read(drm_fd)
 */

#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/poll.h>
#include "drm.h"

IGT_TEST_DESCRIPTION("Call read(drm) and see if it behaves.");

static void sighandler(int sig)
{
}

static void assert_empty(int fd)
{
	struct pollfd pfd = {fd, POLLIN};
	do_or_die(poll(&pfd, 1, 0));
}

static void generate_event(int fd)
{
	union drm_wait_vblank vbl;

	/* We require that pipe 0 is running */

	vbl.request.type =
		DRM_VBLANK_RELATIVE |
		DRM_VBLANK_EVENT;
	vbl.request.sequence = 0;

	do_ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
}

static void wait_for_event(int fd)
{
	struct pollfd pfd = {fd, POLLIN};
	igt_assert(poll(&pfd, 1, -1) == 1);
}

static int setup(int in, int nonblock)
{
	int fd;
	int ret = -1;

	alarm(0);

	fd = dup(in);
	if (fd != -1)
		ret = fcntl(fd, F_GETFL);
	if (ret != -1) {
		if (nonblock)
			ret |= O_NONBLOCK;
		else
			ret &= ~O_NONBLOCK;
		ret = fcntl(fd, F_SETFL, ret);
	}
	igt_require(ret != -1);

	assert_empty(fd);
	return fd;
}

static void teardown(int fd)
{
	alarm(0);
	assert_empty(fd);
	close(fd);
	errno = 0;
}

static void test_invalid_buffer(int in)
{
	int fd = setup(in, 0);

	alarm(1);

	igt_assert_eq(read(fd, (void *)-1, 4096), -1);
	igt_assert_eq(errno, EFAULT);

	teardown(fd);
}

static uint32_t dumb_create(int fd)
{
	struct drm_mode_create_dumb arg;

	arg.bpp = 32;
	arg.width = 32;
	arg.height = 32;

	do_ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &arg);
	igt_assert(arg.size >= 4096);

	return arg.handle;
}

static void test_fault_buffer(int in)
{
	int fd = setup(in, 0);
	struct drm_mode_map_dumb arg;
	char *buf;

	memset(&arg, 0, sizeof(arg));
	arg.handle = dumb_create(fd);

	do_ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &arg);

	buf = mmap(0, 4096, PROT_WRITE, MAP_SHARED, fd, arg.offset);
	igt_assert(buf != MAP_FAILED);

	generate_event(fd);

	alarm(1);

	igt_assert(read(fd, buf, 4096) > 0);

	munmap(buf, 4096);
	teardown(fd);
}

static void test_empty(int in, int nonblock, int expected)
{
	char buffer[1024];
	int fd = setup(in, nonblock);

	alarm(1);
	igt_assert_eq(read(fd, buffer, sizeof(buffer)), -1);
	igt_assert_eq(errno, expected);

	teardown(fd);
}

static void test_short_buffer(int in, int nonblock)
{
	char buffer[1024]; /* events are typically 32 bytes */
	int fd = setup(in, nonblock);

	generate_event(fd);
	generate_event(fd);

	wait_for_event(fd);

	alarm(3);

	igt_assert_eq(read(fd, buffer, 4), 0);
	igt_assert(read(fd, buffer, 40) > 0);
	igt_assert(read(fd, buffer, 40) > 0);

	teardown(fd);
}

static int pipe0_enabled(int fd)
{
	struct drm_mode_card_res res;
	uint32_t crtcs[32];
	int i;

	/* We assume we can generate events on pipe 0. So we have better
	 * make sure that is running!
	 */

	memset(&res, 0, sizeof(res));
	res.count_crtcs = 32;
	res.crtc_id_ptr = (uintptr_t)crtcs;

	if (drmIoctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res))
		return 0;

	if (res.count_crtcs > 32)
		return 0;

	for (i = 0; i < res.count_crtcs; i++) {
		struct drm_i915_get_pipe_from_crtc_id get_pipe;
		struct drm_mode_crtc mode;

		memset(&get_pipe, 0, sizeof(get_pipe));
		memset(&mode, 0, sizeof(mode));

		mode.crtc_id = crtcs[i];

		get_pipe.pipe = -1;
		get_pipe.crtc_id = mode.crtc_id;
		drmIoctl(fd, DRM_IOCTL_I915_GET_PIPE_FROM_CRTC_ID, &get_pipe);
		if (get_pipe.pipe)
			continue;

		drmIoctl(fd, DRM_IOCTL_MODE_GETCRTC, &mode);
		return mode.mode_valid && mode.mode.clock;
	}

	return 0;
}

igt_main
{
	int fd;

	signal(SIGALRM, sighandler);
	siginterrupt(SIGALRM, 1);

	igt_fixture {
		fd = drm_open_driver_master(DRIVER_INTEL);
		igt_require(pipe0_enabled(fd));
	}

	igt_subtest("invalid-buffer")
		test_invalid_buffer(fd);

	igt_subtest("fault-buffer")
		test_fault_buffer(fd);

	igt_subtest("empty-block")
		test_empty(fd, 0, EINTR);

	igt_subtest("empty-nonblock")
		test_empty(fd, 1, EAGAIN);

	igt_subtest("short-buffer-block")
		test_short_buffer(fd, 0);

	igt_subtest("short-buffer-nonblock")
		test_short_buffer(fd, 1);
}
