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
#include <time.h>

static int __gem_context_create(int fd, struct drm_i915_gem_context_create *arg)
{
	int ret = 0;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, arg))
		ret = -errno;
	return ret;
}

static double elapsed(const struct timespec *start,
		      const struct timespec *end)
{
	return (end->tv_sec - start->tv_sec) + 1e-9*(end->tv_nsec - start->tv_nsec);
}

static void active(int fd, int timeout)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	struct timespec start, end;
	unsigned count = 0;

	memset(&obj, 0, sizeof(obj));
	obj.handle = gem_create(fd, 4096);
	gem_write(fd, obj.handle, 0, &bbe, sizeof(bbe));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&obj;
	execbuf.buffer_count = 1;

	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		do {

			execbuf.rsvd1 = gem_context_create(fd);
			gem_execbuf(fd, &execbuf);
			gem_context_destroy(fd, execbuf.rsvd1);
		} while (++count & 1023);
		clock_gettime(CLOCK_MONOTONIC, &end);
	} while (elapsed(&start, &end) < timeout);

	gem_sync(fd, obj.handle);
	clock_gettime(CLOCK_MONOTONIC, &end);
	igt_info("Context creation + execution: %.3f us\n",
		 elapsed(&start, &end) / count *1e6);

	gem_close(fd, obj.handle);
}

igt_main
{
	struct drm_i915_gem_context_create create;
	int fd;

	igt_fixture {
		fd = drm_open_driver_render(DRIVER_INTEL);

		memset(&create, 0, sizeof(create));
		igt_require(__gem_context_create(fd, &create) == 0);
		gem_context_destroy(fd, create.ctx_id);
	}

	igt_subtest("basic") {
		memset(&create, 0, sizeof(create));
		create.ctx_id = rand();
		create.pad = 0;
		igt_assert_eq(__gem_context_create(fd, &create), 0);
		igt_assert(create.ctx_id != 0);
		gem_context_destroy(fd, create.ctx_id);
	}

	igt_subtest("invalid-pad") {
		memset(&create, 0, sizeof(create));
		create.ctx_id = rand();
		create.pad = 1;
		igt_assert_eq(__gem_context_create(fd, &create), -EINVAL);
	}

	igt_subtest("active")
		active(fd, 20);

	igt_fixture
		close(fd);
}
