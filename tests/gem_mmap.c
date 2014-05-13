/*
 * Copyright © 2008 Intel Corporation
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
 *    Eric Anholt <eric@anholt.net>
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
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"

#define OBJECT_SIZE 16384
int fd;
int handle;

igt_main
{
	struct drm_i915_gem_mmap arg;
	uint8_t expected[OBJECT_SIZE];
	uint8_t buf[OBJECT_SIZE];
	uint8_t *addr;
	int ret;

	igt_fixture
		fd = drm_open_any();

	igt_subtest("bad-object") {
		memset(&arg, 0, sizeof(arg));
		arg.handle = 0x10101010;
		arg.offset = 0;
		arg.size = 4096;
		ret = ioctl(fd, DRM_IOCTL_I915_GEM_MMAP, &arg);
		igt_assert(ret == -1 && errno == ENOENT);
	}

	igt_fixture
		handle = gem_create(fd, OBJECT_SIZE);

	igt_subtest("new-object") {
		arg.handle = handle;
		arg.offset = 0;
		arg.size = OBJECT_SIZE;
		ret = ioctl(fd, DRM_IOCTL_I915_GEM_MMAP, &arg);
		igt_assert(ret == 0);
		addr = (uint8_t *)(uintptr_t)arg.addr_ptr;

		igt_info("Testing contents of newly created object.\n");
		memset(expected, 0, sizeof(expected));
		igt_assert(memcmp(addr, expected, sizeof(expected)) == 0);

		igt_info("Testing coherency of writes and mmap reads.\n");
		memset(buf, 0, sizeof(buf));
		memset(buf + 1024, 0x01, 1024);
		memset(expected + 1024, 0x01, 1024);
		gem_write(fd, handle, 0, buf, OBJECT_SIZE);
		igt_assert(memcmp(buf, addr, sizeof(buf)) == 0);

		igt_info("Testing that mapping stays after close\n");
		gem_close(fd, handle);
		igt_assert(memcmp(buf, addr, sizeof(buf)) == 0);

		igt_info("Testing unmapping\n");
		munmap(addr, OBJECT_SIZE);
	}

	igt_fixture
		close(fd);
}
