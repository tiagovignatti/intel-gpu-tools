/*
 * Copyright © 2011,2012 Intel Corporation
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
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

/*
 * Testcase: run a nop batch which is really big
 *
 * Mostly useful to stress-test the error-capture code
 */

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
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"

#define BATCH_SIZE		(1024*1024)

static int exec(int fd, uint32_t handle, uint32_t reloc_ofs)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];
	struct drm_i915_gem_relocation_entry gem_reloc[1];
	uint32_t tmp;
	int ret = 0;

	gem_reloc[0].offset = reloc_ofs;
	gem_reloc[0].delta = 0;
	gem_reloc[0].target_handle = handle;
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
	execbuf.flags = 0;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	ret = drmIoctl(fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER2,
		       &execbuf);
	gem_sync(fd, handle);

	gem_read(fd, handle, reloc_ofs, &tmp, 4);

	igt_assert(tmp == gem_reloc[0].presumed_offset);

	return ret;
}

igt_simple_main
{
	uint32_t batch[2] = {MI_BATCH_BUFFER_END};
	uint32_t handle;
	int fd;
	uint32_t reloc_ofs;
	unsigned batch_size;

	igt_skip_on_simulation();

	fd = drm_open_any();

	for (batch_size = BATCH_SIZE/4; batch_size <= BATCH_SIZE; batch_size += 4096) {
		handle = gem_create(fd, batch_size);
		gem_write(fd, handle, 0, batch, sizeof(batch));

		for (reloc_ofs = 4096; reloc_ofs < batch_size; reloc_ofs += 4096)
			igt_assert(exec(fd, handle, reloc_ofs) == 0);
	}

	gem_close(fd, handle);

	close(fd);
}
