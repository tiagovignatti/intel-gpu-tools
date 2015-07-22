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
#include "intel_bufmgr.h"

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
static int bad_pipe;

static void
gpu_hang(void)
{
	int cmd;

	cmd = bad_pipe ? MI_WAIT_FOR_PIPEB_SCAN_LINE_WINDOW :
		MI_WAIT_FOR_PIPEA_SCAN_LINE_WINDOW;

	BEGIN_BATCH(6, 0);
	/* The documentation says that the LOAD_SCAN_LINES command
	 * always comes in pairs. Don't ask me why. */
	OUT_BATCH(MI_LOAD_SCAN_LINES_INCL | (bad_pipe << 20));
	OUT_BATCH((0 << 16) | 2048);
	OUT_BATCH(MI_LOAD_SCAN_LINES_INCL | (bad_pipe << 20));
	OUT_BATCH((0 << 16) | 2048);
	OUT_BATCH(MI_WAIT_FOR_EVENT | cmd);
	OUT_BATCH(MI_NOOP);
	ADVANCE_BATCH();

	intel_batchbuffer_flush(batch);
}

int main(int argc, char **argv)
{
	int fd;

	igt_simple_init(argc, argv);

	igt_assert_f(argc == 2,
		     "usage: %s <disabled pipe number>\n",
		     argv[0]);

	bad_pipe = atoi(argv[1]);

	fd = drm_open_driver(DRIVER_INTEL);

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));

	gpu_hang();

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	igt_exit();
}
