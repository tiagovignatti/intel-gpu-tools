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
#include <errno.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"

struct local_drm_i915_reg_read {
	__u64 offset;
	__u64 val; /* Return value */
};

#define REG_READ_IOCTL DRM_IOWR(DRM_COMMAND_BASE + 0x31, struct local_drm_i915_reg_read)

static uint64_t timer_query(int fd)
{
	struct local_drm_i915_reg_read reg_read;

	reg_read.offset = 0x2358;
	if (drmIoctl(fd, REG_READ_IOCTL, &reg_read)) {
		perror("positive test case failed: ");
		igt_fail(1);
	}

	return reg_read.val;
}

igt_simple_main
{
	struct local_drm_i915_reg_read reg_read;
	int fd, ret;

	fd = drm_open_any();

	reg_read.offset = 0x2358;
	ret = drmIoctl(fd, REG_READ_IOCTL, &reg_read);
	igt_assert(ret == 0 || errno == EINVAL);
	igt_require(ret == 0);

	reg_read.val = timer_query(fd);
	sleep(1);
	/* Check that timer is moving and isn't busted. */
	igt_assert(timer_query(fd) != reg_read.val);

	/* bad reg */
	reg_read.offset = 0x12345678;
	ret = drmIoctl(fd, REG_READ_IOCTL, &reg_read);

	igt_assert(ret != 0 && errno == EINVAL);

	close(fd);
}
