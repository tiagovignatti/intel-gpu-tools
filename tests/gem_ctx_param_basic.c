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
 * Authors:
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 */

#include "igt.h"
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>

IGT_TEST_DESCRIPTION("Basic test for context set/get param input validation.");

int fd;
int32_t ctx;

#define LOCAL_I915_GEM_CONTEXT_GETPARAM       0x34
#define LOCAL_I915_GEM_CONTEXT_SETPARAM       0x35
#define LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM DRM_IOWR (DRM_COMMAND_BASE + LOCAL_I915_GEM_CONTEXT_GETPARAM, struct local_i915_gem_context_param)
#define LOCAL_IOCTL_I915_GEM_CONTEXT_SETPARAM DRM_IOWR (DRM_COMMAND_BASE + LOCAL_I915_GEM_CONTEXT_SETPARAM, struct local_i915_gem_context_param)

#define TEST_SUCCESS(ioc) \
	igt_assert(drmIoctl(fd, (ioc), &ctx_param) == 0);
#define TEST_FAIL(ioc, exp_errno) \
	igt_assert(drmIoctl(fd, (ioc), &ctx_param) < 0 && errno == exp_errno);

igt_main
{
	struct local_i915_gem_context_param ctx_param;

	memset(&ctx_param, 0, sizeof(ctx_param));

	igt_fixture {
		fd = drm_open_driver_render(DRIVER_INTEL);
		ctx = gem_context_create(fd);
	}

	ctx_param.param = LOCAL_CONTEXT_PARAM_BAN_PERIOD;

	igt_subtest("basic") {
		ctx_param.context = ctx;
		TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM);
		TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_SETPARAM);
	}

	igt_subtest("basic-default") {
		ctx_param.context = 0;
		TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM);
		TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_SETPARAM);
	}

	igt_subtest("invalid-ctx-get") {
		ctx_param.context = 2;
		TEST_FAIL(LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM, ENOENT);
	}

	igt_subtest("invalid-ctx-set") {
		ctx_param.context = ctx;
		TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM);
		ctx_param.context = 2;
		TEST_FAIL(LOCAL_IOCTL_I915_GEM_CONTEXT_SETPARAM, ENOENT);
	}

	igt_subtest("invalid-size-get") {
		ctx_param.context = ctx;
		ctx_param.size = 8;
		TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM);
		igt_assert(ctx_param.size == 0);
	}

	igt_subtest("invalid-size-set") {
		ctx_param.context = ctx;
		TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM);
		ctx_param.size = 8;
		TEST_FAIL(LOCAL_IOCTL_I915_GEM_CONTEXT_SETPARAM, EINVAL);
		ctx_param.size = 0;
	}

	ctx_param.param = LOCAL_CONTEXT_PARAM_BAN_PERIOD;

	igt_subtest("non-root-set") {
		igt_fork(child, 1) {
			igt_drop_root();

			ctx_param.context = ctx;
			TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM);
			ctx_param.value--;
			TEST_FAIL(LOCAL_IOCTL_I915_GEM_CONTEXT_SETPARAM, EPERM);
		}

		igt_waitchildren();
	}

	igt_subtest("root-set") {
		ctx_param.context = ctx;
		TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM);
		ctx_param.value--;
		TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_SETPARAM);
	}

	ctx_param.param = LOCAL_CONTEXT_PARAM_NO_ZEROMAP;

	igt_subtest("non-root-set-no-zeromap") {
		igt_fork(child, 1) {
			igt_drop_root();

			ctx_param.context = ctx;
			TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM);
			ctx_param.value--;
			TEST_FAIL(LOCAL_IOCTL_I915_GEM_CONTEXT_SETPARAM, EPERM);
		}

		igt_waitchildren();
	}

	igt_subtest("root-set-no-zeromap-enabled") {
		ctx_param.context = ctx;
		TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM);
		ctx_param.value = 1;
		TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_SETPARAM);
	}

	igt_subtest("root-set-no-zeromap-disabled") {
		ctx_param.context = ctx;
		TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM);
		ctx_param.value = 0;
		TEST_SUCCESS(LOCAL_IOCTL_I915_GEM_CONTEXT_SETPARAM);
	}

	/* NOTE: This testcase intentionally tests for the next free parameter
	 * to catch ABI extensions. Don't "fix" this testcase without adding all
	 * the tests for the new param first. */
	ctx_param.param = LOCAL_CONTEXT_PARAM_NO_ZEROMAP + 1;

	igt_subtest("invalid-param-get") {
		ctx_param.context = ctx;
		TEST_FAIL(LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM, EINVAL);
	}

	igt_subtest("invalid-param-set") {
		ctx_param.context = ctx;
		TEST_FAIL(LOCAL_IOCTL_I915_GEM_CONTEXT_SETPARAM, EINVAL);
	}

	igt_fixture
		close(fd);
}
