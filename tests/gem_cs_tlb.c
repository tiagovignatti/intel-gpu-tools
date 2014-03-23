/*
 * Copyright © 2011,2012 Intel Corporation
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
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

/*
 * Testcase: Check whether we correctly invalidate the cs tlb
 *
 * Motivated by a strange bug on launchpad where *acth != ipehr, on snb notably
 * where everything should be coherent by default.
 *
 * https://bugs.launchpad.net/ubuntu/+source/xserver-xorg-video-intel/+bug/1063252
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <drm.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_io.h"
#include "igt_aux.h"

#define LOCAL_I915_EXEC_VEBOX (4<<0)
#define BATCH_SIZE (1024*1024)

static int exec(int fd, uint32_t handle, int split,
		uint64_t *gtt_ofs, unsigned ring_id)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];
	int ret = 0;

	gem_exec[0].handle = handle;
	gem_exec[0].relocation_count = 0;
	gem_exec[0].relocs_ptr = 0;
	gem_exec[0].alignment = 0;
	gem_exec[0].offset = 0x00100000;
	gem_exec[0].flags = 0;
	gem_exec[0].rsvd1 = 0;
	gem_exec[0].rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = 8*(split+1);
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = ring_id;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	ret = drmIoctl(fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER2,
		       &execbuf);

	*gtt_ofs = gem_exec[0].offset;

	return ret;
}

static void run_on_ring(int fd, unsigned ring_id, const char *ring_name)
{
	uint32_t handle, handle_new;
	uint64_t gtt_offset, gtt_offset_new;
	uint32_t *batch_ptr, *batch_ptr_old;
	unsigned split;
	char buf[100];
	int i;

	gem_require_ring(fd, ring_id);

	sprintf(buf, "testing %s cs tlb coherency: ", ring_name);

	/* Shut up gcc, too stupid. */
	batch_ptr_old = NULL;
	handle = 0;
	gtt_offset = 0;

	for (split = 0; split < BATCH_SIZE/8 - 1; split += 2) {
		igt_progress(buf, split, BATCH_SIZE/8 - 1);

		handle_new = gem_create(fd, BATCH_SIZE);
		batch_ptr = gem_mmap__cpu(fd, handle_new, BATCH_SIZE,
					  PROT_READ | PROT_WRITE);
		batch_ptr[split*2] = MI_BATCH_BUFFER_END;

		for (i = split*2 + 2; i < BATCH_SIZE/8; i++)
			batch_ptr[i] = 0xffffffff;

		if (split > 0) {
			gem_sync(fd, handle);
			gem_close(fd, handle);
		}

		igt_assert(exec(fd, handle_new, split, &gtt_offset_new, 0) == 0);

		if (split > 0) {
			/* Check that we've managed to collide in the tlb. */
			igt_assert(gtt_offset == gtt_offset_new);

			/* We hang onto the storage of the old batch by keeping
			 * the cpu mmap around. */
			munmap(batch_ptr_old, BATCH_SIZE);
		}

		handle = handle_new;
		gtt_offset = gtt_offset_new;
		batch_ptr_old = batch_ptr;
	}

}

int fd;

igt_main
{

	igt_skip_on_simulation();

	igt_fixture {
		fd = drm_open_any();

		/* This test is very sensitive to residual gtt_mm noise from previous
		 * tests. Try to quiet thing down first. */
		gem_quiescent_gpu(fd);
		sleep(5); /* needs more serious ducttape */
	}

	igt_subtest("render")
		run_on_ring(fd, I915_EXEC_RENDER, "render");

	igt_subtest("bsd")
		run_on_ring(fd, I915_EXEC_BSD, "bsd");

	igt_subtest("blt")
		run_on_ring(fd, I915_EXEC_BLT, "blt");

	igt_subtest("vebox")
		run_on_ring(fd, LOCAL_I915_EXEC_VEBOX, "vebox");

	igt_fixture
		close(fd);
}
