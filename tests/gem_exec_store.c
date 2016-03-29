/*
 * Copyright © 2009 Intel Corporation
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

/** @file gem_exec_store.c
 *
 * Simplest non-NOOP only batch with verification.
 */

#include "igt.h"
#include "igt_gt.h"

#define LOCAL_I915_EXEC_BSD_SHIFT      (13)
#define LOCAL_I915_EXEC_BSD_MASK       (3 << LOCAL_I915_EXEC_BSD_SHIFT)

#define ENGINE_MASK  (I915_EXEC_RING_MASK | LOCAL_I915_EXEC_BSD_MASK)

static void store_dword(int fd, unsigned ring)
{
	const int gen = intel_gen(intel_get_drm_devid(fd));
	struct drm_i915_gem_exec_object2 obj[2];
	struct drm_i915_gem_relocation_entry reloc;
	struct drm_i915_gem_execbuffer2 execbuf;
	uint32_t batch[16];
	int i;

	gem_require_ring(fd, ring);
	igt_skip_on_f(gen == 6 && (ring & ~(3<<13)) == I915_EXEC_BSD,
		      "MI_STORE_DATA broken on gen6 bsd\n");

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)obj;
	execbuf.buffer_count = 2;
	execbuf.flags = ring;
	if (gen < 6)
		execbuf.flags |= I915_EXEC_SECURE;

	memset(obj, 0, sizeof(obj));
	obj[0].handle = gem_create(fd, 4096);
	obj[1].handle = gem_create(fd, 4096);

	memset(&reloc, 0, sizeof(reloc));
	reloc.target_handle = obj[0].handle;
	reloc.presumed_offset = 0;
	reloc.offset = sizeof(uint32_t);
	reloc.delta = 0;
	reloc.read_domains = I915_GEM_DOMAIN_INSTRUCTION;
	reloc.write_domain = I915_GEM_DOMAIN_INSTRUCTION;
	obj[1].relocs_ptr = (uintptr_t)&reloc;
	obj[1].relocation_count = 1;

	i = 0;
	batch[i] = MI_STORE_DWORD_IMM | (gen < 6 ? 1 << 22 : 0);
	if (gen >= 8) {
		batch[++i] = 0;
		batch[++i] = 0;
	} else if (gen >= 4) {
		batch[++i] = 0;
		batch[++i] = 0;
		reloc.offset += sizeof(uint32_t);
	} else {
		batch[i]--;
		batch[++i] = 0;
	}
	batch[++i] = 0xc0ffee;
	batch[++i] = MI_BATCH_BUFFER_END;
	gem_write(fd, obj[1].handle, 0, batch, sizeof(batch));
	gem_execbuf(fd, &execbuf);
	gem_close(fd, obj[1].handle);

	gem_read(fd, obj[0].handle, 0, batch, sizeof(batch));
	gem_close(fd, obj[0].handle);
	igt_assert_eq(*batch, 0xc0ffee);
}

