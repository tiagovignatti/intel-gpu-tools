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

#include "igt.h"
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

IGT_TEST_DESCRIPTION("Check that kernel relocation overflows are caught.");

/*
 * Testcase: Kernel relocation overflows are caught.
 */

int fd, entries, num;
struct drm_i915_gem_exec_object2 *obj;
struct drm_i915_gem_execbuffer2 execbuf;
struct drm_i915_gem_relocation_entry *reloc;

static uint32_t target_handle(void)
{
	return execbuf.flags & I915_EXEC_HANDLE_LUT ? 0 : obj[0].handle;
}

static void source_offset_tests(int devid, bool reloc_gtt)
{
	struct drm_i915_gem_relocation_entry single_reloc;
	const char *relocation_type;

	if (reloc_gtt)
		relocation_type = "reloc-gtt";
	else
		relocation_type = "reloc-cpu";

	igt_fixture {
		obj[1].relocation_count = 0;
		obj[1].relocs_ptr = 0;

		obj[0].relocation_count = 1;
		obj[0].relocs_ptr = (uintptr_t) &single_reloc;
		execbuf.buffer_count = 2;

		if (reloc_gtt) {
			gem_set_domain(fd, obj[0].handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
			relocation_type = "reloc-gtt";
		} else {
			gem_set_domain(fd, obj[0].handle, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
			relocation_type = "reloc-cpu";
		}
	}

	/* Special tests for 64b relocs. */
	igt_subtest_f("source-offset-page-stradle-gen8-%s", relocation_type) {
		igt_require(intel_gen(devid) >= 8);
		single_reloc.offset = 4096 - 4;
		single_reloc.delta = 0;
		single_reloc.target_handle = target_handle();
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;
		gem_execbuf(fd, &execbuf);

		single_reloc.delta = 1024;
		gem_execbuf(fd, &execbuf);
	}

	igt_subtest_f("source-offset-end-gen8-%s", relocation_type) {
		igt_require(intel_gen(devid) >= 8);
		single_reloc.offset = 8192 - 8;
		single_reloc.delta = 0;
		single_reloc.target_handle = target_handle();
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;
		gem_execbuf(fd, &execbuf);
	}

	igt_subtest_f("source-offset-overflow-gen8-%s", relocation_type) {
		igt_require(intel_gen(devid) >= 8);
		single_reloc.offset = 8192 - 4;
		single_reloc.delta = 0;
		single_reloc.target_handle = target_handle();
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);
	}

	/* Tests for old 4byte relocs on pre-gen8. */
	igt_subtest_f("source-offset-end-%s", relocation_type) {
		igt_require(intel_gen(devid) < 8);
		single_reloc.offset = 8192 - 4;
		single_reloc.delta = 0;
		single_reloc.target_handle = target_handle();
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;
		gem_execbuf(fd, &execbuf);
	}

	igt_subtest_f("source-offset-big-%s", relocation_type) {
		single_reloc.offset = 8192;
		single_reloc.delta = 0;
		single_reloc.target_handle = target_handle();
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);
	}

	igt_subtest_f("source-offset-negative-%s", relocation_type) {
		single_reloc.offset = (int64_t) -4;
		single_reloc.delta = 0;
		single_reloc.target_handle = target_handle();
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);
	}

	igt_subtest_f("source-offset-unaligned-%s", relocation_type) {
		single_reloc.offset = 1;
		single_reloc.delta = 0;
		single_reloc.target_handle = target_handle();
		single_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
		single_reloc.write_domain = I915_GEM_DOMAIN_RENDER;
		single_reloc.presumed_offset = 0;
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);
	}
}

