/*
 * Copyright © 2011,2013 Intel Corporation
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

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/wait.h>

#include <drm.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_chipset.h"
#include "intel_io.h"
#include "igt_debugfs.h"
#include "igt_aux.h"
#include "igt_gt.h"

IGT_TEST_DESCRIPTION("Test kernel relocations vs. gpu races.");

/*
 * Testcase: Kernel relocations vs. gpu races
 *
 */

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;

uint32_t blob[2048*2048];
#define NUM_TARGET_BOS 16
drm_intel_bo *pc_target_bo[NUM_TARGET_BOS];
drm_intel_bo *dummy_bo;
drm_intel_bo *special_bo;
uint32_t devid;
int special_reloc_ofs;
int special_batch_len;

static void create_special_bo(void)
{
	uint32_t data[1024];
	int len = 0;
	int small_pitch = 64;
#define BATCH(dw) data[len++] = (dw);

	memset(data, 0, 4096);
	special_bo = drm_intel_bo_alloc(bufmgr, "special batch", 4096, 4096);

	if (intel_gen(devid) >= 8) {
		BATCH(MI_NOOP);
		BATCH(XY_COLOR_BLT_CMD_NOLEN | 5 |
				COLOR_BLT_WRITE_ALPHA | XY_COLOR_BLT_WRITE_RGB);
	} else {
		BATCH(XY_COLOR_BLT_CMD_NOLEN | 4 |
				COLOR_BLT_WRITE_ALPHA | XY_COLOR_BLT_WRITE_RGB);
	}

	BATCH((3 << 24) | (0xf0 << 16) | small_pitch);
	BATCH(0);
	BATCH(1 << 16 | 1);
	special_reloc_ofs = 4*len;
	BATCH(0);
	if (intel_gen(devid) >= 8)
		BATCH(0);
	BATCH(0xdeadbeef);

#define CMD_POLY_STIPPLE_OFFSET       0x7906
	/* batchbuffer end */
	if (IS_GEN5(batch->devid)) {
		BATCH(CMD_POLY_STIPPLE_OFFSET << 16);
		BATCH(0);
	}
	igt_assert(len % 2 == 0);
	BATCH(MI_NOOP);
	BATCH(MI_BATCH_BUFFER_END);

	drm_intel_bo_subdata(special_bo, 0, 4096, data);
	special_batch_len = len*4;
}

