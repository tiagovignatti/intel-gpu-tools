/*
 * Copyright © 2015 Intel Corporation
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
 * Author:
 *    Antti Koskipaa <antti.koskipaa@linux.intel.com>
 *
 */

#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#include "igt_core.h"

#define TOLERANCE 5 /* percent */
#define BACKLIGHT_PATH "/sys/class/backlight/intel_backlight"

#define FADESTEPS 10
#define FADESPEED 100 /* milliseconds between steps */

IGT_TEST_DESCRIPTION("Basic backlight sysfs test");

static int backlight_read(int *result, const char *fname)
{
	int fd;
	char full[PATH_MAX];
	char dst[64];
	int r, e;

	igt_assert(snprintf(full, PATH_MAX, "%s/%s", BACKLIGHT_PATH, fname) < PATH_MAX);

	fd = open(full, O_RDONLY);
	if (fd == -1)
		return -errno;

	r = read(fd, dst, sizeof(dst));
	e = errno;
	close(fd);

	if (r < 0)
		return -e;

	errno = 0;
	*result = strtol(dst, NULL, 10);
	return errno;
}

static int backlight_write(int value, const char *fname)
{
	int fd;
	char full[PATH_MAX];
	char src[64];
	int len;

	igt_assert(snprintf(full, PATH_MAX, "%s/%s", BACKLIGHT_PATH, fname) < PATH_MAX);
	fd = open(full, O_WRONLY);
	if (fd == -1)
		return -errno;

	len = snprintf(src, sizeof(src), "%i", value);
	len = write(fd, src, len);
	close(fd);

	if (len < 0)
		return len;

	return 0;
}

static void test_and_verify(int val)
{
	int result;

	igt_assert(backlight_write(val, "brightness") == 0);
	igt_assert(backlight_read(&result, "brightness") == 0);
	/* Check that the exact value sticks */
	igt_assert(result == val);

	igt_assert(backlight_read(&result, "actual_brightness") == 0);
	/* Some rounding may happen depending on hw. Just check that it's close enough. */
	igt_assert(result <= val + val * TOLERANCE / 100 && result >= val - val * TOLERANCE / 100);
}

static void test_brightness(int max)
{
	test_and_verify(0);
	test_and_verify(max);
	test_and_verify(max / 2);
}

static void test_bad_brightness(int max)
{
	int val;
	/* First write some sane value */
	backlight_write(max / 2, "brightness");
	/* Writing invalid values should fail and not change the value */
	igt_assert(backlight_write(-1, "brightness") < 0);
	backlight_read(&val, "brightness");
	igt_assert(val == max / 2);
	igt_assert(backlight_write(max + 1, "brightness") < 0);
	backlight_read(&val, "brightness");
	igt_assert(val == max / 2);
	igt_assert(backlight_write(INT_MAX, "brightness") < 0);
	backlight_read(&val, "brightness");
	igt_assert(val == max / 2);
}

static void test_fade(int max)
{
	int i;
	static const struct timespec ts = { .tv_sec = 0, .tv_nsec = FADESPEED*1000000 };

	/* Fade out, then in */
	for (i = max; i > 0; i -= max / FADESTEPS) {
		test_and_verify(i);
		nanosleep(&ts, NULL);
	}
	for (i = 0; i <= max; i += max / FADESTEPS) {
		test_and_verify(i);
		nanosleep(&ts, NULL);
	}
}

igt_main
{
	int max, old;

	igt_skip_on_simulation();

	igt_fixture {
		/* Get the max value and skip the whole test if sysfs interface not available */
		igt_skip_on(backlight_read(&old, "brightness"));
		igt_assert(backlight_read(&max, "max_brightness") > -1);
	}

	igt_subtest("basic-brightness")
		test_brightness(max);
	igt_subtest("bad-brightness")
		test_bad_brightness(max);
	igt_subtest("fade")
		test_fade(max);

	igt_fixture {
		/* Restore old brightness */
		backlight_write(old, "brightness");
	}
}
