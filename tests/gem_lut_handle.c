/*
 * Copyright © 2012,2013 Intel Corporation
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

/* Exercises the basic execbuffer using theh andle LUT interface */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"

#define BATCH_SIZE		(1024*1024)

#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

#define NORMAL 0
#define USE_LUT 0x1
#define BROKEN 0x2

static int exec(int fd, uint32_t handle, unsigned int flags)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];
	struct drm_i915_gem_relocation_entry gem_reloc[1];

	gem_reloc[0].offset = 1024;
	gem_reloc[0].delta = 0;
	gem_reloc[0].target_handle =
		!!(flags & USE_LUT) ^ !!(flags & BROKEN) ? 0 : handle;
	gem_reloc[0].read_domains = I915_GEM_DOMAIN_RENDER;
	gem_reloc[0].write_domain = 0;
	gem_reloc[0].presumed_offset = 0;

	gem_exec[0].handle = handle;
	gem_exec[0].relocation_count = 1;
	gem_exec[0].relocs_ptr = (uintptr_t) gem_reloc;
	gem_exec[0].alignment = 0;
	gem_exec[0].offset = 0;
	gem_exec[0].flags = 0;
	gem_exec[0].rsvd1 = 0;
	gem_exec[0].rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = 8;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = flags & USE_LUT ? LOCAL_I915_EXEC_HANDLE_LUT : 0;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	if (drmIoctl(fd,
		     DRM_IOCTL_I915_GEM_EXECBUFFER2,
		     &execbuf))
		return -errno;

	return 0;
}

static int many_exec(int fd, uint32_t batch, int num_exec, int num_reloc, unsigned flags)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 *gem_exec;
	struct drm_i915_gem_relocation_entry *gem_reloc;
	unsigned max_handle = batch;
	int ret, n;

	gem_exec = calloc(num_exec+1, sizeof(*gem_exec));
	gem_reloc = calloc(num_reloc, sizeof(*gem_reloc));
	igt_assert(gem_exec && gem_reloc);

	for (n = 0; n < num_exec; n++) {
		gem_exec[n].handle = gem_create(fd, 4096);
		if (gem_exec[n].handle > max_handle)
			max_handle = gem_exec[n].handle;
		gem_exec[n].relocation_count = 0;
		gem_exec[n].relocs_ptr = 0;
		gem_exec[n].alignment = 0;
		gem_exec[n].offset = 0;
		gem_exec[n].flags = 0;
		gem_exec[n].rsvd1 = 0;
		gem_exec[n].rsvd2 = 0;
	}

	gem_exec[n].handle = batch;
	gem_exec[n].relocation_count = num_reloc;
	gem_exec[n].relocs_ptr = (uintptr_t) gem_reloc;

	if (flags & USE_LUT)
		max_handle = num_exec + 1;
	max_handle++;

	for (n = 0; n < num_reloc; n++) {
		uint32_t target;

		if (flags & BROKEN) {
			target = -(rand() % 4096) - 1;
		} else {
			target = rand() % (num_exec + 1);
			if ((flags & USE_LUT) == 0)
				target = gem_exec[target].handle;
		}

		gem_reloc[n].offset = 1024;
		gem_reloc[n].delta = 0;
		gem_reloc[n].target_handle = target;
		gem_reloc[n].read_domains = I915_GEM_DOMAIN_RENDER;
		gem_reloc[n].write_domain = 0;
		gem_reloc[n].presumed_offset = 0;
	}

	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = num_exec + 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = 8;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = flags & USE_LUT ? LOCAL_I915_EXEC_HANDLE_LUT : 0;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	ret = drmIoctl(fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER2,
		       &execbuf);
	if (ret < 0)
		ret = -errno;

	for (n = 0; n < num_exec; n++)
		gem_close(fd, gem_exec[n].handle);

	free(gem_exec);
	free(gem_reloc);

	return ret;
}

#define fail(x) igt_assert((x) == -ENOENT)
#define pass(x) igt_assert((x) == 0)

igt_simple_main
{
	uint32_t batch[2] = {MI_BATCH_BUFFER_END};
	uint32_t handle;
	int fd, i;

	fd = drm_open_any();

	handle = gem_create(fd, 4096);
	gem_write(fd, handle, 0, batch, sizeof(batch));

	do_or_die(exec(fd, handle, NORMAL));
	fail(exec(fd, handle, BROKEN));

	igt_skip_on(exec(fd, handle, USE_LUT));

	do_or_die(exec(fd, handle, USE_LUT));
	fail(exec(fd, handle, USE_LUT | BROKEN));

	for (i = 2; i <= SLOW_QUICK(65536, 8); i *= 2) {
		if (many_exec(fd, handle, i+1, i+1, NORMAL) == -ENOSPC)
			break;

		pass(many_exec(fd, handle, i-1, i-1, NORMAL));
		pass(many_exec(fd, handle, i-1, i, NORMAL));
		pass(many_exec(fd, handle, i-1, i+1, NORMAL));
		pass(many_exec(fd, handle, i, i-1, NORMAL));
		pass(many_exec(fd, handle, i, i, NORMAL));
		pass(many_exec(fd, handle, i, i+1, NORMAL));
		pass(many_exec(fd, handle, i+1, i-1, NORMAL));
		pass(many_exec(fd, handle, i+1, i, NORMAL));
		pass(many_exec(fd, handle, i+1, i+1, NORMAL));

		fail(many_exec(fd, handle, i-1, i-1, NORMAL | BROKEN));
		fail(many_exec(fd, handle, i-1, i, NORMAL | BROKEN));
		fail(many_exec(fd, handle, i-1, i+1, NORMAL | BROKEN));
		fail(many_exec(fd, handle, i, i-1, NORMAL | BROKEN));
		fail(many_exec(fd, handle, i, i, NORMAL | BROKEN));
		fail(many_exec(fd, handle, i, i+1, NORMAL | BROKEN));
		fail(many_exec(fd, handle, i+1, i-1, NORMAL | BROKEN));
		fail(many_exec(fd, handle, i+1, i, NORMAL | BROKEN));
		fail(many_exec(fd, handle, i+1, i+1, NORMAL | BROKEN));

		pass(many_exec(fd, handle, i-1, i-1, USE_LUT));
		pass(many_exec(fd, handle, i-1, i, USE_LUT));
		pass(many_exec(fd, handle, i-1, i+1, USE_LUT));
		pass(many_exec(fd, handle, i, i-1, USE_LUT));
		pass(many_exec(fd, handle, i, i, USE_LUT));
		pass(many_exec(fd, handle, i, i+1, USE_LUT));
		pass(many_exec(fd, handle, i+1, i-1, USE_LUT));
		pass(many_exec(fd, handle, i+1, i, USE_LUT));
		pass(many_exec(fd, handle, i+1, i+1, USE_LUT));

		fail(many_exec(fd, handle, i-1, i-1, USE_LUT | BROKEN));
		fail(many_exec(fd, handle, i-1, i, USE_LUT | BROKEN));
		fail(many_exec(fd, handle, i-1, i+1, USE_LUT | BROKEN));
		fail(many_exec(fd, handle, i, i-1, USE_LUT | BROKEN));
		fail(many_exec(fd, handle, i, i, USE_LUT | BROKEN));
		fail(many_exec(fd, handle, i, i+1, USE_LUT | BROKEN));
		fail(many_exec(fd, handle, i+1, i-1, USE_LUT | BROKEN));
		fail(many_exec(fd, handle, i+1, i, USE_LUT | BROKEN));
		fail(many_exec(fd, handle, i+1, i+1, USE_LUT | BROKEN));
	}
}
