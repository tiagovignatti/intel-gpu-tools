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


typedef struct {
	int gfx_fd;
	igt_display_t display;
	struct igt_fb fb;
	struct igt_fb fb_modeset;
	igt_crc_t ref_crc;
	igt_pipe_crc_t *pipe_crc;
	igt_rotation_t rotation;
	int pos_x;
	int pos_y;
	unsigned int w, h;
	uint32_t override_fmt;
	uint64_t override_tiling;
} data_t;

static void
paint_squares(data_t *data, drmModeModeInfo *mode, igt_rotation_t rotation,
	      igt_plane_t *plane)
{
	cairo_t *cr;
	unsigned int w = data->w;
	unsigned int h = data->h;

	cr = igt_get_cairo_ctx(data->gfx_fd, &data->fb);

	if (rotation == IGT_ROTATION_180) {
		cairo_translate(cr, w, h);
		cairo_rotate(cr, M_PI);
	}

	if (rotation == IGT_ROTATION_90) {
		/* Paint 4 squares with width == height in Green, White,
		Blue, Red Clockwise order to look like 270 degree rotated*/
		igt_paint_color(cr, 0, 0, w / 2, h / 2, 0.0, 1.0, 0.0);
		igt_paint_color(cr, w / 2, 0, w / 2, h / 2, 1.0, 1.0, 1.0);
		igt_paint_color(cr, 0, h / 2, w / 2, h / 2, 1.0, 0.0, 0.0);
		igt_paint_color(cr, w / 2, h / 2, w / 2, h / 2, 0.0, 0.0, 1.0);
	} else if (rotation == IGT_ROTATION_270) {
		/* Paint 4 squares with width == height in Blue, Red,
		Green, White Clockwise order to look like 90 degree rotated*/
		igt_paint_color(cr, 0, 0, w / 2, h / 2, 0.0, 0.0, 1.0);
		igt_paint_color(cr, w / 2, 0, w / 2, h / 2, 1.0, 0.0, 0.0);
		igt_paint_color(cr, 0, h / 2, w / 2, h / 2, 1.0, 1.0, 1.0);
		igt_paint_color(cr, w / 2, h / 2, w / 2, h / 2, 0.0, 1.0, 0.0);
	} else {
		/* Paint with 4 squares of Red, Green, White, Blue Clockwise */
		igt_paint_color(cr, 0, 0, w / 2, h / 2, 1.0, 0.0, 0.0);
		igt_paint_color(cr, w / 2, 0, w / 2, h / 2, 0.0, 1.0, 0.0);
		igt_paint_color(cr, 0, h / 2, w / 2, h / 2, 0.0, 0.0, 1.0);
		igt_paint_color(cr, w / 2, h / 2, w / 2, h / 2, 1.0, 1.0, 1.0);
	}

	cairo_destroy(cr);
}

static void commit_crtc(data_t *data, igt_output_t *output, igt_plane_t *plane)
{
	igt_display_t *display = &data->display;
	enum igt_commit_style commit = COMMIT_LEGACY;
	igt_plane_t *primary;

	/*
	 * With igt_display_commit2 and COMMIT_UNIVERSAL, we call just the
	 * setplane without a modeset. So, to be able to call
	 * igt_display_commit and ultimately setcrtc to do the first modeset,
	 * we create an fb covering the crtc and call commit
	 */

	primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
	igt_plane_set_fb(primary, &data->fb_modeset);
	igt_display_commit(display);

	igt_plane_set_fb(plane, &data->fb);

	if (!plane->is_cursor)
		igt_plane_set_position(plane, data->pos_x, data->pos_y);

	if (plane->is_primary || plane->is_cursor) {
		igt_require(data->display.has_universal_planes);
		commit = COMMIT_UNIVERSAL;
	}

	igt_display_commit2(display, commit);
}

static void prepare_crtc(data_t *data, igt_output_t *output, enum pipe pipe,
			 igt_plane_t *plane)
{
	drmModeModeInfo *mode;
	int fb_id, fb_modeset_id;
	unsigned int w, h;
	uint64_t tiling = data->override_tiling ?
			  data->override_tiling : LOCAL_DRM_FORMAT_MOD_NONE;
	uint32_t pixel_format = data->override_fmt ?
				data->override_fmt : DRM_FORMAT_XRGB8888;

	igt_output_set_pipe(output, pipe);

	/* create the pipe_crc object for this pipe */
	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = igt_pipe_crc_new(pipe, INTEL_PIPE_CRC_SOURCE_AUTO);

	mode = igt_output_get_mode(output);

	w = mode->hdisplay;
	h = mode->vdisplay;

	fb_modeset_id = igt_create_fb(data->gfx_fd,
				      w, h,
				      pixel_format,
				      tiling,
				      &data->fb_modeset);
	igt_assert(fb_modeset_id);

	/*
	 * For 90/270, we will use create smaller fb so that the rotated
	 * frame can fit in
	 */
	if (data->rotation == IGT_ROTATION_90 ||
	    data->rotation == IGT_ROTATION_270) {
		tiling = data->override_tiling ?
			 data->override_tiling : LOCAL_I915_FORMAT_MOD_Y_TILED;
		w = h =  mode->vdisplay;
	} else if (plane->is_cursor) {
		pixel_format = data->override_fmt ?
			       data->override_fmt : DRM_FORMAT_ARGB8888;
		w = h = 128;
	}

	data->w = w;
	data->h = h;

	fb_id = igt_create_fb(data->gfx_fd,
			      w, h,
			      pixel_format,
			      tiling,
			      &data->fb);
	igt_assert(fb_id);

	/* Step 1: create a reference CRC for a software-rotated fb */
	paint_squares(data, mode, data->rotation, plane);
	commit_crtc(data, output, plane);
	igt_pipe_crc_collect_crc(data->pipe_crc, &data->ref_crc);

	/*
	 * Step 2: prepare the plane with an non-rotated fb let the hw
	 * rotate it.
	 */
	paint_squares(data, mode, IGT_ROTATION_0, plane);
	igt_plane_set_fb(plane, &data->fb);
}

