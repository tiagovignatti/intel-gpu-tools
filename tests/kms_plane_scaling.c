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


IGT_TEST_DESCRIPTION("Test display plane scaling");

typedef struct {
	uint32_t devid;
	int drm_fd;
	igt_display_t display;
	igt_crc_t ref_crc;
	igt_pipe_crc_t *pipe_crc;

	int image_w;
	int image_h;

	int num_scalers;

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

static void
paint_color(data_t *d, struct igt_fb *fb, uint16_t w, uint16_t h)
{
	cairo_t *cr;

	cr = igt_get_cairo_ctx(d->drm_fd, fb);
	igt_paint_test_pattern(cr, w, h);
	cairo_destroy(cr);
}

static void
paint_image(data_t *d, struct igt_fb *fb, uint16_t w, uint16_t h)
{
	cairo_t *cr;

	cr = igt_get_cairo_ctx(d->drm_fd, fb);
	igt_paint_image(cr, FILE_NAME, 0, 0, w, h);
	cairo_destroy(cr);
}

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
	data->fb_id1 = igt_create_fb(data->drm_fd,
			mode->hdisplay, mode->vdisplay,
			DRM_FORMAT_XRGB8888,
			LOCAL_I915_FORMAT_MOD_X_TILED, /* tiled */
			&data->fb1);
	igt_assert(data->fb_id1);

	paint_color(data, &data->fb1, mode->hdisplay, mode->vdisplay);

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

/* does iterative scaling on plane2 */
static void iterate_plane_scaling(data_t *d, drmModeModeInfo *mode)
{
	igt_display_t *display = &d->display;

	if (mode->hdisplay >= d->fb2.width) {
		int w, h;
		/* fixed fb */
		igt_fb_set_position(&d->fb2, d->plane2, 0, 0);
		igt_fb_set_size(&d->fb2, d->plane2, d->fb2.width, d->fb2.height);
		igt_plane_set_position(d->plane2, 0, 0);

		/* adjust plane size */
		for (w = d->fb2.width; w <= mode->hdisplay; w+=10) {
			h = w * d->fb2.height / d->fb2.width;
			igt_plane_set_size(d->plane2, w, h);
			igt_display_commit2(display, COMMIT_UNIVERSAL);
		}
	} else {
		int w, h;
		/* fixed plane */
		igt_plane_set_position(d->plane2, 0, 0);
		igt_plane_set_size(d->plane2, mode->hdisplay, mode->vdisplay);
		igt_fb_set_position(&d->fb2, d->plane2, 0, 0);

		/* adjust fb size */
		for (w = mode->hdisplay; w <= d->fb2.width; w+=10) {
			h = w * mode->hdisplay / mode->vdisplay;
			igt_fb_set_size(&d->fb2, d->plane2, w, h);
			igt_display_commit2(display, COMMIT_UNIVERSAL);
		}
	}
}

static void test_plane_scaling(data_t *d)
{
	igt_display_t *display = &d->display;
	igt_output_t *output;
	cairo_surface_t *image;
	enum pipe pipe;
	int valid_tests = 0;
	int primary_plane_scaling = 0; /* For now */

	igt_require(d->display.has_universal_planes);
	igt_require(d->num_scalers);

	for_each_connected_output(display, output) {
		drmModeModeInfo *mode;

		pipe = output->config.pipe;
		igt_output_set_pipe(output, pipe);

		mode = igt_output_get_mode(output);

		/* allocate fb2 with image size */
		image = cairo_image_surface_create_from_png(FILE_NAME);
		igt_assert(cairo_surface_status(image) == CAIRO_STATUS_SUCCESS);
		d->image_w = cairo_image_surface_get_width(image);
		d->image_h = cairo_image_surface_get_height(image);
		cairo_surface_destroy(image);

		d->fb_id2 = igt_create_fb(d->drm_fd,
				d->image_w, d->image_h,
				DRM_FORMAT_XRGB8888,
				LOCAL_I915_FORMAT_MOD_X_TILED, /* tiled */
				&d->fb2);
		igt_assert(d->fb_id2);
		paint_image(d, &d->fb2, d->fb2.width, d->fb2.height);

		d->fb_id3 = igt_create_fb(d->drm_fd,
				mode->hdisplay, mode->vdisplay,
				DRM_FORMAT_XRGB8888,
				LOCAL_I915_FORMAT_MOD_X_TILED, /* tiled */
				&d->fb3);
		igt_assert(d->fb_id3);
		paint_color(d, &d->fb3, mode->hdisplay, mode->vdisplay);

		/* Set up display with plane 1 */
		d->plane1 = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
		prepare_crtc(d, output, pipe, d->plane1, mode, COMMIT_UNIVERSAL);

		if (primary_plane_scaling) {
			/* Primary plane upscaling */
			igt_fb_set_position(&d->fb1, d->plane1, 100, 100);
			igt_fb_set_size(&d->fb1, d->plane1, 500, 500);
			igt_plane_set_position(d->plane1, 0, 0);
			igt_plane_set_size(d->plane1, mode->hdisplay, mode->vdisplay);
			igt_display_commit2(display, COMMIT_UNIVERSAL);

			/* Primary plane 1:1 no scaling */
			igt_fb_set_position(&d->fb1, d->plane1, 0, 0);
			igt_fb_set_size(&d->fb1, d->plane1, d->fb1.width, d->fb1.height);
			igt_plane_set_position(d->plane1, 0, 0);
			igt_plane_set_size(d->plane1, mode->hdisplay, mode->vdisplay);
			igt_display_commit2(display, COMMIT_UNIVERSAL);
		}

		/* Set up fb2->plane2 mapping. */
		d->plane2 = igt_output_get_plane(output, IGT_PLANE_2);
		igt_plane_set_fb(d->plane2, &d->fb2);

		/* 2nd plane windowed */
		igt_fb_set_position(&d->fb2, d->plane2, 100, 100);
		igt_fb_set_size(&d->fb2, d->plane2, d->fb2.width-200, d->fb2.height-200);
		igt_plane_set_position(d->plane2, 100, 100);
		igt_plane_set_size(d->plane2, mode->hdisplay-200, mode->vdisplay-200);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		iterate_plane_scaling(d, mode);

		/* 2nd plane up scaling */
		igt_fb_set_position(&d->fb2, d->plane2, 100, 100);
		igt_fb_set_size(&d->fb2, d->plane2, 500, 500);
		igt_plane_set_position(d->plane2, 10, 10);
		igt_plane_set_size(d->plane2, mode->hdisplay-20, mode->vdisplay-20);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		/* 2nd plane downscaling */
		igt_fb_set_position(&d->fb2, d->plane2, 0, 0);
		igt_fb_set_size(&d->fb2, d->plane2, d->fb2.width, d->fb2.height);
		igt_plane_set_position(d->plane2, 10, 10);
		igt_plane_set_size(d->plane2, 500, 500 * d->fb2.height/d->fb2.width);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		if (primary_plane_scaling) {
			/* Primary plane up scaling */
			igt_fb_set_position(&d->fb1, d->plane1, 100, 100);
			igt_fb_set_size(&d->fb1, d->plane1, 500, 500);
			igt_plane_set_position(d->plane1, 0, 0);
			igt_plane_set_size(d->plane1, mode->hdisplay, mode->vdisplay);
			igt_display_commit2(display, COMMIT_UNIVERSAL);
		}

		/* Set up fb3->plane3 mapping. */
		d->plane3 = igt_output_get_plane(output, IGT_PLANE_3);
		igt_plane_set_fb(d->plane3, &d->fb3);

		/* 3rd plane windowed - no scaling */
		igt_fb_set_position(&d->fb3, d->plane3, 100, 100);
		igt_fb_set_size(&d->fb3, d->plane3, d->fb3.width-300, d->fb3.height-300);
		igt_plane_set_position(d->plane3, 100, 100);
		igt_plane_set_size(d->plane3, mode->hdisplay-300, mode->vdisplay-300);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		/* Switch scaler from plane 2 to plane 3 */
		igt_fb_set_position(&d->fb2, d->plane2, 100, 100);
		igt_fb_set_size(&d->fb2, d->plane2, d->fb2.width-200, d->fb2.height-200);
		igt_plane_set_position(d->plane2, 100, 100);
		igt_plane_set_size(d->plane2, d->fb2.width-200, d->fb2.height-200);

		igt_fb_set_position(&d->fb3, d->plane3, 100, 100);
		igt_fb_set_size(&d->fb3, d->plane3, d->fb3.width-400, d->fb3.height-400);
		igt_plane_set_position(d->plane3, 10, 10);
		igt_plane_set_size(d->plane3, mode->hdisplay-300, mode->vdisplay-300);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		if (primary_plane_scaling) {
			/* Switch scaler from plane 1 to plane 2 */
			igt_fb_set_position(&d->fb1, d->plane1, 0, 0);
			igt_fb_set_size(&d->fb1, d->plane1, d->fb1.width, d->fb1.height);
			igt_plane_set_position(d->plane1, 0, 0);
			igt_plane_set_size(d->plane1, mode->hdisplay, mode->vdisplay);

			igt_fb_set_position(&d->fb2, d->plane2, 100, 100);
			igt_fb_set_size(&d->fb2, d->plane2, d->fb2.width-500,d->fb2.height-500);
			igt_plane_set_position(d->plane2, 100, 100);
			igt_plane_set_size(d->plane2, mode->hdisplay-200, mode->vdisplay-200);
			igt_display_commit2(display, COMMIT_UNIVERSAL);
		}

		/* back to single plane mode */
		igt_plane_set_fb(d->plane2, NULL);
		igt_plane_set_fb(d->plane3, NULL);
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

	data.num_scalers = intel_gen(data.devid) >= 9 ? 2 : 0;

	test_plane_scaling(&data);

	igt_display_fini(&data.display);
}
