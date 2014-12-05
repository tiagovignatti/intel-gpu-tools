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
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "igt_aux.h"

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

	/* We assume that pipe 0 is running */

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

	alarm(0);

	fd = dup(in);
	if (nonblock) {
		int ret = -1;
		if (fd != -1)
			ret = fcntl(fd, F_GETFL);
		if (ret != -1) {
			ret |= O_NONBLOCK;
			ret = fcntl(fd, F_SETFL, ret);
		}
		igt_require(ret != -1);
	}

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

igt_main
{
	int fd;

	signal(SIGALRM, sighandler);
	siginterrupt(SIGALRM, 1);

	igt_fixture {
		fd = drm_open_any_master();
	}

	igt_subtest("invalid-buffer")
		test_invalid_buffer(fd);

	igt_subtest("empty-block")
		test_empty(fd, 0, EINTR);

	igt_subtest("empty-nonblock")
		test_empty(fd, 1, EAGAIN);

	igt_subtest("short-buffer-block")
		test_short_buffer(fd, 0);

	igt_subtest("short-buffer-nonblock")
		test_short_buffer(fd, 1);
}
