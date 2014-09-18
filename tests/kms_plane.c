/*
 * Copyright © 2014 Intel Corporation
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
 *   Damien Lespiau <damien.lespiau@intel.com>
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"

typedef struct {
	float red;
	float green;
	float blue;
} color_t;

typedef struct {
	int drm_fd;
	igt_display_t display;
	igt_pipe_crc_t *pipe_crc;
} data_t;

static color_t red   = { 1.0f, 0.0f, 0.0f };
static color_t green = { 0.0f, 1.0f, 0.0f };
static color_t blue  = { 0.0f, 0.0f, 1.0f };

/*
 * Common code across all tests, acting on data_t
 */
static void test_init(data_t *data, enum pipe pipe)
{
	data->pipe_crc = igt_pipe_crc_new(pipe, INTEL_PIPE_CRC_SOURCE_AUTO);
}

static void test_fini(data_t *data)
{
	igt_pipe_crc_free(data->pipe_crc);
}

static void
test_grab_crc(data_t *data, igt_output_t *output, enum pipe pipe,
	      color_t *fb_color, igt_crc_t *crc /* out */)
{
	struct igt_fb fb;
	drmModeModeInfo *mode;
	igt_plane_t *primary;
	char *crc_str;

	igt_output_set_pipe(output, pipe);

	primary = igt_output_get_plane(output, 0);

	mode = igt_output_get_mode(output);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888,
			    I915_TILING_NONE,
			    fb_color->red, fb_color->green, fb_color->blue,
			    &fb);
	igt_plane_set_fb(primary, &fb);

	igt_display_commit(&data->display);

	igt_pipe_crc_collect_crc(data->pipe_crc, crc);

	igt_plane_set_fb(primary, NULL);
	igt_display_commit(&data->display);

	igt_remove_fb(data->drm_fd, &fb);

	crc_str = igt_crc_to_string(crc);
	igt_debug("CRC for a (%.02f,%.02f,%.02f) fb: %s\n", fb_color->red,
		  fb_color->green, fb_color->blue, crc_str);
	free(crc_str);
}

/*
 * Plane position test.
 *   - We start by grabbing a reference CRC of a full green fb being scanned
 *     out on the primary plane
 *   - Then we scannout 2 planes:
 *      - the primary plane uses a green fb with a black rectangle
 *      - a plane, on top of the primary plane, with a green fb that is set-up
 *        to cover the black rectangle of the primary plane fb
 *     The resulting CRC should be identical to the reference CRC
 */

typedef struct {
	data_t *data;
	igt_crc_t reference_crc;
} test_position_t;

/*
 * create a green fb with a black rectangle at (rect_x,rect_y) and of size
 * (rect_w,rect_h)
 */
static void
create_fb_for_mode__position(data_t *data, drmModeModeInfo *mode,
			     double rect_x, double rect_y,
			     double rect_w, double rect_h,
			     struct igt_fb *fb /* out */)
{
	unsigned int fb_id;
	cairo_t *cr;

	fb_id = igt_create_fb(data->drm_fd,
				  mode->hdisplay, mode->vdisplay,
				  DRM_FORMAT_XRGB8888,
				  I915_TILING_NONE,
				  fb);
	igt_assert(fb_id);

	cr = igt_get_cairo_ctx(data->drm_fd, fb);
	igt_paint_color(cr, 0, 0, mode->hdisplay, mode->vdisplay,
			    0.0, 1.0, 0.0);
	igt_paint_color(cr, rect_x, rect_y, rect_w, rect_h, 0.0, 0.0, 0.0);
	igt_assert(cairo_status(cr) == 0);
	cairo_destroy(cr);
}

enum {
	TEST_POSITION_FULLY_COVERED = 1 << 0,
};

