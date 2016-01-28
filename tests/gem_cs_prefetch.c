/*
 * Copyright © 2011 Intel Corporation
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
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

/*
 * Testcase: Test the CS prefetch behaviour on batches
 *
 * Historically the batch prefetcher doesn't check whether it's crossing page
 * boundaries and likes to throw up when it gets a pagefault in return for his
 * over-eager behaviour. Check for this.
 *
 * This test for a bug where we've failed to plug a scratch pte entry into the
 * very last gtt pte.
 */
#include "igt.h"

IGT_TEST_DESCRIPTION("Test the CS prefetch behaviour on batches.");

#define BATCH_SIZE 4096

struct shadow {
	uint32_t handle;
	struct drm_i915_gem_relocation_entry reloc;
};

static void gem_require_store_dword(int fd, unsigned ring)
{
	int gen = intel_gen(intel_get_drm_devid(fd));
	ring &= ~(3 << 13);
	igt_skip_on_f(gen == 6 && ring == I915_EXEC_BSD,
		      "MI_STORE_DATA broken on gen6 bsd\n");
}

static void setup(int fd, int gen, struct shadow *shadow)
{
	uint32_t buf[16];
	int i;

	shadow->handle = gem_create(fd, 4096);

	i = 0;
	buf[i++] = MI_STORE_DWORD_IMM | (gen < 6 ? 1 << 22 : 0);
	if (gen >= 8) {
		buf[i++] = BATCH_SIZE - sizeof(uint32_t);
		buf[i++] = 0;
	} else if (gen >= 4) {
		buf[i++] = 0;
		buf[i++] = BATCH_SIZE - sizeof(uint32_t);
	} else {
		buf[i-1]--;
		buf[i++] = BATCH_SIZE - sizeof(uint32_t);
	}
	buf[i++] = MI_BATCH_BUFFER_END;
	buf[i++] = MI_BATCH_BUFFER_END;
	gem_write(fd, shadow->handle, 0, buf, sizeof(buf));

	memset(&shadow->reloc, 0, sizeof(shadow->reloc));
	if (gen >= 8 || gen < 4)
		shadow->reloc.offset = sizeof(uint32_t);
	else
		shadow->reloc.offset = 2*sizeof(uint32_t);
	shadow->reloc.delta = BATCH_SIZE - sizeof(uint32_t);
	shadow->reloc.read_domains = I915_GEM_DOMAIN_INSTRUCTION;
	shadow->reloc.write_domain = I915_GEM_DOMAIN_INSTRUCTION;
}

static void can_test_ring(unsigned ring)
{
	int master = drm_open_driver_master(DRIVER_INTEL);
	int fd = drm_open_driver(DRIVER_INTEL);

	/* Dance to avoid dying with master open */
	close(master);
	gem_require_ring(fd, ring);
	gem_require_store_dword(fd, ring);
	close(fd);
}

static void test_ring(unsigned ring)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj[2];
	struct shadow shadow;
	uint64_t i, count;
	int fd, gen;

	can_test_ring(ring);

	fd = drm_open_driver_master(DRIVER_INTEL);
	gen = intel_gen(intel_get_drm_devid(fd));
	setup(fd, gen, &shadow);

	count = gem_aperture_size(fd) / BATCH_SIZE;
	intel_require_memory(count, BATCH_SIZE, CHECK_RAM);
	/* Fill the entire gart with batches and run them. */
	memset(obj, 0, sizeof(obj));
	obj[1].handle = shadow.handle;
	obj[1].relocs_ptr = (uintptr_t)&shadow.reloc;
	obj[1].relocation_count = 1;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)obj;
	execbuf.flags = ring;
	if (gen < 6)
		execbuf.flags |= I915_EXEC_SECURE;

	for (i = 0; i < count; i++) {
		/* Create the new batch using the GPU */
		obj[0].handle = gem_create(fd, BATCH_SIZE);
		shadow.reloc.target_handle = obj[0].handle;
		execbuf.buffer_count = 2;
		gem_execbuf(fd, &execbuf);

		/* ...then execute the new batch */
		execbuf.buffer_count = 1;
		gem_execbuf(fd, &execbuf);

		/* ...and leak the handle to consume the GTT */
	}

	close(fd);
}

igt_main
{
	const struct intel_execution_engine *e;

	igt_skip_on_simulation();

	for (e = intel_execution_engines; e->name; e++)
		igt_subtest_f("%s", e->name)
			test_ring(e->exec_id | e->flags);
}
