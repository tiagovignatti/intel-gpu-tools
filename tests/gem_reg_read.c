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

#include <stdio.h>
#include <string.h>
#include "i915_drm.h"
#include "drmtest.h"

struct local_drm_i915_reg_read {
	__u64 offset;
	__u64 val; /* Return value */
};

#define REG_READ_IOCTL DRM_IOWR(DRM_COMMAND_BASE + 0x31, struct local_drm_i915_reg_read)

static void handle_bad(int ret, int lerrno, int expected, const char *desc)
{
	if (ret != 0 && lerrno != expected) {
		fprintf(stderr, "%s - errno was %d, but should have been %d\n",
				desc, lerrno, expected);
		exit(EXIT_FAILURE);
	} else if (ret == 0) {
		fprintf(stderr, "%s - Command succeeded, but should have failed\n",
			desc);
		exit(EXIT_FAILURE);
	}
}

static uint64_t timer_query(int fd)
{
	struct local_drm_i915_reg_read read;
	int ret;

	read.offset = 0x2358;
	ret = drmIoctl(fd, REG_READ_IOCTL, &read);
	if (ret) {
		perror("positive test case failed: ");
		exit(EXIT_FAILURE);
	}

	return read.val;
}

int main(int argc, char *argv[])
{
	struct local_drm_i915_reg_read read;
	int ret, fd;
	uint64_t val;

	fd = drm_open_any();

	read.offset = 0x2358;
	ret = drmIoctl(fd, REG_READ_IOCTL, &read);
	if (errno == EINVAL)
		exit(77);
	else if (ret)
		exit(EXIT_FAILURE);

	val = timer_query(fd);
	sleep(1);
	if (timer_query(fd) == val) {
		fprintf(stderr, "Timer isn't moving, probably busted\n");
		exit(EXIT_FAILURE);
	}

	/* bad reg */
	read.offset = 0x12345678;
	ret = drmIoctl(fd, REG_READ_IOCTL, &read);
	handle_bad(ret, errno, EINVAL, "bad register");

	close(fd);

	exit(EXIT_SUCCESS);
}
