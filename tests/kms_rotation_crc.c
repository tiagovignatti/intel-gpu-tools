/*
 * Copyright © 2013,2014 Intel Corporation
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
#include <sys/ioctl.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"
#include "igt_core.h"

#define DRM_ROTATE_0   0
#define DRM_ROTATE_90  1
#define DRM_ROTATE_180 2
#define DRM_ROTATE_270 3
#define DRM_REFLECT_X  4
#define DRM_REFLECT_Y  5
#define DRM_ROTATE_NUM	6

#define BIT(x)	(1 << x)

// This will be part of libdrm later. Adding here temporarily
#define DRM_PLANE_TYPE_OVERLAY 0
#define DRM_PLANE_TYPE_PRIMARY 1
#define DRM_PLANE_TYPE_CURSOR  2
#define DRM_CLIENT_CAP_EXPOSE_PRIMARY_PLANES 2

typedef struct {
	int gfx_fd;
	igt_display_t display;
	igt_output_t *output;
	int type;
	int pipe;
	struct igt_fb fb;
	igt_crc_t ref_crc[2];
	igt_pipe_crc_t *pipe_crc;
	int rotate;
} data_t;

static bool prepare_crtc(data_t *data)
{
	drmModeModeInfo *mode;
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;
	cairo_t *cr;
	igt_plane_t *primary, *sprite;
	int fb_id;
	int w, h;

	igt_output_set_pipe(output, data->pipe);
	igt_display_commit(display);

	if (!data->output->valid)
		return false;

	/* create the pipe_crc object for this pipe */
	if (data->pipe_crc)
		igt_pipe_crc_free(data->pipe_crc);

	data->pipe_crc = igt_pipe_crc_new(data->pipe,
					  INTEL_PIPE_CRC_SOURCE_AUTO);
	if (!data->pipe_crc) {
		igt_info("auto crc not supported on this connector with pipe %i\n",
			 data->pipe);
		return false;
	}

	switch (data->type) {

		case DRM_PLANE_TYPE_OVERLAY: /* Sprite */
			igt_info("Sprite plane\n");
			mode = igt_output_get_mode(output);
			w = mode->hdisplay;
			h = mode->vdisplay;

			fb_id = igt_create_fb(data->gfx_fd,
					mode->hdisplay, mode->vdisplay,
					DRM_FORMAT_XRGB8888,
					false, /* tiled */
					&data->fb);
			igt_assert(fb_id);
			cr = igt_get_cairo_ctx(data->gfx_fd, &data->fb);

			/* Paint rotated image of 4 colors */
			igt_paint_color(cr, (w/2)-1, (h/2)-1, w/2, h/2, 1.0, 0.0, 0.0);
			igt_paint_color(cr, 0, (h/2)-1, w/2, h/2, 0.0, 1.0, 0.0);
			igt_paint_color(cr, (w/2)-1, 0, w/2, h/2, 0.0, 0.0, 1.0);
			igt_paint_color(cr, 0, 0, w/2, h/2, 1.0, 1.0, 1.0);
			sprite = igt_output_get_plane(output, IGT_PLANE_2);
			igt_plane_set_fb(sprite, &data->fb);
			igt_display_commit(display);

			/* Collect reference crc */
			igt_pipe_crc_collect_crc(data->pipe_crc, &data->ref_crc[1]);

			/* Paint with 4 squares of Red, Green, White, Blue Clockwise */
			igt_paint_color(cr, 0, 0, w/2, h/2, 1.0, 0.0, 0.0);
			igt_paint_color(cr, (w/2)-1, 0, w/2, h/2, 0.0, 1.0, 0.0);
			igt_paint_color(cr, 0, (h/2)-1, w/2, h/2, 0.0, 0.0, 1.0);
			igt_paint_color(cr, (w/2)-1, (h/2)-1, w/2, h/2, 1.0, 1.0, 1.0);
			cairo_destroy(cr);
			sprite = igt_output_get_plane(output, IGT_PLANE_2);
			igt_plane_set_fb(sprite, &data->fb);
			igt_display_commit(display);

			break;
		case DRM_PLANE_TYPE_PRIMARY: /* Primary */
			igt_info("Primary plane\n");
			mode = igt_output_get_mode(output);
			w = mode->hdisplay;
			h = mode->vdisplay;

			fb_id = igt_create_fb(data->gfx_fd,
					      mode->hdisplay, mode->vdisplay,
					      DRM_FORMAT_XRGB8888,
					      false, /* tiled */
					      &data->fb);
			igt_assert(fb_id);
			cr = igt_get_cairo_ctx(data->gfx_fd, &data->fb);

			/* Paint rotated image of 4 colors */
			igt_paint_color(cr, (w/2)-1, (h/2)-1, w/2, h/2, 1.0, 0.2, 0.2);
			igt_paint_color(cr, 0, (h/2)-1, w/2, h/2, 0.2, 1.0, 0.2);
			igt_paint_color(cr, (w/2)-1, 0, w/2, h/2, 0.2, 0.2, 1.0);
			igt_paint_color(cr, 0, 0, w/2, h/2, 0.8, 0.8, 0.8);
			primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
			igt_plane_set_fb(primary, &data->fb);
			igt_display_commit(display);

			/* Collect reference crc */
			igt_pipe_crc_collect_crc(data->pipe_crc, &data->ref_crc[0]);

			/* Paint with 4 squares of Red, Green, White, Blue Clockwise */
			igt_paint_color(cr, 0, 0, w/2, h/2, 1.0, 0.2, 0.2);
			igt_paint_color(cr, (w/2)-1, 0, w/2, h/2, 0.2, 1.0, 0.2);
			igt_paint_color(cr, 0, (h/2)-1, w/2, h/2, 0.2, 0.2, 1.0);
			igt_paint_color(cr, (w/2)-1, (h/2)-1, w/2, h/2, 0.8, 0.8, 0.8);
			cairo_destroy(cr);
			primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
			igt_plane_set_fb(primary, &data->fb);
			igt_display_commit(display);

			break;


	}

	return true;
}

