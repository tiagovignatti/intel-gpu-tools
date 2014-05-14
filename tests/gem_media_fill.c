/*
 * Copyright © 2013 Intel Corporation
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
 *    Damien Lespiau <damien.lespiau@intel.com>
 *    Xiang, Haihao <haihao.xiang@intel.com>
 */

/*
 * This file is a basic test for the media_fill() function, a very simple
 * workload for the Media pipeline.
 */

#include <stdbool.h>
#include <unistd.h>
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
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"
#include "intel_chipset.h"

#define WIDTH 64
#define STRIDE (WIDTH)
#define HEIGHT 64
#define SIZE (HEIGHT*STRIDE)

#define COLOR_C4	0xc4
#define COLOR_4C	0x4c

typedef struct {
	int drm_fd;
	uint32_t devid;
	drm_intel_bufmgr *bufmgr;
	uint8_t linear[WIDTH * HEIGHT];
} data_t;

static void scratch_buf_init(data_t *data, struct igt_buf *buf,
			int width, int height, int stride, uint8_t color)
{
	drm_intel_bo *bo;
	int i;

	bo = drm_intel_bo_alloc(data->bufmgr, "", SIZE, 4096);
	for (i = 0; i < width * height; i++)
		data->linear[i] = color;
	gem_write(data->drm_fd, bo->handle, 0, data->linear,
		sizeof(data->linear));

	buf->bo = bo;
	buf->stride = stride;
	buf->tiling = I915_TILING_NONE;
	buf->size = SIZE;
}

static void
scratch_buf_check(data_t *data, struct igt_buf *buf, int x, int y,
		uint8_t color)
{
	uint8_t val;

	gem_read(data->drm_fd, buf->bo->handle, 0,
		data->linear, sizeof(data->linear));
	val = data->linear[y * WIDTH + x];
	igt_assert_f(val == color,
		     "Expected 0x%02x, found 0x%02x at (%d,%d)\n",
		     color, val, x, y);
}

igt_simple_main
{
	data_t data = {0, };
	struct intel_batchbuffer *batch = NULL;
	struct igt_buf dst;
	igt_media_fillfunc_t media_fill = NULL;
	int i, j;

	data.drm_fd = drm_open_any_render();
	data.devid = intel_get_drm_devid(data.drm_fd);

	data.bufmgr = drm_intel_bufmgr_gem_init(data.drm_fd, 4096);
	igt_assert(data.bufmgr);

	media_fill = igt_get_media_fillfunc(data.devid);

	igt_require_f(media_fill,
		"no media-fill function\n");

	batch = intel_batchbuffer_alloc(data.bufmgr, data.devid);
	igt_assert(batch);

	scratch_buf_init(&data, &dst, WIDTH, HEIGHT, STRIDE, COLOR_C4);

	for (i = 0; i < WIDTH; i++) {
		for (j = 0; j < HEIGHT; j++) {
			scratch_buf_check(&data, &dst, i, j, COLOR_C4);
		}
	}

	media_fill(batch,
		&dst, 0, 0, WIDTH / 2, HEIGHT / 2,
		COLOR_4C);

	for (i = 0; i < WIDTH; i++) {
		for (j = 0; j < HEIGHT; j++) {
			if (i < WIDTH / 2 && j < HEIGHT / 2)
				scratch_buf_check(&data, &dst, i, j, COLOR_4C);
			else
				scratch_buf_check(&data, &dst, i, j, COLOR_C4);
		}
	}
}
