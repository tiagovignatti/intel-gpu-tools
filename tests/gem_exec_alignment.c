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
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

/* Exercises the basic execbuffer using object alignments */

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
#include "igt_debugfs.h"
#include "drmtest.h"

IGT_TEST_DESCRIPTION("Exercises the basic execbuffer using object alignments");

static int __gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *eb)
{
	return drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, eb);
}

igt_simple_main
{
	struct drm_i915_gem_exec_object2 execobj;
	struct drm_i915_gem_execbuffer2 execbuf;
	uint32_t batch = MI_BATCH_BUFFER_END;
	int non_pot;
	int fd;

	igt_skip_on_simulation();

	fd = drm_open_any();

	memset(&execobj, 0, sizeof(execobj));
	memset(&execbuf, 0, sizeof(execbuf));

	execobj.handle = gem_create(fd, 4096);
	gem_write(fd, execobj.handle, 0, &batch, sizeof(batch));

	execbuf.buffers_ptr = (uintptr_t)&execobj;
	execbuf.buffer_count = 1;

	gem_execbuf(fd, &execbuf);

	execobj.alignment = 3*4096;
	non_pot = __gem_execbuf(fd, &execbuf) == 0;
	igt_debug("execbuffer() accepts non-power-of-two alignment? %s\n",
		  non_pot ? "yes" : "no");

	for (execobj.alignment = 4096;
	     execobj.alignment <= 64<<20;
	     execobj.alignment += 4096) {
		if (!non_pot && execobj.alignment & -execobj.alignment)
			continue;

		gem_execbuf(fd, &execbuf);
		igt_assert_eq(execobj.offset % execobj.alignment, 0);
	}
}
