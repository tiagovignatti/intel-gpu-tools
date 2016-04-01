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

#include "igt.h"

IGT_TEST_DESCRIPTION("Basic sanity check of execbuf-ioctl relocations.");

#define LOCAL_I915_EXEC_BSD_SHIFT      (13)
#define LOCAL_I915_EXEC_BSD_MASK       (3 << LOCAL_I915_EXEC_BSD_SHIFT)

#define ENGINE_MASK  (I915_EXEC_RING_MASK | LOCAL_I915_EXEC_BSD_MASK)

static uint32_t find_last_set(uint64_t x)
{
	uint32_t i = 0;
	while (x) {
		x >>= 1;
		i++;
	}
	return i;
}

static void write_dword(int fd,
			uint32_t target_handle,
			uint64_t target_offset,
			uint32_t value)
{
	int gen = intel_gen(intel_get_drm_devid(fd));
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj[2];
	struct drm_i915_gem_relocation_entry reloc;
	uint32_t buf[16];
	int i;

	memset(obj, 0, sizeof(obj));
	obj[0].handle = target_handle;
	obj[1].handle = gem_create(fd, 4096);

	i = 0;
	buf[i++] = MI_STORE_DWORD_IMM | (gen < 6 ? 1<<22 : 0);
	if (gen >= 8) {
		buf[i++] = target_offset;
		buf[i++] = target_offset >> 32;
	} else if (gen >= 4) {
		buf[i++] = 0;
		buf[i++] = target_offset;
	} else {
		buf[i-1]--;
		buf[i++] = target_offset;
	}
	buf[i++] = value;
	buf[i++] = MI_BATCH_BUFFER_END;
	gem_write(fd, obj[1].handle, 0, buf, sizeof(buf));

	memset(&reloc, 0, sizeof(reloc));
	if (gen >= 8 || gen < 4)
		reloc.offset = sizeof(uint32_t);
	else
		reloc.offset = 2*sizeof(uint32_t);
	reloc.target_handle = target_handle;
	reloc.delta = target_offset;
	reloc.read_domains = I915_GEM_DOMAIN_INSTRUCTION;
	reloc.write_domain = I915_GEM_DOMAIN_INSTRUCTION;

	obj[1].relocation_count = 1;
	obj[1].relocs_ptr = (uintptr_t)&reloc;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)obj;
	execbuf.buffer_count = 2;
	execbuf.flags = I915_EXEC_SECURE;
	gem_execbuf(fd, &execbuf);
	gem_close(fd, obj[1].handle);
}

