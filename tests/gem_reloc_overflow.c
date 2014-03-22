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
 *    Rafael Barbalho <rafael.barbalho@intel.com>
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
#include "ioctl_wrappers.h"
#include "intel_chipset.h"
#include "drmtest.h"
#include "intel_io.h"

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

static void source_offset_tests(int devid, bool reloc_gtt)
{
	struct drm_i915_gem_relocation_entry single_reloc;
	void *dst_gtt;
	const char *relocation_type;

	if (reloc_gtt)
		relocation_type = "reloc-gtt";
	else
		relocation_type = "reloc-cpu";

	igt_fixture {
		handle = gem_create(fd, 8192);

		execobjs[1].handle = batch_handle;
		execobjs[1].relocation_count = 0;
		execobjs[1].relocs_ptr = 0;

		execobjs[0].handle = handle;
		execobjs[0].relocation_count = 1;
		execobjs[0].relocs_ptr = (uintptr_t) &single_reloc;
		execbuf.buffer_count = 2;

		if (reloc_gtt) {
			dst_gtt = gem_mmap(fd, handle, 8192, PROT_READ | PROT_WRITE);
			igt_assert(dst_gtt != MAP_FAILED);
			gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
			memset(dst_gtt, 0, 8192);
			munmap(dst_gtt, 8192);
			relocation_type = "reloc-gtt";
		} else {
			relocation_type = "reloc-cpu";
		}
	}

	/* Special tests for 64b relocs. */
	igt_subtest_f("source-offset-page-stradle-gen8-%s", relocation_type) {
		igt_require(intel_gen(devid) >= 8);
		single_reloc.offset = 4096 - 4;
		single_reloc.delta = 0;
		single_reloc.target_handle = handle;
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;

		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) == 0);
		single_reloc.delta = 1024;
		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) == 0);
	}

	igt_subtest_f("source-offset-end-gen8-%s", relocation_type) {
		igt_require(intel_gen(devid) >= 8);
		single_reloc.offset = 8192 - 8;
		single_reloc.delta = 0;
		single_reloc.target_handle = handle;
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;

		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) == 0);
	}

	igt_subtest_f("source-offset-overflow-gen8-%s", relocation_type) {
		igt_require(intel_gen(devid) >= 8);
		single_reloc.offset = 8192 - 4;
		single_reloc.delta = 0;
		single_reloc.target_handle = handle;
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;

		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) != 0);
		igt_assert(errno == EINVAL);
	}

	/* Tests for old 4byte relocs on pre-gen8. */
	igt_subtest_f("source-offset-end-%s", relocation_type) {
		igt_require(intel_gen(devid) < 8);
		single_reloc.offset = 8192 - 4;
		single_reloc.delta = 0;
		single_reloc.target_handle = handle;
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;

		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) == 0);
	}

	igt_subtest_f("source-offset-big-%s", relocation_type) {
		single_reloc.offset = 8192;
		single_reloc.delta = 0;
		single_reloc.target_handle = handle;
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;

		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) != 0);
		igt_assert(errno == EINVAL);
	}

	igt_subtest_f("source-offset-negative-%s", relocation_type) {
		single_reloc.offset = (int64_t) -4;
		single_reloc.delta = 0;
		single_reloc.target_handle = handle;
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;

		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) != 0);
		igt_assert(errno == EINVAL);
	}

	igt_subtest_f("source-offset-unaligned-%s", relocation_type) {
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
		gem_close(fd, handle);
	}
}

static void reloc_tests(void)
{
	int i;
	unsigned int total_unsigned = 0;

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

	igt_fixture {
		execobjs[0].handle = batch_handle;
		execobjs[0].relocation_count = 0;
		execobjs[0].relocs_ptr = 0;

		execbuf.buffer_count = 1;

		/* Make sure the batch would succeed except for the thing we're
		 * testing. */
		execbuf.batch_start_offset = 0;
		execbuf.batch_len = 8;
		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) == 0);
	}

	igt_subtest("batch-start-unaligned") {
		execbuf.batch_start_offset = 1;
		execbuf.batch_len = 8;

		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) != 0);
		igt_assert(errno == EINVAL);
	}

	igt_subtest("batch-end-unaligned") {
		execbuf.batch_start_offset = 0;
		execbuf.batch_len = 7;

		igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) != 0);
		igt_assert(errno == EINVAL);
	}

	igt_fixture {
		/* Undo damage for next tests. */
		execbuf.batch_start_offset = 0;
		execbuf.batch_len = 8;
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
		}
		execbuf.buffer_count = num;

		errno = 0;
		ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
		igt_assert(errno == EINVAL);
	}
}

static void buffer_count_tests(void)
{
	igt_subtest("buffercount-overflow") {
		for (int i = 0; i < num; i++) {
			execobjs[i].relocation_count = 0;
			execobjs[i].relocs_ptr = 0;
			execobjs[i].handle = handles[i];
		}

		execobjs[0].relocation_count = 0;
		execobjs[0].relocs_ptr = 0;
		/* We only have num buffers actually, but the overflow will make
		 * sure we blow up the kernel before we blow up userspace. */
		execbuf.buffer_count = num;

		/* Put a real batch at the end. */
		execobjs[num - 1].handle = batch_handle;

		/* Make sure the basic thing would work first ... */
		errno = 0;
		ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
		igt_assert(errno == 0);

		/* ... then be evil: Overflow of the pointer table (which has a
		 * bit of lead datastructures, so no + 1 needed to overflow). */
		execbuf.buffer_count = INT_MAX / sizeof(void *);

		errno = 0;
		ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
		igt_assert(errno == EINVAL);

		/* ... then be evil: Copying/allocating the array. */
		execbuf.buffer_count = UINT_MAX / sizeof(execobjs[0]) + 1;

		errno = 0;
		ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
		igt_assert(errno == EINVAL);
	}
}

igt_main
{
	int devid = 0;

	igt_fixture {
		int ring;
		uint32_t batch_data [2] = { MI_NOOP, MI_BATCH_BUFFER_END };

		fd = drm_open_any();

		devid = intel_get_drm_devid(fd);

		/* Create giant reloc buffer area. */
		num = 257;
		entries = ((1ULL << 32) / (num - 1));
		reloc_size = entries * sizeof(struct drm_i915_gem_relocation_entry);
		reloc = mmap(NULL, reloc_size, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANON, -1, 0);
		igt_assert(reloc != MAP_FAILED);

		/* Allocate the handles we'll need to wrap. */
		handles = calloc(num, sizeof(*handles));
		for (int i = 0; i < num; i++)
			handles[i] = gem_create(fd, 4096);

		if (intel_gen(devid) >= 6)
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

	reloc_tests();

	source_offset_tests(devid, false);
	source_offset_tests(devid, true);

	buffer_count_tests();

	igt_fixture {
		gem_close(fd, batch_handle);
		close(fd);
	}
}