static void emit_dummy_load(int pitch)
{
	int i;
	uint32_t tile_flags = 0;

	if (IS_965(devid)) {
		pitch /= 4;
		tile_flags = XY_SRC_COPY_BLT_SRC_TILED |
			XY_SRC_COPY_BLT_DST_TILED;
	}

	for (i = 0; i < 10; i++) {
		BLIT_COPY_BATCH_START(tile_flags);
		OUT_BATCH((3 << 24) | /* 32 bits */
			  (0xcc << 16) | /* copy ROP */
			  pitch);
		OUT_BATCH(0 << 16 | 1024);
		OUT_BATCH((2048) << 16 | (2048));
		OUT_RELOC_FENCED(dummy_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
		OUT_BATCH(0 << 16 | 0);
		OUT_BATCH(pitch);
		OUT_RELOC_FENCED(dummy_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
		ADVANCE_BATCH();

		if (batch->gen >= 6) {
			BEGIN_BATCH(3, 0);
			OUT_BATCH(XY_SETUP_CLIP_BLT_CMD);
			OUT_BATCH(0);
			OUT_BATCH(0);
			ADVANCE_BATCH();
		}
	}
	intel_batchbuffer_flush(batch);
}

static void reloc_and_emit(int fd, drm_intel_bo *target_bo, bool faulting_reloc)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec[2];
	struct drm_i915_gem_relocation_entry reloc[1];
	uint32_t handle_relocs;
	void *gtt_relocs;

	memset(&execbuf, 0, sizeof(execbuf));
	memset(exec, 0, sizeof(exec));
	memset(reloc, 0, sizeof(reloc));

	exec[0].handle = target_bo->handle;

	reloc[0].offset = special_reloc_ofs;
	reloc[0].target_handle = target_bo->handle;
	reloc[0].read_domains = I915_GEM_DOMAIN_RENDER;
	reloc[0].write_domain = I915_GEM_DOMAIN_RENDER;
	/* We do not track the last patched value, so force the relocation
	 * every time.
	 */
	reloc[0].presumed_offset = -1;

	handle_relocs = gem_create(fd, 4096);
	gem_write(fd, handle_relocs, 0, reloc, sizeof(reloc));
	gtt_relocs = gem_mmap(fd, handle_relocs, 4096,
			      PROT_READ | PROT_WRITE);
	igt_assert(gtt_relocs);

	exec[1].handle = special_bo->handle;
	exec[1].relocation_count = 1;
	/* A newly mmap gtt bo will fault on first access. */
	if (faulting_reloc)
		exec[1].relocs_ptr = (uintptr_t)gtt_relocs;
	else
		exec[1].relocs_ptr = (uintptr_t)reloc;

	execbuf.buffers_ptr = (uintptr_t)exec;
	execbuf.buffer_count = 2;
	execbuf.batch_len = special_batch_len;
	if (intel_gen(devid) >= 6)
		execbuf.flags |= I915_EXEC_BLT;

	gem_execbuf(fd, &execbuf);

	gem_close(fd, handle_relocs);
}

static struct igt_hang_ring no_hang(int fd)
{
	return (struct igt_hang_ring){0};
}

static struct igt_hang_ring bcs_hang(int fd)
{
	return igt_hang_ring(fd, I915_EXEC_BLT);
}

static void do_test(int fd, bool faulting_reloc,
		    struct igt_hang_ring (*do_hang)(int fd))
{
	uint32_t tiling_mode = I915_TILING_X;
	unsigned long pitch, act_size;
	uint32_t test;
	int i;

	if (faulting_reloc)
		igt_disable_prefault();

	act_size = 2048;
	dummy_bo = drm_intel_bo_alloc_tiled(bufmgr, "tiled dummy_bo", act_size, act_size,
				      4, &tiling_mode, &pitch, 0);

	drm_intel_bo_subdata(dummy_bo, 0, act_size*act_size*4, blob);

	create_special_bo();

	for (i = 0; i < NUM_TARGET_BOS; i++) {
		struct igt_hang_ring hang;

		pc_target_bo[i] = drm_intel_bo_alloc(bufmgr, "special batch", 4096, 4096);
		emit_dummy_load(pitch);
		igt_assert(pc_target_bo[i]->offset == 0);
		hang = do_hang(fd);

		reloc_and_emit(fd, pc_target_bo[i], faulting_reloc);

		igt_post_hang_ring(fd, hang);
	}

	/* Only check at the end to avoid unnecessary synchronous behaviour. */
	for (i = 0; i < NUM_TARGET_BOS; i++) {
		drm_intel_bo_get_subdata(pc_target_bo[i], 0, 4, &test);
		igt_assert_f(test == 0xdeadbeef,
			     "mismatch in buffer %i: 0x%08x instead of 0xdeadbeef\n", i, test);
		drm_intel_bo_unreference(pc_target_bo[i]);
	}

	drm_intel_gem_bo_map_gtt(dummy_bo);
	drm_intel_gem_bo_unmap_gtt(dummy_bo);

	drm_intel_bo_unreference(special_bo);
	drm_intel_bo_unreference(dummy_bo);

	if (faulting_reloc)
		igt_enable_prefault();
}

#define INTERRUPT	(1 << 0)
#define FAULTING	(1 << 1)
#define THRASH		(1 << 2)
#define THRASH_INACTIVE	(1 << 3)
#define HANG		(1 << 4)
#define ALL_FLAGS	(HANG | INTERRUPT | FAULTING | THRASH | THRASH_INACTIVE)
static void do_forked_test(int fd, unsigned flags)
{
	int num_threads = sysconf(_SC_NPROCESSORS_ONLN);
	struct igt_helper_process thrasher = {};

	if (flags & HANG)
		igt_require_hang_ring(fd, I915_EXEC_BLT);

	if (flags & (THRASH | THRASH_INACTIVE)) {
		uint64_t val = (flags & THRASH_INACTIVE) ?
				(DROP_RETIRE | DROP_BOUND | DROP_UNBOUND) : DROP_ALL;

		igt_fork_helper(&thrasher) {
			while (1) {
				usleep(1000);
				igt_drop_caches_set(val);
			}
		}
	}

	igt_fork(i, num_threads * 4) {
		/* re-create process local data */
		fd = drm_open_any();
		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		batch = intel_batchbuffer_alloc(bufmgr, devid);

		if (flags & INTERRUPT)
			igt_fork_signal_helper();

		do_test(fd, flags & FAULTING, flags & HANG ? bcs_hang : no_hang);

		if (flags & INTERRUPT)
			igt_stop_signal_helper();
	}

	igt_waitchildren();
	if (flags & (THRASH | THRASH_INACTIVE))
		igt_stop_helper(&thrasher);
}

int fd;

#define MAX_BLT_SIZE 128
igt_main
{
	igt_skip_on_simulation();

	memset(blob, 'A', sizeof(blob));

	igt_fixture {
		fd = drm_open_any();
		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		/* disable reuse, otherwise the test fails */
		//drm_intel_bufmgr_gem_enable_reuse(bufmgr);
		devid = intel_get_drm_devid(fd);
		batch = intel_batchbuffer_alloc(bufmgr, devid);
	}

	igt_subtest("normal")
		do_test(fd, false, no_hang);

	igt_subtest("faulting-reloc")
		do_test(fd, true, no_hang);

	igt_fork_signal_helper();
	igt_subtest("interruptible")
		do_test(fd, false, no_hang);

	igt_subtest("interruptible-hang") {
		igt_require_hang_ring(fd, I915_EXEC_BLT);
		do_test(fd, false, bcs_hang);
	}

	igt_subtest("faulting-reloc-interruptible")
		do_test(fd, true, no_hang);

	igt_subtest("faulting-reloc-interruptible-hang") {
		igt_require_hang_ring(fd, I915_EXEC_BLT);
		do_test(fd, true, bcs_hang);
	}
	igt_stop_signal_helper();

	for (unsigned flags = 0; flags <= ALL_FLAGS; flags++) {
		if ((flags & THRASH) && (flags & THRASH_INACTIVE))
			continue;

		igt_subtest_f("forked%s%s%s%s%s",
			      flags & INTERRUPT ? "-interruptible" : "",
			      flags & FAULTING ? "-faulting-reloc" : "",
			      flags & THRASH ? "-thrashing" : "",
			      flags & THRASH_INACTIVE ? "-thrash-inactive" : "",
			      flags & HANG ? "-hang": "")
			do_forked_test(fd, flags);
	}

	igt_fixture {
		intel_batchbuffer_free(batch);
		drm_intel_bufmgr_destroy(bufmgr);

		close(fd);
	}
}
