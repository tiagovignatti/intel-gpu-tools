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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"
#include "i830_reg.h"
#include "intel_chipset.h"

#define LOCAL_I915_EXEC_VEBOX (4<<0)

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
static drm_intel_bo *target_buffer;

#define NUM_FD	50

static int mfd[NUM_FD];
static drm_intel_bufmgr *mbufmgr[NUM_FD];
static struct intel_batchbuffer *mbatch[NUM_FD];
static drm_intel_bo *mbuffer[NUM_FD];

/*
 * Testcase: Basic check of ring<->cpu sync using a dummy reloc
 *
 * The last test (that randomly switches the ring) seems to be pretty effective
 * at hitting the missed irq bug that's worked around with the HWSTAM irq write.
 */


#define MI_COND_BATCH_BUFFER_END	(0x36<<23 | 1)
#define MI_DO_COMPARE			(1<<21)
static void
dummy_reloc_loop(int ring)
{
	int i;

	for (i = 0; i < 0x100000; i++) {
		if (ring == I915_EXEC_RENDER) {
			BEGIN_BATCH(4);
			OUT_BATCH(MI_COND_BATCH_BUFFER_END | MI_DO_COMPARE);
			OUT_BATCH(0xffffffff); /* compare dword */
			OUT_RELOC(target_buffer, I915_GEM_DOMAIN_RENDER,
					I915_GEM_DOMAIN_RENDER, 0);
			OUT_BATCH(MI_NOOP);
			ADVANCE_BATCH();
		} else {
			BEGIN_BATCH(4);
			OUT_BATCH(MI_FLUSH_DW | 1);
			OUT_BATCH(0); /* reserved */
			OUT_RELOC(target_buffer, I915_GEM_DOMAIN_RENDER,
					I915_GEM_DOMAIN_RENDER, 0);
			OUT_BATCH(MI_NOOP | (1<<22) | (0xf));
			ADVANCE_BATCH();
		}
		intel_batchbuffer_flush_on_ring(batch, ring);

		drm_intel_bo_map(target_buffer, 0);
		// map to force completion
		drm_intel_bo_unmap(target_buffer);
	}
}

static void
dummy_reloc_loop_random_ring(int num_rings)
{
	int i;

	srandom(0xdeadbeef);

	for (i = 0; i < 0x100000; i++) {
		int ring = random() % num_rings + 1;

		if (ring == I915_EXEC_RENDER) {
			BEGIN_BATCH(4);
			OUT_BATCH(MI_COND_BATCH_BUFFER_END | MI_DO_COMPARE);
			OUT_BATCH(0xffffffff); /* compare dword */
			OUT_RELOC(target_buffer, I915_GEM_DOMAIN_RENDER,
					I915_GEM_DOMAIN_RENDER, 0);
			OUT_BATCH(MI_NOOP);
			ADVANCE_BATCH();
		} else {
			BEGIN_BATCH(4);
			OUT_BATCH(MI_FLUSH_DW | 1);
			OUT_BATCH(0); /* reserved */
			OUT_RELOC(target_buffer, I915_GEM_DOMAIN_RENDER,
					I915_GEM_DOMAIN_RENDER, 0);
			OUT_BATCH(MI_NOOP | (1<<22) | (0xf));
			ADVANCE_BATCH();
		}
		intel_batchbuffer_flush_on_ring(batch, ring);

		drm_intel_bo_map(target_buffer, 0);
		// map to force waiting on rendering
		drm_intel_bo_unmap(target_buffer);
	}
}

static void
dummy_reloc_loop_random_ring_multi_fd(int num_rings)
{
	int i;
	struct intel_batchbuffer *saved_batch;

	saved_batch = batch;

	srandom(0xdeadbeef);

	for (i = 0; i < 0x100000; i++) {
		int mindex;
		int ring = random() % num_rings + 1;

		mindex = random() % NUM_FD;
		batch = mbatch[mindex];

		if (ring == I915_EXEC_RENDER) {
			BEGIN_BATCH(4);
			OUT_BATCH(MI_COND_BATCH_BUFFER_END | MI_DO_COMPARE);
			OUT_BATCH(0xffffffff); /* compare dword */
			OUT_RELOC(mbuffer[mindex], I915_GEM_DOMAIN_RENDER,
					I915_GEM_DOMAIN_RENDER, 0);
			OUT_BATCH(MI_NOOP);
			ADVANCE_BATCH();
		} else {
			BEGIN_BATCH(4);
			OUT_BATCH(MI_FLUSH_DW | 1);
			OUT_BATCH(0); /* reserved */
			OUT_RELOC(mbuffer[mindex], I915_GEM_DOMAIN_RENDER,
					I915_GEM_DOMAIN_RENDER, 0);
			OUT_BATCH(MI_NOOP | (1<<22) | (0xf));
			ADVANCE_BATCH();
		}
		intel_batchbuffer_flush_on_ring(batch, ring);

		drm_intel_bo_map(target_buffer, 0);
		// map to force waiting on rendering
		drm_intel_bo_unmap(target_buffer);
	}

	batch = saved_batch;
}

