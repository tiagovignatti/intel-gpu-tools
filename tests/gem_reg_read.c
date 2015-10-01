/*
 * Copyright © 2012 Intel Corporation
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
 *    Ben Widawsky <ben@bwidawsk.net>
 *
 */

#include "igt.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/utsname.h>
#include <time.h>


static bool is_x86_64;
static bool has_proper_timestamp;

struct local_drm_i915_reg_read {
	__u64 offset;
	__u64 val; /* Return value */
};

#define REG_READ_IOCTL DRM_IOWR(DRM_COMMAND_BASE + 0x31, struct local_drm_i915_reg_read)

#define RENDER_RING_TIMESTAMP 0x2358

static int read_register(int fd, uint64_t offset, uint64_t * val)
{
	int ret = 0;
	struct local_drm_i915_reg_read reg_read;
	reg_read.offset = offset;

	if (drmIoctl(fd, REG_READ_IOCTL, &reg_read))
		ret = -errno;

	*val = reg_read.val;

	return ret;
}

static bool check_kernel_x86_64(void)
{
	int ret;
	struct utsname uts;

	ret = uname(&uts);
	igt_assert_eq(ret, 0);

	if (!strcmp(uts.machine, "x86_64"))
		return true;

	return false;
}

static bool check_timestamp(int fd)
{
	int ret;
	uint64_t val;

	ret = read_register(fd, RENDER_RING_TIMESTAMP | 1, &val);

	return ret == 0;
}

static int timer_query(int fd, uint64_t * val)
{
	uint64_t offset;
	int ret;

	offset = RENDER_RING_TIMESTAMP;
	if (has_proper_timestamp)
		offset |= 1;

	ret = read_register(fd, offset, val);

/*
 * When reading the timestamp register with single 64b read, we are observing
 * invalid values on x86_64:
 *
 *      [f = valid counter value | X = garbage]
 *
 *      i386:   0x0000000fffffffff
 *      x86_64: 0xffffffffXXXXXXXX
 *
 * In the absence of a corrected register read ioctl, attempt
 * to fix up the return value to be vaguely useful.
 */

	if (is_x86_64 && !has_proper_timestamp)
		*val >>= 32;

	return ret;
}

static void test_timestamp_moving(int fd)
{
	uint64_t first_val, second_val;

	igt_fail_on(timer_query(fd, &first_val) != 0);
	sleep(1);
	igt_fail_on(timer_query(fd, &second_val) != 0);
	igt_assert(second_val != first_val);
}

static void test_timestamp_monotonic(int fd)
{
	uint64_t first_val, second_val;
	time_t start;
	bool retry = true;

	igt_fail_on(timer_query(fd, &first_val) != 0);
	time(&start);
	do {
retry:
		igt_fail_on(timer_query(fd, &second_val) != 0);
		if (second_val < first_val && retry) {
		/* We may hit timestamp overflow once */
			retry = false;
			first_val = second_val;
			goto retry;
		}
		igt_assert(second_val >= first_val);
	} while(difftime(time(NULL), start) < 5);

}

igt_main
{
	uint64_t val = 0;
	int fd = -1;

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);
		is_x86_64 = check_kernel_x86_64();
		has_proper_timestamp = check_timestamp(fd);
	}

	igt_subtest("bad-register")
		igt_assert_eq(read_register(fd, 0x12345678, &val), -EINVAL);

	igt_subtest("timestamp-moving") {
		igt_skip_on(timer_query(fd, &val) != 0);
		test_timestamp_moving(fd);
	}

	igt_subtest("timestamp-monotonic") {
		igt_skip_on(timer_query(fd, &val) != 0);
		test_timestamp_monotonic(fd);
	}

	igt_fixture {
		close(fd);
	}
}