enum mode { MEM, CPU, WC, GTT };
#define RO 0x100
static void from_mmap(int fd, uint64_t size, enum mode mode)
{
	uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	struct drm_i915_gem_relocation_entry *relocs;
	uint32_t reloc_handle;
	uint64_t value;
	uint64_t max, i;
	int retry = 2;

	intel_require_memory(1, size, CHECK_RAM);

	memset(&obj, 0, sizeof(obj));
	obj.handle = gem_create(fd, 4096);
	gem_write(fd, obj.handle, 0, &bbe, sizeof(bbe));

	max = size / sizeof(*relocs);
	switch (mode & ~RO) {
	case MEM:
		relocs = mmap(0, size,
			      PROT_WRITE, MAP_PRIVATE | MAP_ANON,
			      -1, 0);
		igt_assert(relocs != (void *)-1);
		break;
	case GTT:
		reloc_handle = gem_create(fd, size);
		relocs = gem_mmap__gtt(fd, reloc_handle, size, PROT_WRITE);
		gem_set_domain(fd, reloc_handle,
				I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
		gem_close(fd, reloc_handle);
		break;
	case CPU:
		reloc_handle = gem_create(fd, size);
		relocs = gem_mmap__cpu(fd, reloc_handle, 0, size, PROT_WRITE);
		gem_set_domain(fd, reloc_handle,
			       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
		gem_close(fd, reloc_handle);
		break;
	case WC:
		reloc_handle = gem_create(fd, size);
		relocs = gem_mmap__wc(fd, reloc_handle, 0, size, PROT_WRITE);
		gem_set_domain(fd, reloc_handle,
			       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
		gem_close(fd, reloc_handle);
		break;
	}

	for (i = 0; i < max; i++) {
		relocs[i].target_handle = obj.handle;
		relocs[i].presumed_offset = ~0ull;
		relocs[i].offset = 1024;
		relocs[i].delta = i;
		relocs[i].read_domains = I915_GEM_DOMAIN_INSTRUCTION;
		relocs[i].write_domain = 0;
	}
	obj.relocation_count = max;
	obj.relocs_ptr = (uintptr_t)relocs;

	if (mode & RO)
		mprotect(relocs, size, PROT_READ);

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&obj;
	execbuf.buffer_count = 1;
	while (relocs[0].presumed_offset == ~0ull && retry--)
		gem_execbuf(fd, &execbuf);
	gem_read(fd, obj.handle, 1024, &value, sizeof(value));
	gem_close(fd, obj.handle);

	igt_assert_eq_u64(value, obj.offset + max - 1);
	if (relocs[0].presumed_offset != ~0ull) {
		for (i = 0; i < max; i++)
			igt_assert_eq_u64(relocs[i].presumed_offset,
					  obj.offset);
	}
	munmap(relocs, size);
}

static void from_gpu(int fd)
{
	uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	struct drm_i915_gem_relocation_entry *relocs;
	uint32_t reloc_handle;
	uint64_t value;

	memset(&obj, 0, sizeof(obj));
	obj.handle = gem_create(fd, 4096);
	gem_write(fd, obj.handle, 0, &bbe, sizeof(bbe));

	reloc_handle = gem_create(fd, 4096);
	write_dword(fd,
		    reloc_handle,
		    offsetof(struct drm_i915_gem_relocation_entry,
			     target_handle),
		    obj.handle);
	write_dword(fd,
		    reloc_handle,
		    offsetof(struct drm_i915_gem_relocation_entry,
			     offset),
		    1024);
	write_dword(fd,
		    reloc_handle,
		    offsetof(struct drm_i915_gem_relocation_entry,
			     read_domains),
		    I915_GEM_DOMAIN_INSTRUCTION);

	relocs = gem_mmap__cpu(fd, reloc_handle, 0, 4096, PROT_READ);
	gem_set_domain(fd, reloc_handle,
		       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
	gem_close(fd, reloc_handle);

	obj.relocation_count = 1;
	obj.relocs_ptr = (uintptr_t)relocs;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&obj;
	execbuf.buffer_count = 1;
	gem_execbuf(fd, &execbuf);
	gem_read(fd, obj.handle, 1024, &value, sizeof(value));
	gem_close(fd, obj.handle);

	igt_assert_eq_u64(value, obj.offset);
	igt_assert_eq_u64(relocs->presumed_offset, obj.offset);
	munmap(relocs, 4096);
}

static bool ignore_engine(int gen, unsigned engine)
{
	return gen == 6 && (engine & ~(3<<13)) == I915_EXEC_BSD;
}

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

static void active(int fd, unsigned engine)
{
	const int gen = intel_gen(intel_get_drm_devid(fd));
	struct drm_i915_gem_relocation_entry reloc;
	struct drm_i915_gem_exec_object2 obj[2];
	struct drm_i915_gem_execbuffer2 execbuf;
	unsigned engines[16];
	unsigned nengine;
	int pass;

	nengine = 0;
	if (engine == -1) {
		for_each_engine(fd, engine) {
			if (!ignore_engine(gen, engine))
				engines[nengine++] = engine;
		}
	} else {
		igt_require(gem_has_ring(fd, engine));
		igt_require(!ignore_engine(gen, engine));
		engines[nengine++] = engine;
	}
	igt_require(nengine);

	memset(obj, 0, sizeof(obj));
	obj[0].handle = gem_create(fd, 4096);
	obj[1].handle = gem_create(fd, 64*1024);
	obj[1].relocs_ptr = (uintptr_t)&reloc;
	obj[1].relocation_count = 1;

	memset(&reloc, 0, sizeof(reloc));
	reloc.offset = sizeof(uint32_t);
	reloc.target_handle = obj[0].handle;
	if (gen < 8 && gen >= 4)
		reloc.offset += sizeof(uint32_t);
	reloc.read_domains = I915_GEM_DOMAIN_INSTRUCTION;
	reloc.write_domain = I915_GEM_DOMAIN_INSTRUCTION;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)obj;
	execbuf.buffer_count = 2;
	if (gen < 6)
		execbuf.flags |= I915_EXEC_SECURE;

	for (pass = 0; pass < 1024; pass++) {
		uint32_t batch[16];
		int i = 0;
		batch[i] = MI_STORE_DWORD_IMM | (gen < 6 ? 1 << 22 : 0);
		if (gen >= 8) {
			batch[++i] = 0;
			batch[++i] = 0;
		} else if (gen >= 4) {
			batch[++i] = 0;
			batch[++i] = 0;
		} else {
			batch[i]--;
			batch[++i] = 0;
		}
		batch[++i] = pass;
		batch[++i] = MI_BATCH_BUFFER_END;
		gem_write(fd, obj[1].handle, pass*sizeof(batch),
			  batch, sizeof(batch));
	}

	for (pass = 0; pass < 1024; pass++) {
		reloc.delta = 4*pass;
		reloc.presumed_offset = -1;
		execbuf.flags &= ~ENGINE_MASK;
		execbuf.flags |= engines[rand() % nengine];
		gem_execbuf(fd, &execbuf);
		execbuf.batch_start_offset += 64;
		reloc.offset += 64;
	}
	gem_close(fd, obj[1].handle);

	check_bo(fd, obj[0].handle);
	gem_close(fd, obj[0].handle);
}

igt_main
{
	uint64_t size;
	int fd = -1;

	igt_fixture
		fd = drm_open_driver_master(DRIVER_INTEL);

	for (size = 4096; size <= 4ull*1024*1024*1024; size <<= 1) {
		igt_subtest_f("mmap-%u", find_last_set(size) - 1)
			from_mmap(fd, size, MEM);
		igt_subtest_f("readonly-%u", find_last_set(size) - 1)
			from_mmap(fd, size, MEM | RO);
		igt_subtest_f("cpu-%u", find_last_set(size) - 1)
			from_mmap(fd, size, CPU);
		igt_subtest_f("wc-%u", find_last_set(size) - 1)
			from_mmap(fd, size, WC);
		igt_subtest_f("gtt-%u", find_last_set(size) - 1)
			from_mmap(fd, size, GTT);
	}

	igt_subtest("gpu")
		from_gpu(fd);

	igt_subtest("active")
		active(fd, -1);
	for (const struct intel_execution_engine *e = intel_execution_engines;
	     e->name; e++) {
		igt_subtest_f("active-%s", e->name)
			active(fd, e->exec_id | e->flags);
	}
	igt_fixture
		close(fd);
}
