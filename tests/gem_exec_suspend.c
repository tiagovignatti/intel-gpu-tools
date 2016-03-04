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
 *
 */

/** @file gem_exec_suspend.c
 *
 * Exercise executing batches across suspend before checking the results.
 */

#include "igt.h"
#include "igt_gt.h"

enum mode {
	NOSLEEP = 0,
	SUSPEND,
	HIBERNATE,
};

static void run_test(int fd, unsigned ring, enum mode mode);

static void check_bo(int fd, uint32_t handle)
{
	uint32_t *map;
	int i;

	igt_debug("Verifying result\n");
	map = gem_mmap__cpu(fd, handle, 0, 4096, PROT_READ);
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_CPU, 0);
	for (i = 0; i < 1024; i++)
		igt_assert_eq(map[i], i);
	munmap(map, 4096);
}

static void test_all(int fd)
{
	unsigned engine;

	for_each_engine(fd, engine)
		run_test(fd, engine, NOSLEEP);
}

static void run_test(int fd, unsigned ring, enum mode mode)
{
	const int gen = intel_gen(intel_get_drm_devid(fd));
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_exec_object2 obj[2];
	struct drm_i915_gem_relocation_entry reloc;
	struct drm_i915_gem_execbuffer2 execbuf;

	gem_require_ring(fd, ring);
	igt_skip_on_f(gen == 6 && (ring & ~(3<<13)) == I915_EXEC_BSD,
		      "MI_STORE_DATA broken on gen6 bsd\n");

	/* Before suspending, check normal operation */
	if (mode != NOSLEEP)
		test_all(fd);

	gem_quiescent_gpu(fd);

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)obj;
	execbuf.buffer_count = 2;
	execbuf.flags = ring | (1 << 11);
	if (gen < 6)
		execbuf.flags |= I915_EXEC_SECURE;

	memset(obj, 0, sizeof(obj));
	obj[0].handle = gem_create(fd, 4096);
	obj[0].flags |= EXEC_OBJECT_WRITE;
	obj[1].handle = gem_create(fd, 4096);
	gem_write(fd, obj[1].handle, 0, &bbe, sizeof(bbe));
	igt_require(__gem_execbuf(fd, &execbuf) == 0);
	gem_close(fd, obj[1].handle);

	memset(&reloc, 0, sizeof(reloc));
	reloc.target_handle = obj[0].handle;
	reloc.presumed_offset = obj[0].offset;
	reloc.offset = sizeof(uint32_t);
	if (gen >= 4 && gen < 8)
		reloc.offset += sizeof(uint32_t);
	reloc.read_domains = I915_GEM_DOMAIN_INSTRUCTION;
	reloc.write_domain = I915_GEM_DOMAIN_INSTRUCTION;

	obj[1].relocs_ptr = (uintptr_t)&reloc;
	obj[1].relocation_count = 1;

	for (int i = 0; i < 1024; i++) {
		uint64_t offset;
		uint32_t buf[16];
		int b;

		obj[1].handle = gem_create(fd, 4096);

		reloc.delta = i * sizeof(uint32_t);
		offset = reloc.presumed_offset + reloc.delta;

		b = 0;
		buf[b] = MI_STORE_DWORD_IMM | (gen < 6 ? 1 << 22 : 0);
		if (gen >= 8) {
			buf[++b] = offset;
			buf[++b] = offset >> 32;
		} else if (gen >= 4) {
			buf[++b] = 0;
			buf[++b] = offset;
		} else {
			buf[b] -= 1;
			buf[++b] = offset;
		}
		buf[++b] = i;
		buf[++b] = MI_BATCH_BUFFER_END;
		gem_write(fd, obj[1].handle,
			  4096-sizeof(buf), buf, sizeof(buf));
		gem_execbuf(fd, &execbuf);
		gem_close(fd, obj[1].handle);
	}

	switch (mode) {
	case NOSLEEP:
		break;

	case SUSPEND:
		igt_system_suspend_autoresume();
		break;

	case HIBERNATE:
		igt_system_hibernate_autoresume();
		break;
	}

	check_bo(fd, obj[0].handle);
	gem_close(fd, obj[0].handle);

	gem_quiescent_gpu(fd);

	/* After resume, make sure it still works */
	if (mode != NOSLEEP)
		test_all(fd);
}

igt_main
{
	const struct {
		const char *suffix;
		enum mode mode;
	} modes[] = {
		{ "", NOSLEEP },
		{ "-S3", SUSPEND },
		{ "-S4", HIBERNATE },
		{ NULL, 0 }
	}, *m;
	const struct intel_execution_engine *e;
	int fd;

	igt_fixture
		fd = drm_open_driver_master(DRIVER_INTEL);

	for (e = intel_execution_engines; e->name; e++) {
		for (m = modes; m->suffix; m++) {
			igt_subtest_f("%s%s", e->name, m->suffix)
				run_test(fd, e->exec_id | e->flags, m->mode);
		}
	}

	igt_fixture
		close(fd);
}