static bool check_plane_type(int drm_fd, uint32_t plane_id, uint32_t type)
{
	int i = 0;
	drmModeObjectPropertiesPtr props = NULL;

	props = drmModeObjectGetProperties(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE);

	for (i = 0; i < props->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(drm_fd, props->props[i]);

		if (strcmp(prop->name, "type") == 0) {
			if (props->prop_values[i] == type)
				return true;

			igt_info("Didn't find the requested type:%u\n", (unsigned int)props->prop_values[i]);
		}
	}
	return false;
}

static int connector_find_plane(int gfx_fd, uint32_t pipe, uint32_t type)
{
	drmModePlaneRes *plane_resources;
	drmModePlane *ovr;
	uint32_t id = 0;
	int i, ret = 0;

	ret = drmSetClientCap(gfx_fd, DRM_CLIENT_CAP_EXPOSE_PRIMARY_PLANES, 1);
	if (ret < 0) {
		igt_info("Failed to set client cap:%d\n", ret);
		return 0;
	}

	plane_resources = drmModeGetPlaneResources(gfx_fd);
	if (!plane_resources) {
		igt_info("drmModeGetPlaneResources failed: %s\n",
			 strerror(errno));
		return 0;
	}

	for (i = 0; i < plane_resources->count_planes; i++) {
		ovr = drmModeGetPlane(gfx_fd, plane_resources->planes[i]);
		if (!ovr) {
			igt_info("drmModeGetPlane failed: %s\n",
				 strerror(errno));
			continue;
		}

		if (ovr->possible_crtcs & (1 << pipe)) {
			id = ovr->plane_id;
			if (check_plane_type(gfx_fd, id, type)) {
				drmModeFreePlane(ovr);
				return id;
			}
		}
		drmModeFreePlane(ovr);
	}

	return 0;
}

