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
#define WRITE 2
#define KERNEL 4
#define SET_DOMAIN 8
#define INTERRUPTIBLE 16

static void run(int fd, unsigned ring, int nchild, int timeout,
		unsigned flags)
{
	const int gen = intel_gen(intel_get_drm_devid(fd));

	igt_fork(child, nchild) {
		const uint32_t bbe = MI_BATCH_BUFFER_END;
		struct drm_i915_gem_exec_object2 obj[3];
		struct drm_i915_gem_relocation_entry reloc0[1024];
		struct drm_i915_gem_relocation_entry reloc1[1024];
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
		execbuf.buffer_count = 3;
		execbuf.flags = ring | (1 << 11) | (1<<12);
		if (gen < 6)
			execbuf.flags |= I915_EXEC_SECURE;

		obj[1].handle = gem_create(fd, 1024*64);
		obj[2].handle = gem_create(fd, 1024*64);
		gem_write(fd, obj[2].handle, 0, &bbe, sizeof(bbe));
		igt_require(__gem_execbuf(fd, &execbuf) == 0);

		obj[1].relocation_count = 1;
		obj[2].relocation_count = 1;

		ptr = gem_mmap__wc(fd, obj[1].handle, 0, 64*1024,
				PROT_WRITE | PROT_READ);
		gem_set_domain(fd, obj[1].handle,
				I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);

		memset(reloc0, 0, sizeof(reloc0));
		for (i = 0; i < 1024; i++) {
			uint64_t offset;
			uint32_t *b = &ptr[16 * i];

			reloc0[i].presumed_offset = obj[0].offset;
			reloc0[i].offset = (b - ptr + 1) * sizeof(*ptr);
			reloc0[i].delta = i * sizeof(uint32_t);
			reloc0[i].read_domains = I915_GEM_DOMAIN_INSTRUCTION;
			reloc0[i].write_domain = I915_GEM_DOMAIN_INSTRUCTION;

			offset = obj[0].offset + reloc0[i].delta;
			*b++ = MI_STORE_DWORD_IMM | (gen < 6 ? 1 << 22 : 0);
			if (gen >= 8) {
				*b++ = offset;
				*b++ = offset >> 32;
			} else if (gen >= 4) {
				*b++ = 0;
				*b++ = offset;
				reloc0[i].offset += sizeof(*ptr);
			} else {
				b[-1] -= 1;
				*b++ = offset;
			}
			*b++ = i;
			*b++ = MI_BATCH_BUFFER_END;
		}
		munmap(ptr, 64*1024);

		ptr = gem_mmap__wc(fd, obj[2].handle, 0, 64*1024,
				PROT_WRITE | PROT_READ);
		gem_set_domain(fd, obj[2].handle,
				I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);

		memset(reloc1, 0, sizeof(reloc1));
		for (i = 0; i < 1024; i++) {
			uint64_t offset;
			uint32_t *b = &ptr[16 * i];

			reloc1[i].presumed_offset = obj[0].offset;
			reloc1[i].offset = (b - ptr + 1) * sizeof(*ptr);
			reloc1[i].delta = i * sizeof(uint32_t);
			reloc1[i].read_domains = I915_GEM_DOMAIN_INSTRUCTION;
			reloc1[i].write_domain = I915_GEM_DOMAIN_INSTRUCTION;

			offset = obj[0].offset + reloc1[i].delta;
			*b++ = MI_STORE_DWORD_IMM | (gen < 6 ? 1 << 22 : 0);
			if (gen >= 8) {
				*b++ = offset;
				*b++ = offset >> 32;
			} else if (gen >= 4) {
				*b++ = 0;
				*b++ = offset;
				reloc1[i].offset += sizeof(*ptr);
			} else {
				b[-1] -= 1;
				*b++ = offset;
			}
			*b++ = i ^ 0xffffffff;
			*b++ = MI_BATCH_BUFFER_END;
		}
		munmap(ptr, 64*1024);

		igt_timeout(timeout) {
			bool xor = (cycles >> 10) & 1;
			int idx = cycles++ % 1024;

			/* Inspect a different cacheline each iteration */
			i = 16 * (idx % 64) + (idx / 64);

			obj[1].relocs_ptr = (uintptr_t)&reloc0[i];
			obj[2].relocs_ptr = (uintptr_t)&reloc1[i];
			execbuf.batch_start_offset =  64*i;
			execbuf.buffer_count = 2 + xor;

			gem_execbuf(fd, &execbuf);

			if (flags & SET_DOMAIN) {
				igt_interruptible(flags & INTERRUPTIBLE)
					gem_set_domain(fd, obj[0].handle,
						       I915_GEM_DOMAIN_GTT,
						       (flags & WRITE) ? I915_GEM_DOMAIN_GTT : 0);

				if (xor)
					igt_assert_eq_u32(map[i], i ^ 0xffffffff);
				else
					igt_assert_eq_u32(map[i], i);

				if (flags & WRITE)
					map[i] = 0xdeadbeef;
			} else if (flags & KERNEL) {
				uint32_t val;

				igt_interruptible(flags & INTERRUPTIBLE)
					gem_read(fd, obj[0].handle,
						 i*sizeof(uint32_t),
						 &val, sizeof(val));

				if (xor)
					igt_assert_eq_u32(val, i ^ 0xffffffff);
				else
					igt_assert_eq_u32(val, i);

				if (flags & WRITE) {
					val = 0xdeadbeef;
					igt_interruptible(flags & INTERRUPTIBLE)
						gem_write(fd, obj[0].handle,
							  i*sizeof(uint32_t),
							  &val, sizeof(val));
				}
			} else {
				igt_interruptible(flags & INTERRUPTIBLE)
					gem_sync(fd, obj[0].handle);

				if (!(flags & COHERENT) && !gem_has_llc(fd))
					igt_clflush_range(&map[i], sizeof(map[i]));

				if (xor)
					igt_assert_eq_u32(map[i], i ^ 0xffffffff);
				else
					igt_assert_eq_u32(map[i], i);

				if (flags & WRITE) {
					map[i] = 0xdeadbeef;
					if (!(flags & COHERENT))
						igt_clflush_range(&map[i], sizeof(map[i]));
				}
			}
		}
		igt_info("Child[%d]: %lu cycles\n", child, cycles);

		gem_close(fd, obj[2].handle);
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
	const struct mode {
		const char *name;
		unsigned flags;
	} modes[] = {
		{ "ro", 0 },
		{ "rw", WRITE },
		{ "pro", KERNEL },
		{ "prw", KERNEL | WRITE },
		{ "set", SET_DOMAIN | WRITE },
		{ NULL },
	};
	int gen = -1;
	int fd = -1;

	igt_skip_on_simulation();

	igt_fixture {
		igt_require(igt_setup_clflush());
		fd = drm_open_driver(DRIVER_INTEL);
		gem_require_mmap_wc(fd);
		gen = intel_gen(intel_get_drm_devid(fd));
	}

	igt_fork_hang_detector(fd);

	for (e = intel_execution_engines; e->name; e++) igt_subtest_group {
		unsigned ring = e->exec_id | e->flags;
		unsigned timeout = 2 + 120*!!e->exec_id;

		igt_fixture {
			gem_require_ring(fd, ring);
			igt_skip_on_f(gen == 6 && e->exec_id == I915_EXEC_BSD,
				      "MI_STORE_DATA broken on gen6 bsd\n");
		}

		for (const struct mode *m = modes; m->name; m++) {
			igt_subtest_f("%suc-%s-%s",
				      e->exec_id == 0 ? "basic-" : "",
				      m->name,
				      e->name)
				run(fd, ring, ncpus, timeout,
				    UNCACHED | m->flags);

			igt_subtest_f("%suc-%s-%s-interruptible",
				      e->exec_id == 0 ? "basic-" : "",
				      m->name,
				      e->name)
				run(fd, ring, ncpus, timeout,
				    UNCACHED | m->flags | INTERRUPTIBLE);

			igt_subtest_f("%swb-%s-%s",
				      e->exec_id == 0 ? "basic-" : "",
				      m->name,
				      e->name)
				run(fd, ring, ncpus, timeout,
				    COHERENT | m->flags);

			igt_subtest_f("%swb-%s-%s-interruptible",
				      e->exec_id == 0 ? "basic-" : "",
				      m->name,
				      e->name)
				run(fd, ring, ncpus, timeout,
				    COHERENT | m->flags | INTERRUPTIBLE);
		}
	}

	igt_stop_hang_detector();

	igt_fixture
		close(fd);
}
