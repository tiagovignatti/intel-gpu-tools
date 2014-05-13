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
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
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
#include "intel_io.h"

#define OBJECT_SIZE (1024*1024)
#define TEST_STRIDE (1024*4)

/**
 * Testcase: Check set_tiling vs pwrite coherency
 */

igt_simple_main
{
	int fd;
	uint32_t *ptr;
	uint32_t data[OBJECT_SIZE/4];
	int i;
	uint32_t handle;

	igt_skip_on_simulation();

	fd = drm_open_any();

	for (i = 0; i < OBJECT_SIZE/4; i++)
		data[i] = i;

	handle = gem_create(fd, OBJECT_SIZE);
	ptr = gem_mmap(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);
	igt_assert(ptr);

	gem_set_tiling(fd, handle, I915_TILING_X, TEST_STRIDE);

	/* touch it */
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
	*ptr = 0xdeadbeef;

	igt_info("testing pwrite on tiled buffer\n");
	gem_write(fd, handle, 0, data, OBJECT_SIZE);
	memset(data, 0, OBJECT_SIZE);
	gem_read(fd, handle, 0, data, OBJECT_SIZE);
	for (i = 0; i < OBJECT_SIZE/4; i++)
		igt_assert(i == data[i]);

	/* touch it before changing the tiling, so that the fence sticks around */
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
	*ptr = 0xdeadbeef;

	gem_set_tiling(fd, handle, I915_TILING_NONE, 0);

	igt_info("testing pwrite on untiled, but still fenced buffer\n");
	gem_write(fd, handle, 0, data, OBJECT_SIZE);
	memset(data, 0, OBJECT_SIZE);
	gem_read(fd, handle, 0, data, OBJECT_SIZE);
	for (i = 0; i < OBJECT_SIZE/4; i++)
		igt_assert(i == data[i]);

	munmap(ptr, OBJECT_SIZE);

	close(fd);
}