static void
test_plane_position_with_output(data_t *data,
				enum pipe pipe,
				enum igt_plane plane,
				igt_output_t *output,
				unsigned int flags)
{
	test_position_t test = { .data = data };
	igt_plane_t *primary, *sprite;
	struct igt_fb primary_fb, sprite_fb;
	drmModeModeInfo *mode;
	igt_crc_t crc;

	igt_info("Testing connector %s using pipe %s plane %d\n",
		 igt_output_name(output), kmstest_pipe_name(pipe), plane);

	test_init(data, pipe);

	test_grab_crc(data, output, pipe, &green, &test.reference_crc);

	igt_output_set_pipe(output, pipe);

	mode = igt_output_get_mode(output);
	primary = igt_output_get_plane(output, 0);
	sprite = igt_output_get_plane(output, plane);

	create_fb_for_mode__position(data, mode, 100, 100, 64, 64,
				     &primary_fb);
	igt_plane_set_fb(primary, &primary_fb);

	igt_create_color_fb(data->drm_fd,
				64, 64, /* width, height */
				DRM_FORMAT_XRGB8888,
				I915_TILING_NONE,
				0.0, 1.0, 0.0,
				&sprite_fb);
	igt_plane_set_fb(sprite, &sprite_fb);

	if (flags & TEST_POSITION_FULLY_COVERED)
		igt_plane_set_position(sprite, 100, 100);
	else
		igt_plane_set_position(sprite, 132, 132);

	igt_display_commit(&data->display);

	igt_pipe_crc_collect_crc(data->pipe_crc, &crc);

	if (flags & TEST_POSITION_FULLY_COVERED)
		igt_assert(igt_crc_equal(&test.reference_crc, &crc));
	else
		igt_assert(!igt_crc_equal(&test.reference_crc, &crc));

	igt_plane_set_fb(primary, NULL);
	igt_plane_set_fb(sprite, NULL);

	/* reset the constraint on the pipe */
	igt_output_set_pipe(output, PIPE_ANY);

	test_fini(data);
}

static void
test_plane_position(data_t *data, enum pipe pipe, enum igt_plane plane,
		    unsigned int flags)
{
	igt_output_t *output;

	igt_skip_on(pipe >= data->display.n_pipes);
	igt_skip_on(plane >= data->display.pipes[pipe].n_planes);

	for_each_connected_output(&data->display, output)
		test_plane_position_with_output(data, pipe, plane, output,
						flags);
}

/*
 * Plane panning test.
 *   - We start by grabbing reference CRCs of a full red and a full blue fb
 *     being scanned out on the primary plane
 *   - Then we create a big fb, sized (2 * hdisplay, 2 * vdisplay) and:
 *      - fill the top left quarter with red
 *      - fill the bottom right quarter with blue
 *   - The TEST_PANNING_TOP_LEFT test makes sure that with panning at (0, 0)
 *     we do get the same CRC than the full red fb.
 *   - The TEST_PANNING_BOTTOM_RIGHT test makes sure that with panning at
 *     (vdisplay, hdisplay) we do get the same CRC than the full blue fb.
 */
typedef struct {
	data_t *data;
	igt_crc_t red_crc, blue_crc;
} test_panning_t;

static void
create_fb_for_mode__panning(data_t *data, drmModeModeInfo *mode,
			    struct igt_fb *fb /* out */)
{
	unsigned int fb_id;
	cairo_t *cr;

	fb_id = igt_create_fb(data->drm_fd,
			      mode->hdisplay * 2, mode->vdisplay * 2,
			      DRM_FORMAT_XRGB8888,
			      I915_TILING_NONE,
			      fb);
	igt_assert(fb_id);

	cr = igt_get_cairo_ctx(data->drm_fd, fb);

	igt_paint_color(cr, 0, 0, mode->hdisplay, mode->vdisplay,
			1.0, 0.0, 0.0);

	igt_paint_color(cr,
			mode->hdisplay, mode->vdisplay,
			mode->hdisplay, mode->vdisplay,
			0.0, 0.0, 1.0);

	igt_assert(cairo_status(cr) == 0);
	cairo_destroy(cr);
}