int fd;
int devid;
int num_rings;

igt_main
{
	igt_skip_on_simulation();

	igt_fixture {
		int i;
		fd = drm_open_any();
		devid = intel_get_drm_devid(fd);
		num_rings = gem_get_num_rings(fd);
		/* Not yet implemented on pre-snb. */
		igt_require(HAS_BLT_RING(devid));

		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		igt_assert(bufmgr);
		drm_intel_bufmgr_gem_enable_reuse(bufmgr);

		batch = intel_batchbuffer_alloc(bufmgr, devid);
		igt_assert(batch);

		target_buffer = drm_intel_bo_alloc(bufmgr, "target bo", 4096, 4096);
		igt_assert(target_buffer);

		/* Create multi drm_fd and map one gem object to multi gem_contexts */
		{
			unsigned int target_flink;
			char buffer_name[32];
			igt_assert(dri_bo_flink(target_buffer, &target_flink) == 0);

			for (i = 0; i < NUM_FD; i++) {
				sprintf(buffer_name, "Target buffer %d\n", i);
				mfd[i] = drm_open_any();
				mbufmgr[i] = drm_intel_bufmgr_gem_init(mfd[i], 4096);
				igt_assert_f(mbufmgr[i],
					     "fail to initialize buf manager "
					     "for drm_fd %d\n",
					     mfd[i]);
				drm_intel_bufmgr_gem_enable_reuse(mbufmgr[i]);
				mbatch[i] = intel_batchbuffer_alloc(mbufmgr[i], devid);
				igt_assert_f(mbatch[i],
					     "fail to create batchbuffer "
					     "for drm_fd %d\n",
					     mfd[i]);
				mbuffer[i] = intel_bo_gem_create_from_name(
								mbufmgr[i],
								buffer_name,
								target_flink);
				igt_assert_f(mbuffer[i],
					     "fail to create gem bo from global "
					     "gem_handle %d for drm_fd %d\n",
					     target_flink, mfd[i]);
			}
		}
	}

	igt_subtest("render") {
		igt_info("running dummy loop on render\n");
		dummy_reloc_loop(I915_EXEC_RENDER);
		igt_info("dummy loop run on render completed\n");
	}

	igt_subtest("bsd") {
		gem_require_ring(fd, I915_EXEC_BSD);
		sleep(2);
		igt_info("running dummy loop on bsd\n");
		dummy_reloc_loop(I915_EXEC_BSD);
		igt_info("dummy loop run on bsd completed\n");
	}

	igt_subtest("blt") {
		gem_require_ring(fd, I915_EXEC_BLT);
		sleep(2);
		igt_info("running dummy loop on blt\n");
		dummy_reloc_loop(I915_EXEC_BLT);
		igt_info("dummy loop run on blt completed\n");
	}

#ifdef I915_EXEC_VEBOX
	igt_subtest("vebox") {
		gem_require_ring(fd, I915_EXEC_VEBOX);
		sleep(2);
		igt_info("running dummy loop on vebox\n");
		dummy_reloc_loop(LOCAL_I915_EXEC_VEBOX);
		igt_info("dummy loop run on vebox completed\n");
	}
#endif

	igt_subtest("mixed") {
		if (num_rings > 1) {
			sleep(2);
			igt_info("running dummy loop on random rings\n");
			dummy_reloc_loop_random_ring(num_rings);
			igt_info("dummy loop run on random rings completed\n");
		}
	}
	igt_subtest("mixed_multi_fd") {
		if (num_rings > 1) {
			sleep(2);
			igt_info("running dummy loop on random rings based on "
					"multi drm_fd\n");
			dummy_reloc_loop_random_ring_multi_fd(num_rings);
			igt_info("dummy loop run on random rings based on "
					"multi drm_fd completed\n");
		}
	}
	igt_fixture {
		int i;
		/* Free the buffer/batchbuffer/buffer mgr for multi-fd */
		{
			for (i = 0; i < NUM_FD; i++) {
				dri_bo_unreference(mbuffer[i]);
				intel_batchbuffer_free(mbatch[i]);
				drm_intel_bufmgr_destroy(mbufmgr[i]);
				close(mfd[i]);
			}
		}
		drm_intel_bo_unreference(target_buffer);
		intel_batchbuffer_free(batch);
		drm_intel_bufmgr_destroy(bufmgr);

		close(fd);
	}
}
