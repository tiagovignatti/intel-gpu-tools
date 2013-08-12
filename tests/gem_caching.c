/*
 * Copyright © 2012 Intel Corporation
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
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_gpu_tools.h"

/*
 * Testcase: snoop consistency when touching partial cachelines
 *
 */

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;

drm_intel_bo *scratch_bo;
drm_intel_bo *staging_bo;
#define BO_SIZE (4*4096)
uint32_t devid;
uint64_t mappable_gtt_limit;
int fd;

static void
copy_bo(drm_intel_bo *src, drm_intel_bo *dst)
{
	BEGIN_BATCH(8);
	OUT_BATCH(XY_SRC_COPY_BLT_CMD |
		  XY_SRC_COPY_BLT_WRITE_ALPHA |
		  XY_SRC_COPY_BLT_WRITE_RGB);
	OUT_BATCH((3 << 24) | /* 32 bits */
		  (0xcc << 16) | /* copy ROP */
		  4096);
	OUT_BATCH(0 << 16 | 0);
	OUT_BATCH((BO_SIZE/4096) << 16 | 1024);
	OUT_RELOC_FENCED(dst, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(0 << 16 | 0);
	OUT_BATCH(4096);
	OUT_RELOC_FENCED(src, I915_GEM_DOMAIN_RENDER, 0, 0);
	ADVANCE_BATCH();

	intel_batchbuffer_flush(batch);
}

static void
blt_bo_fill(drm_intel_bo *tmp_bo, drm_intel_bo *bo, uint8_t val)
{
	uint8_t *gtt_ptr;
	int i;

	do_or_die(drm_intel_gem_bo_map_gtt(tmp_bo));
	gtt_ptr = tmp_bo->virtual;

	for (i = 0; i < BO_SIZE; i++)
		gtt_ptr[i] = val;

	drm_intel_gem_bo_unmap_gtt(tmp_bo);

	if (bo->offset < mappable_gtt_limit &&
	    (IS_G33(devid) || intel_gen(devid) >= 4))
		drmtest_trash_aperture();

	copy_bo(tmp_bo, bo);
}

#define MAX_BLT_SIZE 128
#define ROUNDS 1000
#define TEST_READ 0x1
#define TEST_WRITE 0x2
#define TEST_BOTH (TEST_READ | TEST_WRITE)
int main(int argc, char **argv)
{
	unsigned flags = TEST_BOTH;
	int i, j;
	uint8_t *cpu_ptr;
	uint8_t *gtt_ptr;

	drmtest_subtest_init(argc, argv);
	drmtest_skip_on_simulation();

	srandom(0xdeadbeef);

	fd = drm_open_any();

	if (!gem_has_caching(fd)) {
		printf("no set_caching support detected\n");
		return 77;
	}

	devid = intel_get_drm_devid(fd);
	if (IS_GEN2(devid)) /* chipset only handles cached -> uncached */
		flags &= ~TEST_READ;
	if (IS_BROADWATER(devid) || IS_CRESTLINE(devid)) {
		/* chipset is completely fubar */
		printf("coherency broken on i965g/gm\n");
		flags = 0;
	}

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	batch = intel_batchbuffer_alloc(bufmgr, devid);

	/* overallocate the buffers we're actually using because */
	scratch_bo = drm_intel_bo_alloc(bufmgr, "scratch bo", BO_SIZE, 4096);
	gem_set_caching(fd, scratch_bo->handle, 1);

	staging_bo = drm_intel_bo_alloc(bufmgr, "staging bo", BO_SIZE, 4096);

	drmtest_init_aperture_trashers(bufmgr);
	mappable_gtt_limit = gem_mappable_aperture_size();

	drmtest_subtest_block("reads") {
		if (!(flags & TEST_READ))
			drmtest_skip();

		printf("checking partial reads\n");

		for (i = 0; i < ROUNDS; i++) {
			uint8_t val0 = i;
			int start, len;

			blt_bo_fill(staging_bo, scratch_bo, i);

			start = random() % BO_SIZE;
			len = random() % (BO_SIZE-start) + 1;

			drm_intel_bo_map(scratch_bo, false);
			cpu_ptr = scratch_bo->virtual;
			for (j = 0; j < len; j++) {
				if (cpu_ptr[j] != val0) {
					printf("mismatch at %i, got: %i, expected: %i\n",
					       j, cpu_ptr[j], val0);
					exit(1);
				}
			}
			drm_intel_bo_unmap(scratch_bo);

			drmtest_progress("partial reads test: ", i, ROUNDS);
		}
	}

	drmtest_subtest_block("writes") {
		if (!(flags & TEST_WRITE))
			drmtest_skip();

		printf("checking partial writes\n");

		for (i = 0; i < ROUNDS; i++) {
			uint8_t val0 = i, val1;
			int start, len;

			blt_bo_fill(staging_bo, scratch_bo, val0);

			start = random() % BO_SIZE;
			len = random() % (BO_SIZE-start) + 1;

			val1 = val0 + 63;
			drm_intel_bo_map(scratch_bo, true);
			cpu_ptr = scratch_bo->virtual;
			memset(cpu_ptr + start, val1, len);
			drm_intel_bo_unmap(scratch_bo);

			copy_bo(scratch_bo, staging_bo);
			do_or_die(drm_intel_gem_bo_map_gtt(staging_bo));
			gtt_ptr = staging_bo->virtual;

			for (j = 0; j < start; j++) {
				if (gtt_ptr[j] != val0) {
					printf("mismatch at %i, partial=[%d+%d] got: %i, expected: %i\n",
					       j, start, len, gtt_ptr[j], val0);
					exit(1);
				}
			}
			for (; j < start + len; j++) {
				if (gtt_ptr[j] != val1) {
					printf("mismatch at %i, partial=[%d+%d] got: %i, expected: %i\n",
					       j, start, len, gtt_ptr[j], val1);
					exit(1);
				}
			}
			for (; j < BO_SIZE; j++) {
				if (gtt_ptr[j] != val0) {
					printf("mismatch at %i, partial=[%d+%d] got: %i, expected: %i\n",
					       j, start, len, gtt_ptr[j], val0);
					exit(1);
				}
			}
			drm_intel_gem_bo_unmap_gtt(staging_bo);

			drmtest_progress("partial writes test: ", i, ROUNDS);
		}
	}

	drmtest_subtest_block("read-writes") {
		if (!((flags & TEST_BOTH) == TEST_BOTH))
			drmtest_skip();

		printf("checking partial writes after partial reads\n");

		for (i = 0; i < ROUNDS; i++) {
			uint8_t val0 = i, val1, val2;
			int start, len;

			blt_bo_fill(staging_bo, scratch_bo, val0);

			/* partial read */
			start = random() % BO_SIZE;
			len = random() % (BO_SIZE-start) + 1;

			do_or_die(drm_intel_bo_map(scratch_bo, false));
			cpu_ptr = scratch_bo->virtual;
			for (j = 0; j < len; j++) {
				if (cpu_ptr[j] != val0) {
					printf("mismatch in read at %i, got: %i, expected: %i\n",
					       j, cpu_ptr[j], val0);
					exit(1);
				}
			}
			drm_intel_bo_unmap(scratch_bo);

			/* Change contents through gtt to make the pread cachelines
			 * stale. */
			val1 = i + 17;
			blt_bo_fill(staging_bo, scratch_bo, val1);

			/* partial write */
			start = random() % BO_SIZE;
			len = random() % (BO_SIZE-start) + 1;

			val2 = i + 63;
			do_or_die(drm_intel_bo_map(scratch_bo, false));
			cpu_ptr = scratch_bo->virtual;
			memset(cpu_ptr + start, val2, len);

			copy_bo(scratch_bo, staging_bo);
			do_or_die(drm_intel_gem_bo_map_gtt(staging_bo));
			gtt_ptr = staging_bo->virtual;

			for (j = 0; j < start; j++) {
				if (gtt_ptr[j] != val1) {
					printf("mismatch at %i, partial=[%d+%d] got: %i, expected: %i\n",
					       j, start, len, gtt_ptr[j], val1);
					exit(1);
				}
			}
			for (; j < start + len; j++) {
				if (gtt_ptr[j] != val2) {
					printf("mismatch at %i, partial=[%d+%d] got: %i, expected: %i\n",
					       j, start, len, gtt_ptr[j], val2);
					exit(1);
				}
			}
			for (; j < BO_SIZE; j++) {
				if (gtt_ptr[j] != val1) {
					printf("mismatch at %i, partial=[%d+%d] got: %i, expected: %i\n",
					       j, start, len, gtt_ptr[j], val1);
					exit(1);
				}
			}
			drm_intel_gem_bo_unmap_gtt(staging_bo);
			drm_intel_bo_unmap(scratch_bo);

			drmtest_progress("partial read/writes test: ", i, ROUNDS);
		}
	}

	drmtest_cleanup_aperture_trashers();
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return drmtest_retval();
}
