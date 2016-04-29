/*
 * Copyright © 2016 Intel Corporation
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
 */

#include <time.h>

#include "igt.h"

IGT_TEST_DESCRIPTION("Basic check of flushing after batches");

#define UNCACHED 0
#define COHERENT 1

static void run(int fd, unsigned ring, int nchild, unsigned flags, int timeout)
{
	const int gen = intel_gen(intel_get_drm_devid(fd));

	gem_require_ring(fd, ring);
	igt_skip_on_f(gen == 6 && (ring & ~(3<<13)) == I915_EXEC_BSD,
			"MI_STORE_DATA broken on gen6 bsd\n");

	igt_fork(child, nchild) {
		const uint32_t bbe = MI_BATCH_BUFFER_END;
		struct drm_i915_gem_exec_object2 obj[2];
		struct drm_i915_gem_relocation_entry reloc[1024];
		struct drm_i915_gem_execbuffer2 execbuf;
		unsigned long cycles = 0;
		uint32_t *ptr;
		uint32_t *map;
		int i;

		memset(obj, 0, sizeof(obj));
		obj[0].handle = gem_create(fd, 4096);
		obj[0].flags |= EXEC_OBJECT_WRITE;

		gem_set_caching(fd, obj[0].handle, !!(flags & COHERENT));
		map = gem_mmap__cpu(fd, obj[0].handle, 0, 4096, PROT_WRITE);

		gem_set_domain(fd, obj[0].handle,
				I915_GEM_DOMAIN_CPU,
				I915_GEM_DOMAIN_CPU);
		for (i = 0; i < 1024; i++)
			map[i] = 0xabcdabcd;

		gem_set_domain(fd, obj[0].handle,
				I915_GEM_DOMAIN_GTT,
				I915_GEM_DOMAIN_GTT);

		memset(&execbuf, 0, sizeof(execbuf));
		execbuf.buffers_ptr = (uintptr_t)obj;
		execbuf.buffer_count = 2;
		execbuf.flags = ring | (1 << 11) | (1<<12);
		if (gen < 6)
			execbuf.flags |= I915_EXEC_SECURE;

		obj[1].handle = gem_create(fd, 1024*64);
		gem_write(fd, obj[1].handle, 0, &bbe, sizeof(bbe));
		igt_require(__gem_execbuf(fd, &execbuf) == 0);

		obj[1].relocation_count = 1;

		ptr = gem_mmap__wc(fd, obj[1].handle, 0, 64*1024,
				PROT_WRITE | PROT_READ);
		gem_set_domain(fd, obj[1].handle,
				I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);

		memset(reloc, 0, sizeof(reloc));
		for (i = 0; i < 1024; i++) {
			uint64_t offset;
			uint32_t *b = &ptr[16 * i];

			reloc[i].presumed_offset = obj[0].offset;
			reloc[i].offset = (b - ptr + 1) * sizeof(*ptr);
			reloc[i].delta = i * sizeof(uint32_t);
			reloc[i].read_domains = I915_GEM_DOMAIN_INSTRUCTION;
			reloc[i].write_domain = I915_GEM_DOMAIN_INSTRUCTION;

			offset = obj[0].offset + reloc[i].delta;
			*b++ = MI_STORE_DWORD_IMM | (gen < 6 ? 1 << 22 : 0);
			if (gen >= 8) {
				*b++ = offset;
				*b++ = offset >> 32;
			} else if (gen >= 4) {
				*b++ = 0;
				*b++ = offset;
				reloc[i].offset += sizeof(*ptr);
			} else {
				b[-1] -= 1;
				*b++ = offset;
			}
			*b++ = i;
			*b++ = MI_BATCH_BUFFER_END;
		}
		munmap(ptr, 64*1024);

		igt_timeout(timeout) {
			i = cycles++ % 1024;

			obj[1].relocs_ptr = (uintptr_t)&reloc[i];
			execbuf.batch_start_offset =  64*i;

			gem_execbuf(fd, &execbuf);
			gem_sync(fd, obj[0].handle);

			if (!(flags & COHERENT) && !gem_has_llc(fd))
				igt_clflush_range(&map[i], sizeof(map[i]));

			igt_assert_eq_u32(map[i], i);

			map[i] = 0xdeadbeef;
			if (!(flags & COHERENT))
				igt_clflush_range(&map[i], sizeof(map[i]));
		}
		igt_info("Child[%d]: %lu cycles\n", child, cycles);

		gem_close(fd, obj[1].handle);

		munmap(map, 4096);
		gem_close(fd, obj[0].handle);
	}
	igt_waitchildren();
}

igt_main
{
	const struct intel_execution_engine *e;
	const int ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	int fd = -1;

	igt_skip_on_simulation();

	igt_fixture {
		igt_require(igt_setup_clflush());
		fd = drm_open_driver(DRIVER_INTEL);
	}

	igt_fork_hang_detector(fd);

	for (e = intel_execution_engines; e->name; e++) {
		igt_subtest_f("%suc-%s",
			      e->exec_id == 0 ? "basic-" : "", e->name)
			run(fd, e->exec_id | e->flags, ncpus, UNCACHED, 10);

		igt_subtest_f("%swb-%s",
			      e->exec_id == 0 ? "basic-" : "", e->name)
			run(fd, e->exec_id | e->flags, ncpus, COHERENT, 10);
	}

	igt_stop_hang_detector();

	igt_fixture
		close(fd);
}
