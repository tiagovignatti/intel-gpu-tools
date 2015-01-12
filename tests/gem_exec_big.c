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
#include "igt_aux.h"

IGT_TEST_DESCRIPTION("Run a large nop batch to stress test the error capture"
		     " code.");

static void exec1(int fd, uint32_t handle, uint32_t reloc_ofs, unsigned flags)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];
	struct drm_i915_gem_relocation_entry gem_reloc[1];
	uint32_t tmp;

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
	execbuf.flags = flags;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	/* Avoid hitting slowpaths in the reloc processing which might yield a
	 * presumed_offset of -1. Happens when the batch is still busy from the
	 * last round. */
	gem_sync(fd, handle);

	gem_execbuf(fd, &execbuf);

	igt_warn_on(gem_reloc[0].presumed_offset == -1);

	gem_read(fd, handle, reloc_ofs, &tmp, 4);
	igt_assert_eq(tmp, gem_reloc[0].presumed_offset);
}

static void execN(int fd, uint32_t handle, uint32_t batch_size, unsigned flags)
{
#define reloc_ofs(N, T) ((((N)+1) << 12) - 4*(1 + ((N) == ((T)-1))))
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];
	struct drm_i915_gem_relocation_entry *gem_reloc;
	int n, nreloc = batch_size >> 12;
	uint32_t tmp;

	gem_reloc = calloc(nreloc, sizeof(*gem_reloc));
	igt_assert(gem_reloc);

	for (n = 0; n < nreloc; n++) {
		gem_reloc[n].offset = reloc_ofs(n, nreloc);
		gem_reloc[n].target_handle = handle;
		gem_reloc[n].read_domains = I915_GEM_DOMAIN_RENDER;
	}

	memset(gem_exec, 0, sizeof(gem_exec));
	gem_exec[0].handle = handle;
	gem_exec[0].relocation_count = nreloc;
	gem_exec[0].relocs_ptr = (uintptr_t)gem_reloc;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = 8;
	execbuf.flags = flags;

	/* Avoid hitting slowpaths in the reloc processing which might yield a
	 * presumed_offset of -1. Happens when the batch is still busy from the
	 * last round. */
	gem_sync(fd, handle);

	gem_execbuf(fd, &execbuf);
	for (n = 0; n < nreloc; n++)
		igt_warn_on(gem_reloc[n].presumed_offset == -1);

	for (n = 0; n < nreloc; n++) {
		gem_read(fd, handle, reloc_ofs(n, nreloc), &tmp, 4);
		igt_assert_eq(tmp, gem_reloc[n].presumed_offset);
	}

	free(gem_reloc);
#undef reloc_ofs
}

igt_simple_main
{
	uint32_t batch[2] = {MI_BATCH_BUFFER_END};
	int fd;
	uint32_t reloc_ofs;
	unsigned batch_size;
	int max;

	fd = drm_open_any();
	max = 3 * gem_aperture_size(fd) / 4;

	intel_require_memory(1, max, CHECK_RAM);

	for (batch_size = 4096; batch_size <= max; ) {
		uint32_t handle = gem_create(fd, batch_size);
		gem_write(fd, handle, 0, batch, sizeof(batch));

		for (reloc_ofs = 4096; reloc_ofs < batch_size; reloc_ofs += 4096) {
			igt_debug("batch_size %u, reloc_ofs %u\n",
				  batch_size, reloc_ofs);
			exec1(fd, handle, reloc_ofs, 0);
			exec1(fd, handle, reloc_ofs, I915_EXEC_SECURE);
		}

		igt_debug("batch_size %u, all %d relocs\n",
			  batch_size, batch_size >> 12);
		execN(fd, handle, batch_size, 0);
		execN(fd, handle, batch_size, I915_EXEC_SECURE);

		gem_madvise(fd, handle, I915_MADV_DONTNEED);

		if (batch_size < max && 2*batch_size > max)
			batch_size = max;
		else
			batch_size *= 2;
	}

	close(fd);
}
