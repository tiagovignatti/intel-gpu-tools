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
 *    Ankitprasad Sharma <ankitprasad.r.sharma at intel.com>
 *
 */

/** @file gem_create.c
 *
 * This is a test for the extended and old gem_create ioctl, that
 * includes allocation of object from stolen memory and shmem.
 *
 * The goal is to simply ensure that basics work and invalid input
 * combinations are rejected.
 */

#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <getopt.h>

#include <drm.h>

#include "ioctl_wrappers.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"
#include "intel_chipset.h"
#include "igt_aux.h"
#include "drmtest.h"
#include "drm.h"
#include "i915_drm.h"

IGT_TEST_DESCRIPTION("This is a test for the extended & old gem_create ioctl,"
		     " that includes allocation of object from stolen memory"
		     " and shmem.");

#define CLEAR(s) memset(&s, 0, sizeof(s))
#define PAGE_SIZE 4096

struct local_i915_gem_create_v2 {
	uint64_t size;
	uint32_t handle;
	uint32_t pad;
#define I915_CREATE_PLACEMENT_STOLEN (1<<0)
	uint32_t flags;
} create;

#define LOCAL_IOCTL_I915_GEM_CREATE       DRM_IOWR(DRM_COMMAND_BASE + DRM_I915_GEM_CREATE, struct local_i915_gem_create_v2)

static void invalid_flag_test(int fd)
{
	int ret;

	gem_require_stolen_support(fd);

	create.handle = 0;
	create.size = PAGE_SIZE;
	create.flags = ~I915_CREATE_PLACEMENT_STOLEN;
	ret = drmIoctl(fd, LOCAL_IOCTL_I915_GEM_CREATE, &create);

	igt_assert(ret <= 0);

	create.flags = ~0;
	ret = drmIoctl(fd, LOCAL_IOCTL_I915_GEM_CREATE, &create);

	igt_assert(ret <= 0);
}

static void invalid_size_test(int fd)
{
	int handle;

	handle = __gem_create(fd, 0);
	igt_assert(!handle);
}

/*
 * Creating an object with non-aligned size and trying to access it with an
 * offset, which is greater than the requested size but smaller than the
 * object's last page boundary. pwrite here must be successful.
 */
static void valid_nonaligned_size(int fd)
{
	int handle;
	char buf[PAGE_SIZE];

	handle = gem_create(fd, PAGE_SIZE / 2);

	gem_write(fd, handle, PAGE_SIZE / 2, buf, PAGE_SIZE / 2);

	gem_close(fd, handle);
}

/*
 * Creating an object with non-aligned size and trying to access it with an
 * offset, which is greater than the requested size and larger than the
 * object's last page boundary. pwrite here must fail.
 */
static void invalid_nonaligned_size(int fd)
{
	int handle;
	char buf[PAGE_SIZE];
	struct drm_i915_gem_pwrite gem_pwrite;

	handle = gem_create(fd, PAGE_SIZE / 2);

	CLEAR(gem_pwrite);
	gem_pwrite.handle = handle;
	gem_pwrite.offset = PAGE_SIZE / 2;
	gem_pwrite.size = PAGE_SIZE;
	gem_pwrite.data_ptr = (uintptr_t)buf;
	/* This should fail. Hence cannot use gem_write. */
	igt_assert(drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite));

	gem_close(fd, handle);
}

igt_main
{
	int fd = -1;

	igt_skip_on_simulation();

	igt_fixture {
		fd = drm_open_any();
	}

	igt_subtest("stolen-invalid-flag")
		invalid_flag_test(fd);

	igt_subtest("create-invalid-size")
		invalid_size_test(fd);

	igt_subtest("create-valid-nonaligned")
		valid_nonaligned_size(fd);

	igt_subtest("create-invalid-nonaligned")
		invalid_nonaligned_size(fd);
}
