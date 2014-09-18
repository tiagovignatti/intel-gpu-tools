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

#include <math.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"
#include "igt_core.h"

typedef struct {
	int gfx_fd;
	igt_display_t display;
	struct igt_fb fb;
	igt_crc_t ref_crc;
	igt_pipe_crc_t *pipe_crc;
} data_t;

static void
paint_squares(data_t *data, struct igt_fb *fb, drmModeModeInfo *mode,
	      igt_rotation_t rotation)
{
	cairo_t *cr;
	int w, h;

	w = mode->hdisplay;
	h = mode->vdisplay;

	cr = igt_get_cairo_ctx(data->gfx_fd, &data->fb);

	if (rotation == IGT_ROTATION_180) {
		cairo_translate(cr, w, h);
		cairo_rotate(cr, M_PI);
	}

	/* Paint with 4 squares of Red, Green, White, Blue Clockwise */
	igt_paint_color(cr, 0, 0, w / 2, h / 2, 1.0, 0.0, 0.0);
	igt_paint_color(cr, w / 2, 0, w / 2, h / 2, 0.0, 1.0, 0.0);
	igt_paint_color(cr, 0, h / 2, w / 2, h / 2, 0.0, 0.0, 1.0);
	igt_paint_color(cr, w / 2, h / 2, w / 2, h / 2, 1.0, 1.0, 1.0);

	cairo_destroy(cr);
}

static bool prepare_crtc(data_t *data, igt_output_t *output, enum pipe pipe,
			 igt_plane_t *plane)
{
	drmModeModeInfo *mode;
	igt_display_t *display = &data->display;
	int fb_id;

	igt_output_set_pipe(output, pipe);

	/* create the pipe_crc object for this pipe */
	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = igt_pipe_crc_new(pipe, INTEL_PIPE_CRC_SOURCE_AUTO);
	if (!data->pipe_crc) {
		igt_info("auto crc not supported on this connector with pipe %i\n",
			 pipe);
		return false;
	}

	mode = igt_output_get_mode(output);

	fb_id = igt_create_fb(data->gfx_fd,
			mode->hdisplay, mode->vdisplay,
			DRM_FORMAT_XRGB8888,
			false, /* tiled */
			&data->fb);
	igt_assert(fb_id);

	/* Step 1: create a reference CRC for a software-rotated fb */

	paint_squares(data, &data->fb, mode, IGT_ROTATION_180);

	/*
	 * XXX: We always set the primary plane to actually enable the pipe as
	 * there's no way (that works) to light up a pipe with only a sprite
	 * plane enabled at the moment.
	 */
	if (!plane->is_primary) {
		igt_plane_t *primary;

		primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
		igt_plane_set_fb(primary, &data->fb);
	}

	igt_plane_set_fb(plane, &data->fb);
	igt_display_commit(display);

	igt_pipe_crc_collect_crc(data->pipe_crc, &data->ref_crc);

	/*
	 * Step 2: prepare the plane with an non-rotated fb let the hw
	 * rotate it.
	 */
	paint_squares(data, &data->fb, mode, IGT_ROTATION_0);

	igt_plane_set_fb(plane, &data->fb);
	return true;
}

static void cleanup_crtc(data_t *data, igt_output_t *output, igt_plane_t *plane)
{
	igt_display_t *display = &data->display;

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	igt_remove_fb(data->gfx_fd, &data->fb);

	/* XXX: see the note in prepare_crtc() */
	if (!plane->is_primary) {
		igt_plane_t *primary;

		primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
		igt_plane_set_fb(primary, NULL);
	}

	igt_plane_set_fb(plane, NULL);
	igt_output_set_pipe(output, PIPE_ANY);

	igt_display_commit(display);
}

static void test_plane_rotation(data_t *data, enum igt_plane plane_type)
{
	igt_display_t *display = &data->display;
	igt_output_t *output;
	enum pipe pipe;
	int valid_tests = 0;
	igt_crc_t crc_output, crc_unrotated;
	enum igt_commit_style commit = COMMIT_LEGACY;

	if (plane_type == IGT_PLANE_PRIMARY) {
		igt_require(data->display.has_universal_planes);
		commit = COMMIT_UNIVERSAL;
	}

	for_each_connected_output(display, output) {
		for_each_pipe(display, pipe) {
			igt_plane_t *plane;

			igt_output_set_pipe(output, pipe);

			plane = igt_output_get_plane(output, plane_type);
			igt_require(igt_plane_supports_rotation(plane));

			if (!prepare_crtc(data, output, pipe, plane))
				continue;
			igt_display_commit2(display, commit);

			/* collect unrotated CRC */
			igt_display_commit2(display, commit);
			igt_pipe_crc_collect_crc(data->pipe_crc, &crc_unrotated);

			igt_plane_set_rotation(plane, IGT_ROTATION_180);
			igt_display_commit2(display, commit);

			igt_pipe_crc_collect_crc(data->pipe_crc, &crc_output);
			igt_assert(igt_crc_equal(&data->ref_crc, &crc_output));

			/* check the rotation state has been reset when the VT
			 * mode is restored */
			kmstest_restore_vt_mode();
			kmstest_set_vt_graphics_mode();
			prepare_crtc(data, output, pipe, plane);
			igt_pipe_crc_collect_crc(data->pipe_crc, &crc_output);
			igt_assert(igt_crc_equal(&crc_unrotated, &crc_output));


			valid_tests++;
			cleanup_crtc(data, output, plane);
		}
	}
	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

igt_main
{
	data_t data = {};

	igt_skip_on_simulation();

	igt_fixture {
		data.gfx_fd = drm_open_any_master();

		kmstest_set_vt_graphics_mode();

		igt_require_pipe_crc();

		igt_display_init(&data.display, data.gfx_fd);
	}

	igt_subtest_f("primary-rotation")
		test_plane_rotation(&data, IGT_PLANE_PRIMARY);

	igt_subtest_f("sprite-rotation")
		test_plane_rotation(&data, IGT_PLANE_2);

	igt_fixture {
		igt_display_fini(&data.display);
	}
}
