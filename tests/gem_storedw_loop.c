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
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Jesse Barnes <jbarnes@virtuousgeek.org> (based on gem_bad_blit.c)
 *
 */

#include "igt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "drm.h"

IGT_TEST_DESCRIPTION("Basic CS check using MI_STORE_DATA_IMM.");

#define LOCAL_I915_EXEC_VEBOX (4<<0)

static int devid;

/*
 * Testcase: Basic bsd MI check using MI_STORE_DATA_IMM
 */

static unsigned coherent_domain;

static void *
mmap_coherent(int fd, uint32_t handle, int size)
{
	if (gem_has_llc(fd)) {
		coherent_domain = I915_GEM_DOMAIN_CPU;
		return gem_mmap__cpu(fd, handle, 0, size, PROT_WRITE);
	}

	coherent_domain = I915_GEM_DOMAIN_GTT;
	if (gem_mmap__has_wc(fd))
		return gem_mmap__wc(fd, handle, 0, size, PROT_WRITE);
	else
		return gem_mmap__gtt(fd, handle, size, PROT_WRITE);
}

static void
store_dword_loop(int fd, int ring, int count, int divider)
{
	int i, val = 0;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj[2];
	struct drm_i915_gem_relocation_entry reloc[divider];
	uint32_t handle[divider];
	uint32_t *batch[divider];
	uint32_t *target;
	int gen = intel_gen(devid);

	memset(obj, 0, sizeof(obj));
	obj[0].handle = gem_create(fd, 4096);
	target = mmap_coherent(fd, obj[0].handle, 4096);

	memset(reloc, 0, sizeof(reloc));
	for (i = 0; i < divider; i++) {
		uint32_t *b;

		handle[i] = gem_create(fd, 4096);
		batch[i] = mmap_coherent(fd, handle[i], 4096);
		gem_set_domain(fd, handle[i], coherent_domain, coherent_domain);

		b = batch[i];
		*b++ = MI_STORE_DWORD_IMM;
		*b++ = 0;
		*b++ = 0;
		*b++ = 0;
		*b++ = MI_BATCH_BUFFER_END;

		reloc[i].target_handle = obj[0].handle;
		reloc[i].offset = 4;
		if (gen < 8)
			reloc[i].offset += 4;
		reloc[i].read_domains = I915_GEM_DOMAIN_INSTRUCTION;
		reloc[i].write_domain = I915_GEM_DOMAIN_INSTRUCTION;
		obj[1].relocation_count = 1;
	}

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)obj;
	execbuf.buffer_count = 2;
	execbuf.flags = ring;

	igt_info("running storedw loop on render with stall every %i batch\n", divider);

	for (i = 0; i < SLOW_QUICK(0x2000, 0x10); i++) {
		int j = i % divider;

		gem_set_domain(fd, handle[j], coherent_domain, coherent_domain);
		batch[j][3] = val;
		obj[1].handle = handle[j];
		obj[1].relocs_ptr = (uintptr_t)&reloc[j];
		gem_execbuf(fd, &execbuf);

		if (j == 0) {
			gem_set_domain(fd, obj[0].handle, coherent_domain, 0);
			igt_assert_f(*target == val,
				     "%d: value mismatch: stored 0x%08x, expected 0x%08x\n",
				     i, *target, val);
		}

		val++;
	}

	gem_set_domain(fd, obj[0].handle, coherent_domain, 0);
	igt_info("completed %d writes successfully, current value: 0x%08x\n",
		 i, target[0]);

	munmap(target, 4096);
	gem_close(fd, obj[0].handle);
	for (i = 0; i < divider; ++i) {
		munmap(batch[i], 4096);
		gem_close(fd, handle[i]);
	}
}

static void
store_test(int fd, int ring, int count)
{
	gem_require_ring(fd, ring);
	store_dword_loop(fd, ring, count, 1);
	store_dword_loop(fd, ring, count, 2);
	if (!igt_run_in_simulation()) {
		store_dword_loop(fd, ring, count, 3);
		store_dword_loop(fd, ring, count, 5);
		store_dword_loop(fd, ring, count, 7);
		store_dword_loop(fd, ring, count, 11);
		store_dword_loop(fd, ring, count, 13);
		store_dword_loop(fd, ring, count, 17);
		store_dword_loop(fd, ring, count, 19);
	}
}

static void
check_test_requirements(int fd, int ringid)
{
	gem_require_ring(fd, ringid);
	igt_skip_on_f(intel_gen(devid) == 6 && ringid == I915_EXEC_BSD,
		      "MI_STORE_DATA broken on gen6 bsd\n");
}

igt_main
{
	const struct intel_execution_engine *e;
	int fd;

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);
		devid = intel_get_drm_devid(fd);

		igt_skip_on_f(intel_gen(devid) < 6,
			      "MI_STORE_DATA can only use GTT address on gen4+/g33 and "
			      "needs snoopable mem on pre-gen6\n");

		/* This only works with ppgtt */
		igt_require(gem_uses_ppgtt(fd));
	}

	for (e = intel_execution_engines; e->name; e++) {
		igt_subtest_f("basic-%s", e->name) {
			check_test_requirements(fd, e->exec_id);
			store_test(fd, e->exec_id | e->flags, 16*1024);
		}

		igt_subtest_f("long-%s", e->name) {
			check_test_requirements(fd, e->exec_id);
			store_test(fd, e->exec_id | e->flags, 1024*1024);
		}
	}

	igt_fixture {
		close(fd);
	}
}
