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
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

/** @file gem_linear_render_blits.c
 *
 * This is a test of doing many blits, with a working set
 * larger than the aperture size.
 *
 * The goal is to simply ensure the basics work.
 */

#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <getopt.h>

#include <drm.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"
#include "intel_chipset.h"
#include "igt_aux.h"

#define WIDTH 512
#define STRIDE (WIDTH*4)
#define HEIGHT 512
#define SIZE (HEIGHT*STRIDE)

static igt_render_copyfunc_t render_copy;
static drm_intel_bo *linear;
static uint32_t data[WIDTH*HEIGHT];
static int snoop;

static void
check_bo(struct intel_batchbuffer *batch, struct igt_buf *buf, uint32_t val)
{
	struct igt_buf tmp;
	uint32_t *ptr;
	int i;

	tmp.bo = linear;
	tmp.stride = STRIDE;
	tmp.tiling = I915_TILING_NONE;
	tmp.size = SIZE;

	render_copy(batch, NULL, buf, 0, 0, WIDTH, HEIGHT, &tmp, 0, 0);
	if (snoop) {
		do_or_die(dri_bo_map(linear, 0));
		ptr = linear->virtual;
	} else {
		do_or_die(drm_intel_bo_get_subdata(linear, 0, sizeof(data), data));
		ptr = data;
	}
	for (i = 0; i < WIDTH*HEIGHT; i++) {
		igt_assert_f(ptr[i] == val,
			     "Expected 0x%08x, found 0x%08x "
			     "at offset 0x%08x\n",
			     val, ptr[i], i * 4);
		val++;
	}
	if (ptr != data)
		dri_bo_unmap(linear);
}

int main(int argc, char **argv)
{
	drm_intel_bufmgr *bufmgr;
	struct intel_batchbuffer *batch;
	uint32_t *start_val;
	struct igt_buf *buf;
	uint32_t start = 0;
	int i, j, fd, count;
	uint32_t devid;

	igt_simple_init();

	igt_skip_on_simulation();

	fd = drm_open_any();
	devid = intel_get_drm_devid(fd);

	render_copy = igt_get_render_copyfunc(devid);
	igt_require(render_copy);

	snoop = 1;
	if (IS_GEN2(devid)) /* chipset only handles cached -> uncached */
		snoop = 0;
	if (IS_BROADWATER(devid) || IS_CRESTLINE(devid)) /* snafu */
		snoop = 0;

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_set_vma_cache_size(bufmgr, 32);
	batch = intel_batchbuffer_alloc(bufmgr, devid);

	count = 0;
	if (argc > 1)
		count = atoi(argv[1]);
	if (count == 0)
		count = 3 * gem_aperture_size(fd) / SIZE / 2;
	else if (count < 2) {
		fprintf(stderr, "count must be >= 2\n");
		return 1;
	}

	if (count > intel_get_total_ram_mb() * 9 / 10) {
		count = intel_get_total_ram_mb() * 9 / 10;
		igt_info("not enough RAM to run test, reducing buffer count\n");
	}

	igt_info("Using %d 1MiB buffers\n", count);

	linear = drm_intel_bo_alloc(bufmgr, "linear", WIDTH*HEIGHT*4, 0);
	if (snoop) {
		gem_set_caching(fd, linear->handle, 1);
		igt_info("Using a snoop linear buffer for comparisons\n");
	}

	buf = malloc(sizeof(*buf)*count);
	start_val = malloc(sizeof(*start_val)*count);

	for (i = 0; i < count; i++) {
		uint32_t tiling = I915_TILING_X + (random() & 1);
		unsigned long pitch = STRIDE;
		uint32_t *ptr;

		buf[i].bo = drm_intel_bo_alloc_tiled(bufmgr, "",
						     WIDTH, HEIGHT, 4,
						     &tiling, &pitch, 0);
		buf[i].stride = pitch;
		buf[i].tiling = tiling;
		buf[i].size = SIZE;

		start_val[i] = start;

		do_or_die(drm_intel_gem_bo_map_gtt(buf[i].bo));
		ptr = buf[i].bo->virtual;
		for (j = 0; j < WIDTH*HEIGHT; j++)
			ptr[j] = start++;
		drm_intel_gem_bo_unmap_gtt(buf[i].bo);
	}

	igt_info("Verifying initialisation...\n");
	for (i = 0; i < count; i++)
		check_bo(batch, &buf[i], start_val[i]);

	igt_info("Cyclic blits, forward...\n");
	for (i = 0; i < count * 4; i++) {
		int src = i % count;
		int dst = (i + 1) % count;

		render_copy(batch, NULL, buf+src, 0, 0, WIDTH, HEIGHT, buf+dst, 0, 0);
		start_val[dst] = start_val[src];
	}
	for (i = 0; i < count; i++)
		check_bo(batch, &buf[i], start_val[i]);

	igt_info("Cyclic blits, backward...\n");
	for (i = 0; i < count * 4; i++) {
		int src = (i + 1) % count;
		int dst = i % count;

		render_copy(batch, NULL, buf+src, 0, 0, WIDTH, HEIGHT, buf+dst, 0, 0);
		start_val[dst] = start_val[src];
	}
	for (i = 0; i < count; i++)
		check_bo(batch, &buf[i], start_val[i]);

	igt_info("Random blits...\n");
	for (i = 0; i < count * 4; i++) {
		int src = random() % count;
		int dst = random() % count;

		if (src == dst)
			continue;

		render_copy(batch, NULL, buf+src, 0, 0, WIDTH, HEIGHT, buf+dst, 0, 0);
		start_val[dst] = start_val[src];
	}
	for (i = 0; i < count; i++)
		check_bo(batch, &buf[i], start_val[i]);

	return 0;
}