static void cleanup_crtc(data_t *data, igt_output_t *output, igt_plane_t *plane)
{
	igt_display_t *display = &data->display;

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	igt_remove_fb(data->gfx_fd, &data->fb);
	igt_remove_fb(data->gfx_fd, &data->fb_modeset);

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
	int ret;

	if (plane_type == IGT_PLANE_PRIMARY || plane_type == IGT_PLANE_CURSOR) {
		igt_require(data->display.has_universal_planes);
		commit = COMMIT_UNIVERSAL;
	}

	for_each_connected_output(display, output) {
		for_each_pipe(display, pipe) {
			igt_plane_t *plane;

			igt_output_set_pipe(output, pipe);

			plane = igt_output_get_plane(output, plane_type);
			igt_require(igt_plane_supports_rotation(plane));

			prepare_crtc(data, output, pipe, plane);

			igt_display_commit2(display, commit);

			/* collect unrotated CRC */
			igt_pipe_crc_collect_crc(data->pipe_crc, &crc_unrotated);

			igt_plane_set_rotation(plane, data->rotation);
			ret = igt_display_try_commit2(display, commit);
			if (data->override_fmt || data->override_tiling) {
				igt_assert(ret == -EINVAL);
			} else {
				igt_assert(ret == 0);
				igt_pipe_crc_collect_crc(data->pipe_crc,
							 &crc_output);
				igt_assert_crc_equal(&data->ref_crc,
						     &crc_output);
			}

			/*
			 * check the rotation state has been reset when the VT
			 * mode is restored
			 */
			kmstest_restore_vt_mode();
			kmstest_set_vt_graphics_mode();

			commit_crtc(data, output, plane);

			igt_pipe_crc_collect_crc(data->pipe_crc, &crc_output);
			igt_assert_crc_equal(&crc_unrotated, &crc_output);

			valid_tests++;
			cleanup_crtc(data, output, plane);
		}
	}
	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

igt_main
{
	data_t data = {};
	int gen = 0;

	igt_skip_on_simulation();

	igt_fixture {
		data.gfx_fd = drm_open_any_master();
		gen = intel_gen(intel_get_drm_devid(data.gfx_fd));

		kmstest_set_vt_graphics_mode();

		igt_require_pipe_crc();

		igt_display_init(&data.display, data.gfx_fd);
	}
	igt_subtest_f("primary-rotation-180") {
		data.rotation = IGT_ROTATION_180;
		test_plane_rotation(&data, IGT_PLANE_PRIMARY);
	}

	igt_subtest_f("sprite-rotation-180") {
		data.rotation = IGT_ROTATION_180;
		test_plane_rotation(&data, IGT_PLANE_2);
	}

	igt_subtest_f("cursor-rotation-180") {
		data.rotation = IGT_ROTATION_180;
		test_plane_rotation(&data, IGT_PLANE_CURSOR);
	}

	igt_subtest_f("primary-rotation-90") {
		igt_require(gen >= 9);
		data.rotation = IGT_ROTATION_90;
		test_plane_rotation(&data, IGT_PLANE_PRIMARY);
	}

	igt_subtest_f("primary-rotation-270") {
		igt_require(gen >= 9);
		data.rotation = IGT_ROTATION_270;
		test_plane_rotation(&data, IGT_PLANE_PRIMARY);
	}

	igt_subtest_f("sprite-rotation-90") {
		igt_require(gen >= 9);
		data.rotation = IGT_ROTATION_90;
		test_plane_rotation(&data, IGT_PLANE_2);
	}

	igt_subtest_f("sprite-rotation-270") {
		igt_require(gen >= 9);
		data.rotation = IGT_ROTATION_270;
		test_plane_rotation(&data, IGT_PLANE_2);
	}

	igt_subtest_f("sprite-rotation-90-pos-100-0") {
		igt_require(gen >= 9);
		data.rotation = IGT_ROTATION_90;
		data.pos_x = 100,
		data.pos_y = 0;
		test_plane_rotation(&data, IGT_PLANE_2);
	}

	igt_subtest_f("bad-pixel-format") {
		igt_require(gen >= 9);
		data.pos_x = 0,
		data.pos_y = 0;
		data.rotation = IGT_ROTATION_90;
		data.override_fmt = DRM_FORMAT_RGB565;
		test_plane_rotation(&data, IGT_PLANE_PRIMARY);
	}

	igt_subtest_f("bad-tiling") {
		igt_require(gen >= 9);
		data.override_fmt = 0;
		data.rotation = IGT_ROTATION_90;
		data.override_tiling = LOCAL_DRM_FORMAT_MOD_NONE;
		test_plane_rotation(&data, IGT_PLANE_PRIMARY);
	}

	igt_fixture {
		igt_display_fini(&data.display);
	}
}
