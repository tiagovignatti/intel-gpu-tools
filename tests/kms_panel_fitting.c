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

#include "igt.h"
#include <math.h>


IGT_TEST_DESCRIPTION("Test display panel fitting");

typedef struct {
	uint32_t devid;
	int drm_fd;
	igt_display_t display;
	igt_crc_t ref_crc;
	igt_pipe_crc_t *pipe_crc;

	int image_w;
	int image_h;

	struct igt_fb fb1;
	struct igt_fb fb2;
	struct igt_fb fb3;
	int fb_id1;
	int fb_id2;
	int fb_id3;

	igt_plane_t *plane1;
	igt_plane_t *plane2;
	igt_plane_t *plane3;
	igt_plane_t *plane4;
} data_t;

#define FILE_NAME   "1080p-left.png"

static void prepare_crtc(data_t *data, igt_output_t *output, enum pipe pipe,
			igt_plane_t *plane, drmModeModeInfo *mode, enum igt_commit_style s)
{
	igt_display_t *display = &data->display;

	igt_output_set_pipe(output, pipe);

	/* create the pipe_crc object for this pipe */
	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = igt_pipe_crc_new(pipe, INTEL_PIPE_CRC_SOURCE_AUTO);

	/* before allocating, free if any older fb */
	if (data->fb_id1) {
		igt_remove_fb(data->drm_fd, &data->fb1);
		data->fb_id1 = 0;
	}

	/* allocate fb for plane 1 */
	data->fb_id1 = igt_create_pattern_fb(data->drm_fd,
					     mode->hdisplay, mode->vdisplay,
					     DRM_FORMAT_XRGB8888,
					     LOCAL_I915_FORMAT_MOD_X_TILED, /* tiled */
					     &data->fb1);
	igt_assert(data->fb_id1);

	/*
	 * We always set the primary plane to actually enable the pipe as
	 * there's no way (that works) to light up a pipe with only a sprite
	 * plane enabled at the moment.
	 */
	if (!plane->is_primary) {
		igt_plane_t *primary;

		primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
		igt_plane_set_fb(primary, &data->fb1);
	}

	igt_plane_set_fb(plane, &data->fb1);
	if (s == COMMIT_LEGACY) {
		int ret;
		ret = drmModeSetCrtc(data->drm_fd,
				output->config.crtc->crtc_id,
				data->fb_id1,
				plane->pan_x, plane->pan_y,
				&output->id,
				1,
				mode);
		igt_assert_eq(ret, 0);
	} else {
		igt_display_commit2(display, s);
	}
}

static void cleanup_crtc(data_t *data, igt_output_t *output, igt_plane_t *plane)
{
	igt_display_t *display = &data->display;

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	if (data->fb_id1) {
		igt_remove_fb(data->drm_fd, &data->fb1);
		data->fb_id1 = 0;
	}
	if (data->fb_id2) {
		igt_remove_fb(data->drm_fd, &data->fb2);
		data->fb_id2 = 0;
	}
	if (data->fb_id3) {
		igt_remove_fb(data->drm_fd, &data->fb3);
		data->fb_id3 = 0;
	}

	if (!plane->is_primary) {
		igt_plane_t *primary;

		primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
		igt_plane_set_fb(primary, NULL);
	}

	igt_plane_set_fb(plane, NULL);
	igt_output_set_pipe(output, PIPE_ANY);

	igt_display_commit2(display, COMMIT_UNIVERSAL);
}

static void test_panel_fitting(data_t *d)
{
	igt_display_t *display = &d->display;
	igt_output_t *output;
	cairo_surface_t *image;
	enum pipe pipe;
	int valid_tests = 0;

	for_each_connected_output(display, output) {
		drmModeModeInfo *mode, native_mode;

		igt_require(output->config.connector->connector_type ==
			DRM_MODE_CONNECTOR_eDP);

		pipe = output->config.pipe;
		igt_output_set_pipe(output, pipe);

		mode = igt_output_get_mode(output);
		native_mode = *mode;

		/* allocate fb2 with image size */
		image = cairo_image_surface_create_from_png(FILE_NAME);
		igt_assert(cairo_surface_status(image) == CAIRO_STATUS_SUCCESS);
		d->image_w = cairo_image_surface_get_width(image);
		d->image_h = cairo_image_surface_get_height(image);
		cairo_surface_destroy(image);

		d->fb_id2 = igt_create_image_fb(d->drm_fd, 0, 0,
						DRM_FORMAT_XRGB8888,
						LOCAL_I915_FORMAT_MOD_X_TILED, /* tiled */
						FILE_NAME, &d->fb2);
		igt_assert(d->fb_id2);

		/* Set up display to enable panel fitting */
		mode->hdisplay = 640;
		mode->vdisplay = 480;
		d->plane1 = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
		prepare_crtc(d, output, pipe, d->plane1, mode, COMMIT_LEGACY);

		/* disable panel fitting */
		prepare_crtc(d, output, pipe, d->plane1, &native_mode, COMMIT_LEGACY);

		/* enable panel fitting */
		mode->hdisplay = 800;
		mode->vdisplay = 600;
		prepare_crtc(d, output, pipe, d->plane1, mode, COMMIT_LEGACY);

		/* disable panel fitting */
		prepare_crtc(d, output, pipe, d->plane1, &native_mode, COMMIT_LEGACY);

		/* Set up fb2->plane2 mapping. */
		d->plane2 = igt_output_get_plane(output, IGT_PLANE_2);
		igt_plane_set_fb(d->plane2, &d->fb2);

		/* enable sprite plane */
		igt_fb_set_position(&d->fb2, d->plane2, 100, 100);
		igt_fb_set_size(&d->fb2, d->plane2, d->fb2.width-200, d->fb2.height-200);
		igt_plane_set_position(d->plane2, 100, 100);
		igt_plane_set_size(d->plane2, mode->hdisplay-200, mode->vdisplay-200);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		/* enable panel fitting along with sprite scaling */
		mode->hdisplay = 1024;
		mode->vdisplay = 768;
		prepare_crtc(d, output, pipe, d->plane1, mode, COMMIT_LEGACY);

		/* back to single plane mode */
		igt_plane_set_fb(d->plane2, NULL);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		valid_tests++;
		cleanup_crtc(d, output, d->plane1);
	}
	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

igt_simple_main
{
	data_t data = {};

	igt_skip_on_simulation();

	data.drm_fd = drm_open_driver(DRIVER_INTEL);
	igt_require_pipe_crc();
	igt_display_init(&data.display, data.drm_fd);
	data.devid = intel_get_drm_devid(data.drm_fd);

	test_panel_fitting(&data);

	igt_display_fini(&data.display);
}
