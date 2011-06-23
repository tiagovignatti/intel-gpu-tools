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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"

#define MI_BATCH_BUFFER_END	(0xA<<23)

static uint32_t gem_create(int fd, int size)
{
	struct drm_i915_gem_create create;
	int ret;

	create.handle = 0;
	create.size = (size + 4095) & -4096;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);
	assert(ret == 0);

	return create.handle;
}

static void gem_write(int fd,
		      uint32_t handle, uint32_t offset,
		      const void *src, int length)
{
	struct drm_i915_gem_pwrite arg;
	int ret;

	arg.handle = handle;
	arg.offset = offset;
	arg.size = length;
	arg.data_ptr = (uintptr_t)src;

	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &arg);
	assert(ret == 0);
}

static int gem_exec(int fd, struct drm_i915_gem_execbuffer2 *execbuf)
{
	return drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, execbuf);
}

static void gem_close(int fd, uint32_t handle)
{
	struct drm_gem_close close;
	int ret;

	close.handle = handle;
	ret = drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &close);
	assert(ret == 0);
}

static void exec0(int fd)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec[2];
	uint32_t buf[2] = { MI_BATCH_BUFFER_END };

	/* Just try executing with a zero-length bo.
	 * We expect the kernel to either accept the nop batch, or reject it
	 * for the zero-length buffer, but never crash.
	 */
	exec[0].handle = gem_create(fd, 0);
	exec[0].relocation_count = 0;
	exec[0].relocs_ptr = 0;
	exec[0].alignment = 0;
	exec[0].offset = 0;
	exec[0].flags = 0;
	exec[0].rsvd1 = 0;
	exec[0].rsvd2 = 0;

	exec[1].handle = gem_create(fd, 4096);
	gem_write(fd, exec[1].handle, 0, buf, sizeof(buf));
	exec[1].relocation_count = 0;
	exec[1].relocs_ptr = 0;
	exec[1].alignment = 0;
	exec[1].offset = 0;
	exec[1].flags = 0;
	exec[1].rsvd1 = 0;
	exec[1].rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)exec;
	execbuf.buffer_count = 2;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = sizeof(buf);
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = 0;
	execbuf.rsvd1 = 0;
	execbuf.rsvd2 = 0;

	gem_exec(fd, &execbuf);

	gem_close(fd, exec[0].handle);
	gem_close(fd, exec[1].handle);
}

int main(int argc, char **argv)
{
	int fd;

	fd = drm_open_any();

	exec0(fd);

	close(fd);

	return 0;
}
