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
 *    Daniel Vetter <daniel.vetter@ffwll.ch> (based on gem_storedw_*.c)
 *
 */

#include "igt.h"

IGT_TEST_DESCRIPTION("Basic check of ring<->ring write synchronisation.");

/*
 * Testcase: Basic check of ring<->ring sync using a dummy reloc
 *
 * Extremely efficient at catching missed irqs with semaphores=0 ...
 */

static void
sync_loop(int fd)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 object[2];
	struct drm_i915_gem_relocation_entry reloc[1];
	unsigned engines[16];
	unsigned nengine;
	unsigned engine;
	int i;

	nengine = 0;
	for_each_engine(fd, engine)
		engines[nengine++] = engine;
	igt_require(nengine);

	memset(object, 0, sizeof(object));
	object[0].handle = gem_create(fd, 4096);
	object[0].flags = EXEC_OBJECT_WRITE;
	object[1].handle = gem_create(fd, 4096);
	gem_write(fd, object[1].handle, 0, &bbe, sizeof(bbe));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)object;
	execbuf.buffer_count = 2;

	/* Check if we have no-reloc support first */
	if (__gem_execbuf(fd, &execbuf)) {
		object[0].flags = 0;
		object[1].relocs_ptr = (uintptr_t)reloc;
		object[1].relocation_count = 1;

		/* Add a dummy relocation to mark the object as writing */
		memset(reloc, 0, sizeof(reloc));
		reloc->offset = 1000;
		reloc->target_handle = object[0].handle;
		reloc->read_domains = I915_GEM_DOMAIN_RENDER;
		reloc->write_domain = I915_GEM_DOMAIN_RENDER;

		gem_execbuf(fd, &execbuf);
	}

	srandom(0xdeadbeef);

	for (i = 0; i < SLOW_QUICK(0x100000, 10); i++) {
		execbuf.flags = engines[rand() % nengine];
		gem_execbuf(fd, &execbuf);
	}

	gem_sync(fd, object[1].handle);
	gem_close(fd, object[1].handle);
	gem_close(fd, object[0].handle);
}

igt_simple_main
{
	int fd;

	fd = drm_open_driver(DRIVER_INTEL);
	intel_detect_and_clear_missed_interrupts(fd);

	sync_loop(fd);

	igt_assert_eq(intel_detect_and_clear_missed_interrupts(fd), 0);
	close(fd);
}
