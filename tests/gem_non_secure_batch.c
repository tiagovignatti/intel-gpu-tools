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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "drm.h"
#include "intel_bufmgr.h"
#include "i830_reg.h"

IGT_TEST_DESCRIPTION("Basic check of non-secure batches.");

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;

/*
 * Testcase: Basic check of non-secure batches
 *
 * This test tries to stop the render ring with a MI_LOAD_REG command, which
 * should fail if the non-secure handling works correctly.
 */

static int num_rings = 1;

static void
mi_lri_loop(void)
{
	int i;

	srandom(0xdeadbeef);

	for (i = 0; i < 0x100; i++) {
		int ring = random() % num_rings + 1;

		BEGIN_BATCH(4, 0);
		OUT_BATCH(MI_LOAD_REGISTER_IMM);
		OUT_BATCH(0x203c); /* RENDER RING CTL */
		OUT_BATCH(0); /* try to stop the ring */
		OUT_BATCH(MI_NOOP);
		ADVANCE_BATCH();

		intel_batchbuffer_flush_on_ring(batch, ring);
	}
}

igt_simple_main
{
	int fd;
	int devid;

	fd = drm_open_driver(DRIVER_INTEL);
	devid = intel_get_drm_devid(fd);

	if (HAS_BSD_RING(devid))
		num_rings++;

	if (HAS_BLT_RING(devid))
		num_rings++;


	igt_info("num rings detected: %i\n", num_rings);

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	igt_assert(bufmgr);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	batch = intel_batchbuffer_alloc(bufmgr, devid);
	igt_assert(batch);

	mi_lri_loop();
	gem_quiescent_gpu(fd);

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);
}
