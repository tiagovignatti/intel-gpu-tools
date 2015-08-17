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


IGT_TEST_DESCRIPTION("Test crtc background color feature");

typedef struct {
	int gfx_fd;
	igt_display_t display;
	struct igt_fb fb;
	igt_crc_t ref_crc;
	igt_pipe_crc_t *pipe_crc;
} data_t;

#define BLACK      0x000000           /* BGR 8bpc */
#define CYAN       0xFFFF00           /* BGR 8bpc */
#define PURPLE     0xFF00FF           /* BGR 8bpc */
#define WHITE      0xFFFFFF           /* BGR 8bpc */

#define BLACK64    0x000000000000     /* BGR 16bpc */
#define CYAN64     0xFFFFFFFF0000     /* BGR 16bpc */
#define PURPLE64   0xFFFF0000FFFF     /* BGR 16bpc */
#define YELLOW64   0x0000FFFFFFFF     /* BGR 16bpc */
#define WHITE64    0xFFFFFFFFFFFF     /* BGR 16bpc */
#define RED64      0x00000000FFFF     /* BGR 16bpc */
#define GREEN64    0x0000FFFF0000     /* BGR 16bpc */
#define BLUE64     0xFFFF00000000     /* BGR 16bpc */

static void
paint_background(data_t *data, struct igt_fb *fb, drmModeModeInfo *mode,
		uint32_t background, double alpha)
{
	cairo_t *cr;
	int w, h;
	double r, g, b;

	w = mode->hdisplay;
	h = mode->vdisplay;

	cr = igt_get_cairo_ctx(data->gfx_fd, &data->fb);

	/* Paint with background color */
	r = (double) (background & 0xFF) / 255.0;
	g = (double) ((background & 0xFF00) >> 8) / 255.0;
	b = (double) ((background & 0xFF0000) >> 16) / 255.0;
	igt_paint_color_alpha(cr, 0, 0, w, h, r, g, b, alpha);

	cairo_destroy(cr);
}

static void prepare_crtc(data_t *data, igt_output_t *output, enum pipe pipe,
			igt_plane_t *plane, int opaque_buffer, int plane_color,
			uint64_t pipe_background_color)
{
	drmModeModeInfo *mode;
	igt_display_t *display = &data->display;
	int fb_id;
	double alpha;

	igt_output_set_pipe(output, pipe);

	/* create the pipe_crc object for this pipe */
	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = igt_pipe_crc_new(pipe, INTEL_PIPE_CRC_SOURCE_AUTO);

	mode = igt_output_get_mode(output);

	fb_id = igt_create_fb(data->gfx_fd,
			mode->hdisplay, mode->vdisplay,
			DRM_FORMAT_XRGB8888,
			LOCAL_DRM_FORMAT_MOD_NONE, /* tiled */
			&data->fb);
	igt_assert(fb_id);

	/* To make FB pixel win with background color, set alpha as full opaque */
	igt_crtc_set_background(plane->pipe, pipe_background_color);
	if (opaque_buffer)
		alpha = 1.0;    /* alpha 1 is fully opque */
	else
		alpha = 0.0;    /* alpha 0 is fully transparent */
	paint_background(data, &data->fb, mode, plane_color, alpha);

	igt_plane_set_fb(plane, &data->fb);
	igt_display_commit2(display, COMMIT_UNIVERSAL);
}

static void cleanup_crtc(data_t *data, igt_output_t *output, igt_plane_t *plane)
{
	igt_display_t *display = &data->display;

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	igt_remove_fb(data->gfx_fd, &data->fb);

	igt_crtc_set_background(plane->pipe, BLACK64);
	igt_plane_set_fb(plane, NULL);
	igt_output_set_pipe(output, PIPE_ANY);

	igt_display_commit2(display, COMMIT_UNIVERSAL);
}

static void test_crtc_background(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output;
	enum pipe pipe;
	int valid_tests = 0;

	igt_require(data->display.has_universal_planes);

	for_each_connected_output(display, output) {
		igt_plane_t *plane;

		pipe = output->config.pipe;
		igt_output_set_pipe(output, pipe);

		plane = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
		igt_require(plane->pipe->background_property);

		prepare_crtc(data, output, pipe, plane, 1, PURPLE, BLACK64);

		/* Now set background without using a plane, i.e.,
		 * Disable the plane to let hw background color win blend. */
		igt_plane_set_fb(plane, NULL);
		igt_crtc_set_background(plane->pipe, PURPLE64);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		/* Try few other background colors */
		igt_crtc_set_background(plane->pipe, CYAN64);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		igt_crtc_set_background(plane->pipe, YELLOW64);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		igt_crtc_set_background(plane->pipe, RED64);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		igt_crtc_set_background(plane->pipe, GREEN64);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		igt_crtc_set_background(plane->pipe, BLUE64);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		igt_crtc_set_background(plane->pipe, WHITE64);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		valid_tests++;
		cleanup_crtc(data, output, plane);
	}
	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

igt_simple_main
{
	data_t data = {};

	igt_skip_on_simulation();

	data.gfx_fd = drm_open_any();
	igt_require_pipe_crc();
	igt_display_init(&data.display, data.gfx_fd);

	test_crtc_background(&data);

	igt_display_fini(&data.display);
}
