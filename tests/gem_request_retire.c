/*
 * Copyright © 2015 Intel Corporation
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
 *    Tvrtko Ursulin <tvrtko.ursulin@intel.com>
 *
 */

/** @file gem_request_retire
 *
 * Collection of tests targeting request retirement code paths.
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
#include <sys/mman.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#include "drm.h"
#include "i915_drm.h"

#include "intel_bufmgr.h"

#define WIDTH 4096
#define HEIGHT 4096
#define BO_SIZE (WIDTH * HEIGHT * sizeof(uint32_t))

static uint32_t
blit(int fd, uint32_t dst, uint32_t src, uint32_t ctx_id)
{
	const unsigned int copies = 1000;
	uint32_t batch[12 * copies + 5];
	struct drm_i915_gem_relocation_entry reloc[2 * copies];
	struct drm_i915_gem_exec_object2 obj[3];
	struct drm_i915_gem_execbuffer2 exec;
	uint32_t handle;
	unsigned int i = 0, j, r = 0;

	for (j = 0; j < copies; j++) {
		reloc[r].target_handle = dst;
		reloc[r].delta = 0;
		reloc[r].offset = (i + 4) * sizeof(uint32_t);
		reloc[r].presumed_offset = 0;
		reloc[r].read_domains = I915_GEM_DOMAIN_RENDER;
		reloc[r].write_domain = I915_GEM_DOMAIN_RENDER;

		r++;

		reloc[r].target_handle = src;
		reloc[r].delta = 0;
		reloc[r].offset = (i + 7) * sizeof(uint32_t);
		if (intel_gen(intel_get_drm_devid(fd)) >= 8)
			reloc[r].offset += sizeof(uint32_t);
		reloc[r].presumed_offset = 0;
		reloc[r].read_domains = I915_GEM_DOMAIN_RENDER;
		reloc[r].write_domain = 0;

		r++;

		batch[i++] = XY_SRC_COPY_BLT_CMD |
			XY_SRC_COPY_BLT_WRITE_ALPHA |
			XY_SRC_COPY_BLT_WRITE_RGB;
		if (intel_gen(intel_get_drm_devid(fd)) >= 8)
			batch[i - 1] |= 8;
		else
			batch[i - 1] |= 6;

		batch[i++] = (3 << 24) | /* 32 bits */
			(0xcc << 16) | /* copy ROP */
			WIDTH*4;
		batch[i++] = 0; /* dst x1,y1 */
		batch[i++] = (HEIGHT << 16) | WIDTH; /* dst x2,y2 */
		batch[i++] = 0; /* dst reloc */
		if (intel_gen(intel_get_drm_devid(fd)) >= 8)
			batch[i++] = 0;
		batch[i++] = 0; /* src x1,y1 */
		batch[i++] = WIDTH*4;
		batch[i++] = 0; /* src reloc */
		if (intel_gen(intel_get_drm_devid(fd)) >= 8)
			batch[i++] = 0;
	}

	batch[i++] = MI_BATCH_BUFFER_END;

	while (i % 4)
		batch[i++] = MI_NOOP;

	handle = gem_create(fd, sizeof(batch));
	gem_write(fd, handle, 0, batch, sizeof(batch));

	memset(obj, 0, sizeof(obj));
	memset(&exec, 0, sizeof(exec));

	obj[exec.buffer_count++].handle = dst;
	if (src != dst)
		obj[exec.buffer_count++].handle = src;
	obj[exec.buffer_count].handle = handle;
	obj[exec.buffer_count].relocation_count = 2 * copies;
	obj[exec.buffer_count].relocs_ptr = (uintptr_t)reloc;
	exec.buffer_count++;
	exec.buffers_ptr = (uintptr_t)obj;

	exec.batch_len = i * sizeof(uint32_t);
	exec.flags = I915_EXEC_BLT;
	i915_execbuffer2_set_context_id(exec, ctx_id);

	gem_execbuf(fd, &exec);

	return handle;
}

static uint32_t
noop(int fd, uint32_t src, uint32_t ctx_id)
{
	uint32_t batch[4];
	struct drm_i915_gem_exec_object2 obj[2];
	struct drm_i915_gem_execbuffer2 exec;
	uint32_t handle;
	unsigned int i = 0;

	batch[i++] = MI_NOOP;
	batch[i++] = MI_BATCH_BUFFER_END;
	batch[i++] = MI_NOOP;
	batch[i++] = MI_NOOP;

	handle = gem_create(fd, 4096);
	gem_write(fd, handle, 0, batch, sizeof(batch));

	memset(obj, 0, sizeof(obj));
	memset(&exec, 0, sizeof(exec));

	obj[exec.buffer_count++].handle = src;
	obj[exec.buffer_count].handle = handle;
	obj[exec.buffer_count].relocation_count = 0;
	obj[exec.buffer_count].relocs_ptr = (uintptr_t)0;
	exec.buffer_count++;
	exec.buffers_ptr = (uintptr_t)obj;

	exec.batch_len = i * sizeof(uint32_t);
	exec.flags = I915_EXEC_RENDER;
	i915_execbuffer2_set_context_id(exec, ctx_id);

	gem_execbuf(fd, &exec);

	return handle;
}

/*
 * A single bo is operated from batchbuffers submitted from two contexts and on
 * different rings.
 * One execbuf finishes way ahead of the other at which point the respective
 * context is destroyed.
 */
static void
test_retire_vma_not_inactive(int fd)
{
	uint32_t ctx_id;
	uint32_t src, dst;
	uint32_t blit_bb, noop_bb;

	igt_require(HAS_BLT_RING(intel_get_drm_devid(fd)));

	ctx_id = gem_context_create(fd);

	/* Create some bos batch buffers will operate on. */
	src = gem_create(fd, BO_SIZE);
	dst = gem_create(fd, BO_SIZE);

	/* Submit a long running batch. */
	blit_bb = blit(fd, dst, src, 0);

	/* Submit a quick batch referencing the same object. */
	noop_bb = noop(fd, src, ctx_id);

	/* Wait for the quick batch to complete. */
	gem_sync(fd, noop_bb);
	gem_close(fd, noop_bb);

	/* Now destroy the context in which the quick batch was submitted. */
	gem_context_destroy(fd, ctx_id);

	/* Wait for the slow batch to finish and clean up. */
	gem_sync(fd, blit_bb);
	gem_close(fd, blit_bb);

	gem_close(fd, src);
	gem_close(fd, dst);
}

int fd;

int main(int argc, char **argv)
{
	igt_subtest_init(argc, argv);

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);
		igt_assert(fd >= 0);
	}

	igt_subtest("retire-vma-not-inactive")
		test_retire_vma_not_inactive(fd);

	igt_exit();
}
