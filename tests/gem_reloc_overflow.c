/*
 * Copyright © 2013 Google
 * Copyright © 2013 Intel Corporation
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
 *    Kees Cook <keescook@chromium.org>
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <malloc.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_gpu_tools.h"

/*
 * Testcase: Kernel relocation overflows are caught.
 */

int fd, entries, num;
size_t reloc_size;
uint32_t *handles;
struct drm_i915_gem_exec_object2 *execobjs;
struct drm_i915_gem_execbuffer2 execbuf = { 0 };
struct drm_i915_gem_relocation_entry *reloc;

uint32_t handle;
uint32_t batch_handle;


static void source_offset_tests(void)
{
	struct drm_i915_gem_relocation_entry single_reloc;

	igt_fixture {
		handle = gem_create(fd, 4096);

		execobjs[1].handle = batch_handle;
		execobjs[1].relocation_count = 0;
		execobjs[1].relocs_ptr = 0;

		execobjs[0].handle = handle;
		execobjs[0].relocation_count = 1;
		execobjs[0].relocs_ptr = (uintptr_t) &single_reloc;
		execbuf.buffer_count = 2;
	}

	igt_subtest("source-offset-end") {
		single_reloc.offset = 4096 - 4;
		single_reloc.delta = 0;
		single_reloc.target_handle = handle;
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;

		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) == 0);
	}

	igt_subtest("source-offset-big") {
		single_reloc.offset = 4096;
		single_reloc.delta = 0;
		single_reloc.target_handle = handle;
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;

		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) != 0);
		igt_assert(errno == EINVAL);
	}

	igt_subtest("source-offset-negative") {
		single_reloc.offset = (int64_t) -4;
		single_reloc.delta = 0;
		single_reloc.target_handle = handle;
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;

		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) != 0);
		igt_assert(errno == EINVAL);
	}

	igt_subtest("source-offset-unaligned") {
		single_reloc.offset = 1;
		single_reloc.delta = 0;
		single_reloc.target_handle = handle;
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;

		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) != 0);
		igt_assert(errno == EINVAL);
	}

	igt_fixture {
		execobjs[0].handle = batch_handle;
		execobjs[0].relocation_count = 0;
		execobjs[0].relocs_ptr = 0;

		execbuf.buffer_count = 1;
	}

	igt_subtest("batch-start-unaligend") {
		execbuf.batch_start_offset = 1;
		execbuf.batch_len = 8;

		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) != 0);
		igt_assert(errno == EINVAL);
	}

	igt_subtest("batch-end-unaligend") {
		execbuf.batch_start_offset = 0;
		execbuf.batch_len = 7;

		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) != 0);
		igt_assert(errno == EINVAL);
	}

	igt_fixture
		gem_close(fd, handle);
}

int main(int argc, char *argv[])
{
	int i;
	size_t total_actual = 0;
	unsigned int total_unsigned = 0;
	int total_signed = 0;
	igt_subtest_init(argc, argv);

	igt_fixture {
		int ring;
		uint32_t batch_data [2] = { MI_NOOP, MI_BATCH_BUFFER_END };

		fd = drm_open_any();

		/* Create giant reloc buffer area. */
		num = 257;
		entries = ((1ULL << 32) / (num - 1));
		reloc_size = entries * sizeof(struct drm_i915_gem_relocation_entry);
		reloc = mmap(NULL, reloc_size, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANON, -1, 0);
		igt_assert(reloc != MAP_FAILED);

		/* Allocate the handles we'll need to wrap. */
		handles = calloc(num, sizeof(*handles));
		for (i = 0; i < num; i++)
			handles[i] = gem_create(fd, 4096);

		if (intel_gen(intel_get_drm_devid(fd)) >= 6)
			ring = I915_EXEC_BLT;
		else
			ring = 0;

		/* Create relocation objects. */
		execobjs = calloc(num, sizeof(*execobjs));
		execbuf.buffers_ptr = (uintptr_t)execobjs;
		execbuf.batch_start_offset = 0;
		execbuf.batch_len = 8;
		execbuf.cliprects_ptr = 0;
		execbuf.num_cliprects = 0;
		execbuf.DR1 = 0;
		execbuf.DR4 = 0;
		execbuf.flags = ring;
		i915_execbuffer2_set_context_id(execbuf, 0);
		execbuf.rsvd2 = 0;

		batch_handle = gem_create(fd, 4096);

		gem_write(fd, batch_handle, 0, batch_data, sizeof(batch_data));
	}

	igt_subtest("invalid-address") {
		/* Attempt unmapped single entry. */
		execobjs[0].relocation_count = 1;
		execobjs[0].relocs_ptr = 0;
		execbuf.buffer_count = 1;

		errno = 0;
		ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
		igt_assert(errno == EFAULT);
	}

	igt_subtest("single-overflow") {
		/* Attempt single overflowed entry. */
		execobjs[0].relocation_count = (1 << 31);
		execobjs[0].relocs_ptr = (uintptr_t)reloc;
		execbuf.buffer_count = 1;

		errno = 0;
		ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
		igt_assert(errno == EINVAL);
	}

	igt_subtest("wrapped-overflow") {
		/* Attempt wrapped overflow entries. */
		for (i = 0; i < num; i++) {
			struct drm_i915_gem_exec_object2 *obj = &execobjs[i];
			obj->handle = handles[i];

			if (i == num - 1) {
				/* Wraps to 1 on last count. */
				obj->relocation_count = 1 - total_unsigned;
				obj->relocs_ptr = (uintptr_t)reloc;
			} else {
				obj->relocation_count = entries;
				obj->relocs_ptr = (uintptr_t)reloc;
			}

			total_unsigned += obj->relocation_count;
			total_signed += obj->relocation_count;
			total_actual += obj->relocation_count;
		}
		execbuf.buffer_count = num;

		errno = 0;
		ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
		igt_assert(errno == EINVAL);
	}

	source_offset_tests();

	igt_fixture {
		gem_close(fd, batch_handle);
		close(fd);
	}

	igt_exit();
}
