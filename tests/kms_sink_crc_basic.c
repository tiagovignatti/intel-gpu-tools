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
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "drm_fourcc.h"

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"

#define CRC_BLACK "000000000000"

enum color {
	RED,
	GREEN,
};

typedef struct {
	int drm_fd;
	igt_display_t display;
	struct igt_fb fb_green, fb_red;
	igt_plane_t *primary;
} data_t;

static void get_crc(data_t *data, char *crc) {
	int ret;
	FILE *file;

	igt_wait_for_vblank(data->drm_fd, 0);
	igt_wait_for_vblank(data->drm_fd, 0);

	file = igt_debugfs_fopen("i915_sink_crc_eDP1", "r");

	igt_require(file);

	ret = fscanf(file, "%s\n", crc);
	igt_require(ret > 0);

	fclose(file);

	/* Black screen is always invalid */
	igt_assert(strcmp(crc, CRC_BLACK) != 0);
}

static void assert_color(char *crc, enum color color)
{
	char color_mask[5] = "FFFF\0";
	char rs[5], gs[5], bs[5];
	unsigned int rh, gh, bh, mask;
	int ret;

	sscanf(color_mask, "%4x", &mask);

	memcpy(rs, &crc[0], 4);
	rs[4] = '\0';
	ret = sscanf(rs, "%4x", &rh);
	igt_require(ret > 0);

	memcpy(gs, &crc[4], 4);
	gs[4] = '\0';
	ret = sscanf(gs, "%4x", &gh);
	igt_require(ret > 0);

	memcpy(bs, &crc[8], 4);
	bs[4] = '\0';
	ret = sscanf(bs, "%4x", &bh);
	igt_require(ret > 0);

	switch (color) {
	case RED:
		igt_assert((rh & mask) != 0 &&
			   (gh & mask) == 0 &&
			   (bh & mask) == 0);
		break;
	case GREEN:
		igt_assert((rh & mask) == 0 &&
			   (gh & mask) != 0 &&
			   (bh & mask) == 0);
		break;
	default:
		igt_fail(-1);
	}
}

static void basic_sink_crc_check(data_t *data)
{
	char crc[13];

	/* Go Green */
	igt_plane_set_fb(data->primary, &data->fb_green);
	igt_display_commit(&data->display);

	/* It should be Green */
	get_crc(data, crc);
	assert_color(crc, GREEN);

	/* Go Red */
	igt_plane_set_fb(data->primary, &data->fb_red);
	igt_display_commit(&data->display);

	/* It should be Red */
	get_crc(data, crc);
	assert_color(crc, RED);
}

static void run_test(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output;
	drmModeModeInfo *mode;

	for_each_connected_output(display, output) {
		drmModeConnectorPtr c = output->config.connector;

		if (c->connector_type != DRM_MODE_CONNECTOR_eDP ||
		    c->connection != DRM_MODE_CONNECTED)
			continue;

		igt_output_set_pipe(output, 0);

		mode = igt_output_get_mode(output);

		igt_create_color_fb(data->drm_fd,
				    mode->hdisplay, mode->vdisplay,
				    DRM_FORMAT_XRGB8888, I915_TILING_X,
				    0.0, 1.0, 0.0,
				    &data->fb_green);

		igt_create_color_fb(data->drm_fd,
				    mode->hdisplay, mode->vdisplay,
				    DRM_FORMAT_XRGB8888, I915_TILING_X,
				    1.0, 0.0, 0.0,
				    &data->fb_red);

		data->primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);

		basic_sink_crc_check(data);
		return;
	}

	igt_skip("no eDP with CRC support found\n");
}

igt_simple_main
{
	data_t data = {};

	igt_skip_on_simulation();

	data.drm_fd = drm_open_any_master();

	kmstest_set_vt_graphics_mode();
	igt_display_init(&data.display, data.drm_fd);

	run_test(&data);

	igt_display_fini(&data.display);
}