static void reloc_tests(const char *suffix)
{
	uint64_t max_relocations;
	int i;

	max_relocations = min(ULONG_MAX, SIZE_MAX);
	max_relocations /= sizeof(struct drm_i915_gem_relocation_entry);
	igt_debug("Maximum allocable relocations: %'llu\n",
		  (long long)max_relocations);

	igt_subtest_f("invalid-address%s", suffix) {
		/* Attempt unmapped single entry. */
		obj[0].relocation_count = 1;
		obj[0].relocs_ptr = 0;
		execbuf.buffer_count = 1;

		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EFAULT);
	}

	igt_subtest_f("single-fault%s", suffix) {
		obj[0].relocation_count = entries + 1;
		execbuf.buffer_count = 1;

		/* out-of-bounds after */
		obj[0].relocs_ptr = (uintptr_t)reloc;
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EFAULT);

		/* out-of-bounds before */
		obj[0].relocs_ptr = (uintptr_t)(reloc - 1);
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EFAULT);
	}

	igt_fixture {
		obj[0].relocation_count = 0;
		obj[0].relocs_ptr = 0;

		execbuf.buffer_count = 1;

		/* Make sure the batch would succeed except for the thing we're
		 * testing. */
		igt_require(__gem_execbuf(fd, &execbuf) == 0);
	}

	igt_subtest_f("batch-start-unaligned%s", suffix) {
		execbuf.batch_start_offset = 1;
		execbuf.batch_len = 8;
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);
	}

	igt_subtest_f("batch-end-unaligned%s", suffix) {
		execbuf.batch_start_offset = 0;
		execbuf.batch_len = 7;
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);
	}

	igt_subtest_f("batch-both-unaligned%s", suffix) {
		execbuf.batch_start_offset = 1;
		execbuf.batch_len = 7;
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);
	}

	igt_fixture {
		/* Undo damage for next tests. */
		execbuf.batch_start_offset = 0;
		execbuf.batch_len = 0;
		igt_require(__gem_execbuf(fd, &execbuf) == 0);
	}

	igt_subtest_f("single-overflow%s", suffix) {
		if (*suffix) {
			igt_require_f(intel_get_avail_ram_mb() >
				      sizeof(struct drm_i915_gem_relocation_entry) * entries / (1024*1024),
				      "Test requires at least %'llu MiB, but only %'llu MiB of RAM available\n",
				      (long long)sizeof(struct drm_i915_gem_relocation_entry) * entries / (1024*1024),
				      (long long)intel_get_avail_ram_mb());
		}

		obj[0].relocs_ptr = (uintptr_t)reloc;
		obj[0].relocation_count = entries;
		execbuf.buffer_count = 1;
		gem_execbuf(fd, &execbuf);

		/* Attempt single overflowed entry. */
		obj[0].relocation_count = -1;
		igt_debug("relocation_count=%u\n",
				obj[0].relocation_count);
		if (max_relocations <= obj[0].relocation_count)
			igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);
		else
			igt_assert_eq(__gem_execbuf(fd, &execbuf), -EFAULT);

		if (max_relocations + 1 < obj[0].relocation_count) {
			obj[0].relocation_count = max_relocations + 1;
			igt_debug("relocation_count=%u\n",
				  obj[0].relocation_count);
			igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);

			obj[0].relocation_count = max_relocations - 1;
			igt_debug("relocation_count=%u\n",
				  obj[0].relocation_count);
			igt_assert_eq(__gem_execbuf(fd, &execbuf), -EFAULT);
		}
	}

	igt_subtest_f("wrapped-overflow%s", suffix) {
		if (*suffix) {
			igt_require_f(intel_get_avail_ram_mb() >
				      sizeof(struct drm_i915_gem_relocation_entry) * entries * num / (1024*1024),
				      "Test requires at least %'llu MiB, but only %'llu MiB of RAM available\n",
				      (long long)sizeof(struct drm_i915_gem_relocation_entry) * entries * num / (1024*1024),
				      (long long)intel_get_avail_ram_mb());
		}

		for (i = 0; i < num; i++) {
			struct drm_i915_gem_exec_object2 *o = &obj[i];

			o->relocs_ptr = (uintptr_t)reloc;
			o->relocation_count = entries;
		}
		execbuf.buffer_count = i;
		gem_execbuf(fd, &execbuf);

		obj[i-1].relocation_count = -1;
		igt_debug("relocation_count[%d]=%u\n",
			  i-1, obj[i-1].relocation_count);
                if (max_relocations <= obj[i-1].relocation_count)
                        igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);
		else
                        igt_assert_eq(__gem_execbuf(fd, &execbuf), -EFAULT);

		if (max_relocations < obj[i-1].relocation_count) {
			obj[i-1].relocation_count = max_relocations;
			igt_debug("relocation_count[%d]=%u\n",
				  i-1, obj[i-1].relocation_count);
			/* Whether the kernel reports the EFAULT for the
			 * invalid relocation array or EINVAL for the overflow
			 * in array size depends upon the order of the
			 * individual tests. From a consistency perspective
			 * EFAULT is preferred (i.e. using that relocation
			 * array by itself would cause EFAULT not EINVAL).
			 */
			igt_assert_eq(__gem_execbuf(fd, &execbuf), -EFAULT);

			obj[i-1].relocation_count = max_relocations - 1;
			igt_debug("relocation_count[%d]=%u\n",
				  i-1, obj[i-1].relocation_count);
			igt_assert_eq(__gem_execbuf(fd, &execbuf), -EFAULT);
		}

		obj[i-1].relocation_count = entries + 1;
		igt_debug("relocation_count[%d]=%u\n",
                          i-1, obj[i-1].relocation_count);
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EFAULT);

		obj[0].relocation_count = -1;
		if (max_relocations < obj[0].relocation_count) {
			execbuf.buffer_count = 1;
			gem_execbuf(fd, &execbuf);

			/* As outlined above, this is why EFAULT is preferred */
			obj[0].relocation_count = max_relocations;
			igt_debug("relocation_count[0]=%u\n",
				  obj[0].relocation_count);
			igt_assert_eq(__gem_execbuf(fd, &execbuf), -EFAULT);
		}
	}
}