enum {
	TEST_PANNING_TOP_LEFT	  = 1 << 0,
	TEST_PANNING_BOTTOM_RIGHT = 1 << 1,
};

static void
test_plane_panning_with_output(data_t *data,
			       enum pipe pipe,
			       enum igt_plane plane,
			       igt_output_t *output,
			       unsigned int flags)
{
	test_panning_t test = { .data = data };
	igt_plane_t *primary;
	struct igt_fb primary_fb;
	drmModeModeInfo *mode;
	igt_crc_t crc;

	igt_info("Testing connector %s using pipe %s plane %d\n",
		 igt_output_name(output), kmstest_pipe_name(pipe), plane);

	test_init(data, pipe);

	test_grab_crc(data, output, pipe, &red, &test.red_crc);
	test_grab_crc(data, output, pipe, &blue, &test.blue_crc);

	igt_output_set_pipe(output, pipe);

	mode = igt_output_get_mode(output);
	primary = igt_output_get_plane(output, 0);

	create_fb_for_mode__panning(data, mode, &primary_fb);
	igt_plane_set_fb(primary, &primary_fb);

	if (flags & TEST_PANNING_TOP_LEFT)
		igt_plane_set_panning(primary, 0, 0);
	else
		igt_plane_set_panning(primary, mode->hdisplay, mode->vdisplay);

	igt_plane_set_position(primary, 0, 0);

	igt_display_commit(&data->display);

	igt_pipe_crc_collect_crc(data->pipe_crc, &crc);

	if (flags & TEST_PANNING_TOP_LEFT)
		igt_assert(igt_crc_equal(&test.red_crc, &crc));
	else
		igt_assert(igt_crc_equal(&test.blue_crc, &crc));

	igt_plane_set_fb(primary, NULL);

	/* reset states to neutral values, assumed by other tests */
	igt_output_set_pipe(output, PIPE_ANY);
	igt_plane_set_panning(primary, 0, 0);

	test_fini(data);
}

static void
test_plane_panning(data_t *data, enum pipe pipe, enum igt_plane plane,
            unsigned int flags)
{
	igt_output_t *output;

	igt_skip_on(pipe >= data->display.n_pipes);
	igt_skip_on(plane >= data->display.pipes[pipe].n_planes);

	for_each_connected_output(&data->display, output)
		test_plane_panning_with_output(data, pipe, plane, output,
						flags);
}

static void
run_tests_for_pipe_plane(data_t *data, enum pipe pipe, enum igt_plane plane)
{
	igt_subtest_f("plane-position-covered-pipe-%s-plane-%d",
		      kmstest_pipe_name(pipe), plane)
		test_plane_position(data, pipe, plane,
				    TEST_POSITION_FULLY_COVERED);

	igt_subtest_f("plane-position-hole-pipe-%s-plane-%d",
		      kmstest_pipe_name(pipe), plane)
		test_plane_position(data, pipe, plane, 0);

	igt_subtest_f("plane-panning-top-left-pipe-%s-plane-%d",
		      kmstest_pipe_name(pipe), plane)
		test_plane_panning(data, pipe, plane, TEST_PANNING_TOP_LEFT);

	igt_subtest_f("plane-panning-bottom-right-pipe-%s-plane-%d",
		      kmstest_pipe_name(pipe), plane)
		test_plane_panning(data, pipe, plane,
				   TEST_PANNING_BOTTOM_RIGHT);

}

static void
run_tests_for_pipe(data_t *data, enum pipe pipe)
{
	int plane;

	for (plane = 1; plane < IGT_MAX_PLANES; plane++)
		run_tests_for_pipe_plane(data, pipe, plane);
}

static data_t data;

igt_main
{

	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_any_master();

		kmstest_set_vt_graphics_mode();

		igt_require_pipe_crc();
		igt_display_init(&data.display, data.drm_fd);
	}

	for (int pipe = 0; pipe < 3; pipe++)
		run_tests_for_pipe(&data, pipe);

	igt_fixture {
		igt_display_fini(&data.display);
	}
}
