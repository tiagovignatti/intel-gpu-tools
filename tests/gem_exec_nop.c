/*
 * Copyright © 2011 Intel Corporation
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

#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include "drm.h"

#define LOCAL_I915_EXEC_NO_RELOC (1<<11)
#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

#define LOCAL_I915_EXEC_VEBOX (4<<0)

const uint32_t batch[2] = {MI_BATCH_BUFFER_END};
int device;

static void loop(int fd, uint32_t handle, unsigned ring_id, const char *ring_name)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];
	int count;

	gem_require_ring(fd, ring_id);

	memset(&gem_exec, 0, sizeof(gem_exec));
	gem_exec[0].handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 1;
	execbuf.flags = ring_id;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf)) {
		execbuf.flags = ring_id;
		do_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
	}
	gem_sync(fd, handle);

	for (count = 1; count <= SLOW_QUICK(1<<17, 1<<4); count <<= 1) {
		int loops = count;
		gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, 0);
		while (loops--)
			do_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
		gem_sync(fd, handle);
	}
}

igt_main
{
	uint32_t handle = 0;

	igt_fixture {
		device = drm_open_driver(DRIVER_INTEL);
		handle = gem_create(device, 4096);
		gem_write(device, handle, 0, batch, sizeof(batch));
	}

	igt_subtest("render")
		loop(device, handle, I915_EXEC_RENDER, "render");

	igt_subtest("bsd")
		loop(device, handle, I915_EXEC_BSD, "bsd");

	igt_subtest("blt")
		loop(device, handle, I915_EXEC_BLT, "blt");

	igt_subtest("vebox")
		loop(device, handle, LOCAL_I915_EXEC_VEBOX, "vebox");

	igt_fixture {
		gem_close(device, handle);
		close(device);
	}
}