static void buffer_count_tests(void)
{
	igt_subtest("buffercount-overflow") {
		for (int i = 0; i < num; i++) {
			obj[i].relocation_count = 0;
			obj[i].relocs_ptr = 0;
		}

		/* We only have num buffers actually, but the overflow will make
		 * sure we blow up the kernel before we blow up userspace. */
		execbuf.buffer_count = num;

		/* Make sure the basic thing would work first ... */
		gem_execbuf(fd, &execbuf);

		/* ... then be evil: Overflow of the pointer table (which has a
		 * bit of lead datastructures, so no + 1 needed to overflow). */
		execbuf.buffer_count = INT_MAX / sizeof(void *);
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);

		/* ... then be evil: Copying/allocating the array. */
		execbuf.buffer_count = UINT_MAX / sizeof(obj[0]) + 1;
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);
	}
}

igt_main
{
	int devid = 0;

	igt_fixture {
		uint32_t bbe = MI_BATCH_BUFFER_END;
		size_t reloc_size;

		fd = drm_open_driver(DRIVER_INTEL);
		devid = intel_get_drm_devid(fd);

		/* Create giant reloc buffer area. */
		num = 257;
		entries = ((1ULL << 32) / (num - 1));
		reloc_size = entries * sizeof(struct drm_i915_gem_relocation_entry);
		igt_assert((reloc_size & 4095) == 0);
		reloc = mmap(NULL, reloc_size + 2*4096, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANON, -1, 0);
		igt_assert(reloc != MAP_FAILED);
		igt_require_f(mlock(reloc, reloc_size) == 0,
			      "Tests require at least %'lu MiB of available memory\n",
			      reloc_size / (1024*1024));

		/* disable access before + after */
		mprotect(reloc, 4096, 0);
		reloc = (struct drm_i915_gem_relocation_entry *)((char *)reloc + 4096);
		mprotect(reloc + entries, 4096, 0);

		/* Allocate the handles we'll need to wrap. */
		intel_require_memory(num+1, 4096, CHECK_RAM);
		obj = calloc(num, sizeof(*obj));
		igt_assert(obj);

		/* First object is used for page crossing tests */
		obj[0].handle = gem_create(fd, 8192);
		gem_write(fd, obj[0].handle, 0, &bbe, sizeof(bbe));
		for (int i = 1; i < num; i++) {
			obj[i].handle = gem_create(fd, 4096);
			gem_write(fd, obj[i].handle, 0, &bbe, sizeof(bbe));
		}

		/* Create relocation objects. */
		memset(&execbuf, 0, sizeof(execbuf));
		execbuf.buffers_ptr = (uintptr_t)obj;
		execbuf.buffer_count = 1;
		execbuf.flags = I915_EXEC_HANDLE_LUT;
		if (__gem_execbuf(fd, &execbuf))
			execbuf.flags = 0;

		for (int i = 0; i < entries; i++) {
			reloc[i].target_handle = target_handle();
			reloc[i].offset = 1024;
			reloc[i].read_domains = I915_GEM_DOMAIN_INSTRUCTION;
			reloc[i].write_domain = 0;
		}
	}

	reloc_tests("");
	igt_disable_prefault();
	reloc_tests("-noprefault");
	igt_enable_prefault();

	source_offset_tests(devid, false);
	source_offset_tests(devid, true);

	buffer_count_tests();
}
