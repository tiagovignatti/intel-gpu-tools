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
 */

/*
 * This file is a basic test for the render_copy() function, a very simple
 * workload for the 3D engine.
 */

#include <stdbool.h>
#include <unistd.h>
#include <cairo.h>
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

#define SRC_COLOR	0xffff00ff
#define DST_COLOR	0xfff0ff00

typedef struct {
	int drm_fd;
	uint32_t devid;
	drm_intel_bufmgr *bufmgr;
	uint32_t linear[WIDTH * HEIGHT];
} data_t;

static void scratch_buf_write_to_png(struct igt_buf *buf, const char *filename)
{
	cairo_surface_t *surface;
	cairo_status_t ret;

	drm_intel_bo_map(buf->bo, 0);
	surface = cairo_image_surface_create_for_data(buf->bo->virtual,
						      CAIRO_FORMAT_RGB24,
						      igt_buf_width(buf),
						      igt_buf_height(buf),
						      buf->stride);
	ret = cairo_surface_write_to_png(surface, filename);
	igt_assert(ret == CAIRO_STATUS_SUCCESS);
	cairo_surface_destroy(surface);
	drm_intel_bo_unmap(buf->bo);
}

static void scratch_buf_init(data_t *data, struct igt_buf *buf,
			     int width, int height, int stride, uint32_t color)
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
		  uint32_t color)
{
	uint32_t val;

	gem_read(data->drm_fd, buf->bo->handle, 0,
		 data->linear, sizeof(data->linear));
	val = data->linear[y * WIDTH + x];
	igt_assert_f(val == color,
		     "Expected 0x%08x, found 0x%08x at (%d,%d)\n",
		     color, val, x, y);
}

int main(int argc, char **argv)
{
	data_t data = {0, };
	struct intel_batchbuffer *batch = NULL;
	struct igt_buf src, dst;
	igt_render_copyfunc_t render_copy = NULL;
	int opt;
	int opt_dump_png = false;
	int opt_dump_aub = igt_aub_dump_enabled();

	igt_simple_init();

	while ((opt = getopt(argc, argv, "d")) != -1) {
		switch (opt) {
		case 'd':
			opt_dump_png = true;
			break;
		default:
			break;
		}
	}

	igt_fixture {
		data.drm_fd = drm_open_any_render();
		data.devid = intel_get_drm_devid(data.drm_fd);

		data.bufmgr = drm_intel_bufmgr_gem_init(data.drm_fd, 4096);
		igt_assert(data.bufmgr);

		render_copy = igt_get_render_copyfunc(data.devid);
		igt_require_f(render_copy,
			      "no render-copy function\n");

		batch = intel_batchbuffer_alloc(data.bufmgr, data.devid);
		igt_assert(batch);
	}

	scratch_buf_init(&data, &src, WIDTH, HEIGHT, STRIDE, SRC_COLOR);
	scratch_buf_init(&data, &dst, WIDTH, HEIGHT, STRIDE, DST_COLOR);

	scratch_buf_check(&data, &src, WIDTH / 2, HEIGHT / 2, SRC_COLOR);
	scratch_buf_check(&data, &dst, WIDTH / 2, HEIGHT / 2, DST_COLOR);

	if (opt_dump_png) {
		scratch_buf_write_to_png(&src, "source.png");
		scratch_buf_write_to_png(&dst, "destination.png");
	}

	if (opt_dump_aub) {
		drm_intel_bufmgr_gem_set_aub_filename(data.bufmgr,
						      "rendercopy.aub");
		drm_intel_bufmgr_gem_set_aub_dump(data.bufmgr, true);
	}

	render_copy(batch, NULL,
		    &src, 0, 0, WIDTH, HEIGHT,
		    &dst, WIDTH / 2, HEIGHT / 2);

	if (opt_dump_png)
		scratch_buf_write_to_png(&dst, "result.png");

	if (opt_dump_aub) {
		drm_intel_gem_bo_aub_dump_bmp(dst.bo,
			0, 0, WIDTH, HEIGHT,
			AUB_DUMP_BMP_FORMAT_ARGB_8888,
			STRIDE, 0);
		drm_intel_bufmgr_gem_set_aub_dump(data.bufmgr, false);
	} else {
		scratch_buf_check(&data, &dst, 10, 10, DST_COLOR);
		scratch_buf_check(&data, &dst, WIDTH - 10, HEIGHT - 10, SRC_COLOR);
	}

	return 0;
}