static void store_all(int fd)
{
	const int gen = intel_gen(intel_get_drm_devid(fd));
	struct drm_i915_gem_exec_object2 obj[2];
	struct drm_i915_gem_relocation_entry reloc[32];
	struct drm_i915_gem_execbuffer2 execbuf;
	unsigned engines[16], permuted[16];
	uint32_t batch[16];
	uint64_t offset;
	unsigned engine, nengine;
	int value;
	int i, j;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)obj;
	execbuf.buffer_count = 2;
	if (gen < 6)
		execbuf.flags |= I915_EXEC_SECURE;

	memset(reloc, 0, sizeof(reloc));
	memset(obj, 0, sizeof(obj));
	obj[0].handle = gem_create(fd, 4096);
	obj[1].handle = gem_create(fd, 4096);
	obj[1].relocation_count = 1;

	offset = sizeof(uint32_t);
	i = 0;
	batch[i] = MI_STORE_DWORD_IMM | (gen < 6 ? 1 << 22 : 0);
	if (gen >= 8) {
		batch[++i] = 0;
		batch[++i] = 0;
	} else if (gen >= 4) {
		batch[++i] = 0;
		batch[++i] = 0;
		offset += sizeof(uint32_t);
	} else {
		batch[i]--;
		batch[++i] = 0;
	}
	batch[value = ++i] = 0xc0ffee;
	batch[++i] = MI_BATCH_BUFFER_END;

	nengine = 0;
	for_each_engine(fd, engine) {
		if (gen == 6 && (engine & ~(3<<13)) == I915_EXEC_BSD)
			continue;

		igt_assert(2*(nengine+1)*sizeof(batch) <= 4096);

		execbuf.flags &= ~ENGINE_MASK;
		execbuf.flags |= engine;

		j = 2*nengine;
		reloc[j].target_handle = obj[0].handle;
		reloc[j].presumed_offset = ~0;
		reloc[j].offset = j*sizeof(batch) + offset;
		reloc[j].delta = nengine*sizeof(uint32_t);
		reloc[j].read_domains = I915_GEM_DOMAIN_INSTRUCTION;
		reloc[j].write_domain = I915_GEM_DOMAIN_INSTRUCTION;
		obj[1].relocs_ptr = (uintptr_t)&reloc[j];

		batch[value] = 0xdeadbeef;
		gem_write(fd, obj[1].handle, j*sizeof(batch),
			  batch, sizeof(batch));
		execbuf.batch_start_offset = j*sizeof(batch);
		gem_execbuf(fd, &execbuf);

		j = 2*nengine + 1;
		reloc[j].target_handle = obj[0].handle;
		reloc[j].presumed_offset = ~0;
		reloc[j].offset = j*sizeof(batch) + offset;
		reloc[j].delta = nengine*sizeof(uint32_t);
		reloc[j].read_domains = I915_GEM_DOMAIN_INSTRUCTION;
		reloc[j].write_domain = I915_GEM_DOMAIN_INSTRUCTION;
		obj[1].relocs_ptr = (uintptr_t)&reloc[j];

		batch[value] = nengine;
		gem_write(fd, obj[1].handle, j*sizeof(batch),
			  batch, sizeof(batch));
		execbuf.batch_start_offset = j*sizeof(batch);
		gem_execbuf(fd, &execbuf);

		engines[nengine++] = engine;
	}
	gem_sync(fd, obj[1].handle);

	for (i = 0; i < nengine; i++) {
		obj[1].relocs_ptr = (uintptr_t)&reloc[2*i];
		execbuf.batch_start_offset = 2*i*sizeof(batch);
		memcpy(permuted, engines, nengine*sizeof(engines[0]));
		igt_permute_array(permuted, nengine, igt_exchange_int);
		for (j = 0; j < nengine; j++) {
			execbuf.flags &= ~ENGINE_MASK;
			execbuf.flags |= permuted[j];
			gem_execbuf(fd, &execbuf);
		}
		obj[1].relocs_ptr = (uintptr_t)&reloc[2*i+1];
		execbuf.batch_start_offset = (2*i+1)*sizeof(batch);
		execbuf.flags &= ~ENGINE_MASK;
		execbuf.flags |= engines[i];
		gem_execbuf(fd, &execbuf);
	}
	gem_close(fd, obj[1].handle);

	gem_read(fd, obj[0].handle, 0, engines, sizeof(engines));
	gem_close(fd, obj[0].handle);

	for (i = 0; i < nengine; i++)
		igt_assert_eq_u32(engines[i], i);
}

igt_main
{
	const struct intel_execution_engine *e;
	int fd;

	igt_fixture
		fd = drm_open_driver_master(DRIVER_INTEL);

	igt_fork_hang_detector(fd);

	for (e = intel_execution_engines; e->name; e++)
		igt_subtest_f("basic-%s", e->name)
			store_dword(fd, e->exec_id | e->flags);

	igt_subtest("basic-all")
		store_all(fd);

	igt_stop_hang_detector();

	igt_fixture
		close(fd);
}
