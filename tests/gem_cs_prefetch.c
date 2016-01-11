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

static void setup(int fd, struct shadow *shadow)
{
	int gen = intel_gen(intel_get_drm_devid(fd));
	uint32_t *cpu;
	int i = 0;

	shadow->handle = gem_create(fd, 4096);

	cpu = gem_mmap__cpu(fd, shadow->handle, 0, 4096, PROT_WRITE);
	gem_set_domain(fd, shadow->handle,
			I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
	cpu[i++] = MI_STORE_DWORD_IMM;
	if (gen >= 8) {
		cpu[i++] = BATCH_SIZE - sizeof(uint32_t);
		cpu[i++] = 0;
	} else if (gen >= 4) {
		cpu[i++] = 0;
		cpu[i++] = BATCH_SIZE - sizeof(uint32_t);
	} else {
		cpu[i-1]--;
		cpu[i++] = BATCH_SIZE - sizeof(uint32_t);
	}
	cpu[i++] = MI_BATCH_BUFFER_END;
	cpu[i++] = MI_BATCH_BUFFER_END;
	munmap(cpu, 4096);

	memset(&shadow->reloc, 0, sizeof(shadow->reloc));
	if (gen >= 8 || gen < 4)
		shadow->reloc.offset = sizeof(uint32_t);
	else
		shadow->reloc.offset = 2*sizeof(uint32_t);
	shadow->reloc.delta = BATCH_SIZE - sizeof(uint32_t);
	shadow->reloc.read_domains = I915_GEM_DOMAIN_INSTRUCTION;
	shadow->reloc.write_domain = I915_GEM_DOMAIN_INSTRUCTION;
}

static uint32_t new_batch(int fd, struct shadow *shadow)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[2];

	memset(gem_exec, 0, sizeof(gem_exec));
	gem_exec[0].handle = gem_create(fd, BATCH_SIZE);
	gem_exec[1].handle = shadow->handle;

	shadow->reloc.target_handle = gem_exec[0].handle;
	gem_exec[1].relocs_ptr = (uintptr_t)&shadow->reloc;
	gem_exec[1].relocation_count = 1;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 2;

	gem_execbuf(fd, &execbuf);

	return gem_exec[0].handle;
}

static void exec(int fd, uint32_t handle)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];

	memset(gem_exec, 0, sizeof(gem_exec));
	gem_exec[0].handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = BATCH_SIZE;

	gem_execbuf(fd, &execbuf);
}

igt_simple_main
{
	struct shadow shadow;
	uint64_t i, count;
	int fd;

	igt_skip_on_simulation();

	fd = drm_open_driver(DRIVER_INTEL);
	setup(fd, &shadow);

	count = gem_aperture_size(fd) / BATCH_SIZE;
	intel_require_memory(count, BATCH_SIZE, CHECK_RAM);

	/* Fill the entire gart with batches and run them. */
	for (i = 0; i < count; i++) {
		/* Launch the newly created batch... */
		exec(fd, new_batch(fd, &shadow));
		/* ...and leak the handle to consume the GTT */
		igt_progress("gem_cs_prefetch: ", i, count);
	}

	igt_info("Test suceeded, cleanup up - this might take a while.\n");
	close(fd);
}