static int set_plane_property(data_t *data, int plane_id, const char *prop_name, int
		val, igt_crc_t *crc_output)
{
	int i = 0, ret = 0;
	int drm_fd = data->gfx_fd;
	uint64_t value;
	drmModeObjectPropertiesPtr props = NULL;

	value = (uint64_t)val;
	props = drmModeObjectGetProperties(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE);

	for (i = 0; i < props->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(drm_fd, props->props[i]);
		igt_info("\nProp->name=%s: plane_id:%d\n ", prop->name,	plane_id);

		if (strcmp(prop->name, prop_name) == 0) {
			ret = drmModeObjectSetProperty(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE,
					(uint32_t)prop->prop_id, value);
			if (ret) {
				igt_info("set_property \"%s\" to %d for plane %d is failed,	err:%d\n", prop_name, val, plane_id, ret);
				drmModeFreeProperty(prop);
				drmModeFreeObjectProperties(props);
				return ret;
			} else {
				/* Collect crc after rotation */
				igt_pipe_crc_collect_crc(data->pipe_crc, crc_output);
				drmModeFreeProperty(prop);
				break;
			}
		}
		drmModeFreeProperty(prop);
	}
	drmModeFreeObjectProperties(props);
	return 0;
}

static void cleanup_crtc(data_t *data, igt_output_t *output)
{
	igt_display_t *display = &data->display;
	igt_plane_t *plane = NULL;

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	igt_remove_fb(data->gfx_fd, &data->fb);

	if (data->type == DRM_PLANE_TYPE_PRIMARY)
		plane = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
	else if (data->type == DRM_PLANE_TYPE_OVERLAY)
		plane = igt_output_get_plane(output, IGT_PLANE_2);

	if (plane != NULL)
		igt_plane_set_fb(plane, NULL);

	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit(display);
}

static void test_sprite_rotation(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output;
	igt_plane_t *sprite;
	igt_crc_t crc_output;
	int p;
	int plane_id;
	int ret;
	int valid_tests = 0;

	for_each_connected_output(display, output) {
		data->output = output;
		for (p = 0; p < igt_display_get_n_pipes(display); p++) {
			data->pipe = p;
			data->type = 0;
			data->rotate = DRM_ROTATE_180;

			if (!prepare_crtc(data))
				continue;
			sleep(2);

			sprite = igt_output_get_plane(output, IGT_PLANE_2);
			plane_id = sprite->drm_plane->plane_id;
			if (plane_id != 0) {
				igt_info("Setting rotation property for plane:%d\n", plane_id);
				ret = set_plane_property(data, plane_id, "rotation", BIT(data->rotate), &crc_output);
				if (ret < 0) {
					igt_info("Setting rotation failed!");
					return;
				}
			}
			igt_assert(igt_crc_equal(&data->ref_crc[1], &crc_output));
			sleep(2);
			valid_tests++;
			cleanup_crtc(data, output);
		}
	}
	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}


static void test_primary_rotation(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output;
	int p;
	int plane_id;
	int ret;
	int valid_tests = 0;
	igt_crc_t crc_output;

	igt_require(data->display.has_universal_planes);

	for_each_connected_output(display, output) {
		data->output = output;
		for (p = 0; p < igt_display_get_n_pipes(display); p++) {
			data->pipe = p;
			data->type = 1;
			data->rotate = DRM_ROTATE_180;

			if (!prepare_crtc(data))
				continue;
			sleep(2);

			/* Find primary plane. Currently igt_plane_t returned from
			 * igt_output_get_plane has NULL drm_plane which is needed to get
			 * the plane_id. So finding the primary plane by checking the "type"
			 * property of the plane */
			plane_id = connector_find_plane(data->gfx_fd, data->pipe, data->type);
			if (plane_id != 0) {
				igt_info("Setting rotation property for plane:%d\n", plane_id);
				ret = set_plane_property(data, plane_id, "rotation", BIT(data->rotate), &crc_output);
				if (ret < 0) {
					igt_info("Setting rotation failed!");
					return;
				}
			}
			igt_assert(igt_crc_equal(&data->ref_crc[0], &crc_output));
			sleep(2);
			valid_tests++;
			cleanup_crtc(data, output);
		}
	}
	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

igt_main
{
	data_t data = {};

	igt_skip_on_simulation();

	igt_fixture {
		data.gfx_fd = drm_open_any();

		igt_set_vt_graphics_mode();

		igt_require_pipe_crc();

		igt_display_init(&data.display, data.gfx_fd);
	}

	igt_subtest_f("primary-rotation")
		test_primary_rotation(&data);

	igt_subtest_f("sprite-rotation")
		test_sprite_rotation(&data);

	igt_fixture {
		igt_display_fini(&data.display);
	}
}
