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
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"

/*
 * Testcase: Minmal bo_create and batchbuffer exec
 *
 * Originally this caught an kernel oops due to the unchecked assumption that
 * objects have size > 0.
 */

static uint32_t do_gem_create(int fd, int size, int *retval)
{
	struct drm_i915_gem_create create;
	int ret;

	create.handle = 0;
	create.size = (size + 4095) & -4096;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);
	igt_assert(retval || ret == 0);
	if (retval)
		*retval = errno;

	return create.handle;
}

#if 0
static int gem_exec(int fd, struct drm_i915_gem_execbuffer2 *execbuf)
{
	return drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, execbuf);
}
#endif

static void create0(int fd)
{
	int retval = 0;
	igt_info("trying to create a zero-length gem object\n");
	do_gem_create(fd, 0, &retval);
	igt_assert(retval == EINVAL);
}

#if 0
static void exec0(int fd)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec[1];
	uint32_t buf[2] = { MI_BATCH_BUFFER_END, 0 };

	/* Just try executing with a zero-length bo.
	 * We expect the kernel to either accept the nop batch, or reject it
	 * for the zero-length buffer, but never crash.
	 */

	exec[0].handle = gem_create(fd, 4096);
	gem_write(fd, exec[0].handle, 0, buf, sizeof(buf));
	exec[0].relocation_count = 0;
	exec[0].relocs_ptr = 0;
	exec[0].alignment = 0;
	exec[0].offset = 0;
	exec[0].flags = 0;
	exec[0].rsvd1 = 0;
	exec[0].rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = sizeof(buf);
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = 0;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	igt_info("trying to run an empty batchbuffer\n");
	gem_exec(fd, &execbuf);

	gem_close(fd, exec[0].handle);
}
#endif

igt_simple_main
{
	int fd;

	igt_skip_on_simulation();

	fd = drm_open_any();

	create0(fd);

	//exec0(fd);

	close(fd);
}
