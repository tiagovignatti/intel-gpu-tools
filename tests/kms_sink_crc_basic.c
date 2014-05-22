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

enum color {
	WHITE,
	BLACK,
	NUM_COLORS,
};

typedef struct {
	struct kmstest_connector_config config;
	struct igt_fb fb;
} connector_t;

typedef struct {
	int drm_fd;
	drmModeRes *resources;
} data_t;

static void get_crc(char *crc) {
	int ret;
	FILE *file = fopen("/sys/kernel/debug/dri/0/i915_sink_crc_eDP1", "r");
	igt_require(file);

	ret = fscanf(file, "%s\n", crc);
	igt_require(ret > 0);

	fclose(file);
}

static uint32_t create_fb(data_t *data,
			  int w, int h,
			  double r, double g, double b,
			  struct igt_fb *fb)
{
	cairo_t *cr;
	uint32_t fb_id;

	fb_id = igt_create_fb(data->drm_fd, w, h,
			      DRM_FORMAT_XRGB8888, false, fb);
	igt_assert(fb_id);

	cr = igt_get_cairo_ctx(data->drm_fd, fb);
	igt_paint_color(cr, 0, 0, w, h, r, g, b);
	igt_assert(cairo_status(cr) == 0);

	return fb_id;
}

static bool
connector_set_mode(data_t *data, connector_t *connector, drmModeModeInfo *mode,
		   enum color crtc_color)
{
	struct kmstest_connector_config *config = &connector->config;
	unsigned int fb_id;
	int ret;

	if (crtc_color == WHITE)
		fb_id = create_fb(data, mode->hdisplay, mode->vdisplay,
				  1.0, 1.0, 1.0, &connector->fb);
	else
		fb_id = create_fb(data, mode->hdisplay, mode->vdisplay,
				  0.0, 0.0, 0.0, &connector->fb);
	igt_assert(fb_id);

	ret = drmModeSetCrtc(data->drm_fd,
			     config->crtc->crtc_id,
			     connector->fb.fb_id,
			     0, 0, /* x, y */
			     &config->connector->connector_id,
			     1,
			     mode);
	igt_assert(ret == 0);

	return 0;
}

static void basic_sink_crc_check(data_t *data, uint32_t connector_id)
{
	connector_t connector;
	int ret;
	char ref_crc_white[12];
	char ref_crc_black[12];
	char crc_check[12];

	ret = kmstest_get_connector_config(data->drm_fd,
					   connector_id,
					   1 << 0,
					   &connector.config);
	igt_require(ret == 0);

	/*Go White*/
	connector_set_mode(data, &connector, &connector.config.default_mode, WHITE);

	/* get reference crc for white color */
	get_crc(ref_crc_white);

	/* Go Black */
	connector_set_mode(data, &connector, &connector.config.default_mode, BLACK);

	/* get reference crc for black color */
	get_crc(ref_crc_black);

	igt_assert(strcmp(ref_crc_black, ref_crc_white) != 0);

	/*Go White again*/
	connector_set_mode(data, &connector, &connector.config.default_mode, WHITE);

	get_crc(crc_check);
	igt_assert(strcmp(crc_check, ref_crc_white) == 0);

	/* Go Black again */
	connector_set_mode(data, &connector, &connector.config.default_mode, BLACK);

	get_crc(crc_check);
	igt_assert(strcmp(crc_check, ref_crc_black) == 0);

	kmstest_free_connector_config(&connector.config);
}

static void run_test(data_t *data)
{
	int i;
	drmModeConnectorPtr c;
	uint32_t connector_id = 0;

	for (i = 0; i < data->resources->count_connectors; i++) {
		connector_id = data->resources->connectors[i];
		c = drmModeGetConnector(data->drm_fd, connector_id);

		if (c->connector_type != DRM_MODE_CONNECTOR_eDP ||
		    c->connection != DRM_MODE_CONNECTED)
			continue;

		basic_sink_crc_check(data, connector_id);
		return;
	}

	igt_skip("no eDP with CRC support found\n");
}

igt_simple_main
{
	data_t data = {};

	igt_skip_on_simulation();

	data.drm_fd = drm_open_any();

	igt_set_vt_graphics_mode();

	data.resources = drmModeGetResources(data.drm_fd);
	igt_assert(data.resources);

	run_test(&data);

	drmModeFreeResources(data.resources);
}
