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

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"

#ifndef DRM_CAP_CURSOR_WIDTH
#define DRM_CAP_CURSOR_WIDTH 0x8
#endif
#ifndef DRM_CAP_CURSOR_HEIGHT
#define DRM_CAP_CURSOR_HEIGHT 0x9
#endif

typedef struct {
	int drm_fd;
	igt_display_t display;
	struct igt_fb primary_fb;
	struct igt_fb fb;
} data_t;

typedef struct {
	data_t *data;
	igt_output_t *output;
	enum pipe pipe;
	igt_crc_t ref_crc;
	int left, right, top, bottom;
	int screenw, screenh;
	int curw, curh; /* cursor size */
	int cursor_max_size;
	igt_pipe_crc_t *pipe_crc;
} test_data_t;

static void draw_cursor(cairo_t *cr, int x, int y, int w)
{
	w /= 2;
	/* Cairo doesn't like to be fed numbers that are too wild */
	if ((x < SHRT_MIN) || (x > SHRT_MAX) || (y < SHRT_MIN) || (y > SHRT_MAX))
		return;
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
	/* 4 color rectangles in the corners, RGBY */
	igt_paint_color_alpha(cr, x, y, w, w, 1.0, 0.0, 0.0, 1.0);
	igt_paint_color_alpha(cr, x + w, y, w, w, 0.0, 1.0, 0.0, 1.0);
	igt_paint_color_alpha(cr, x, y + w, w, w, 0.0, 0.0, 1.0, 1.0);
	igt_paint_color_alpha(cr, x + w, y + w, w, w, 0.5, 0.5, 0.5, 1.0);
}

static void cursor_enable(test_data_t *test_data)
{
	data_t *data = test_data->data;
	igt_display_t *display = &data->display;
	igt_output_t *output = test_data->output;
	igt_plane_t *cursor;

	cursor = igt_output_get_plane(output, IGT_PLANE_CURSOR);
	igt_plane_set_fb(cursor, &data->fb);
	igt_display_commit(display);
}

static void cursor_disable(test_data_t *test_data)
{
	data_t *data = test_data->data;
	igt_display_t *display = &data->display;
	igt_output_t *output = test_data->output;
	igt_plane_t *cursor;

	cursor = igt_output_get_plane(output, IGT_PLANE_CURSOR);
	igt_plane_set_fb(cursor, NULL);
	igt_display_commit(display);
}


static void do_single_test(test_data_t *test_data, int x, int y)
{
	data_t *data = test_data->data;
	igt_display_t *display = &data->display;
	igt_pipe_crc_t *pipe_crc = test_data->pipe_crc;
	igt_crc_t crc, ref_crc;
	igt_plane_t *cursor;
	cairo_t *cr = igt_get_cairo_ctx(data->drm_fd, &data->primary_fb);

	igt_info("."); fflush(stdout);

	/* Hardware test */
	igt_paint_test_pattern(cr, test_data->screenw, test_data->screenh);
	cursor_enable(test_data);
	cursor = igt_output_get_plane(test_data->output, IGT_PLANE_CURSOR);
	igt_plane_set_position(cursor, x, y);
	igt_display_commit(display);
	igt_wait_for_vblank(data->drm_fd, test_data->pipe);
	igt_pipe_crc_collect_crc(pipe_crc, &crc);
	cursor_disable(test_data);

	/* Now render the same in software and collect crc */
	draw_cursor(cr, x, y, test_data->curw);
	igt_display_commit(display);
	igt_wait_for_vblank(data->drm_fd, test_data->pipe);
	igt_pipe_crc_collect_crc(pipe_crc, &ref_crc);
	/* Clear screen afterwards */
	igt_paint_color(cr, 0, 0, test_data->screenw, test_data->screenh,
			    0.0, 0.0, 0.0);

	igt_assert(igt_crc_equal(&crc, &ref_crc));
}

static void do_test(test_data_t *test_data,
		    int left, int right, int top, int bottom)
{
	do_single_test(test_data, left, top);
	do_single_test(test_data, right, top);
	do_single_test(test_data, right, bottom);
	do_single_test(test_data, left, bottom);
}

static void test_crc_onscreen(test_data_t *test_data)
{
	int left = test_data->left;
	int right = test_data->right;
	int top = test_data->top;
	int bottom = test_data->bottom;
	int cursor_w = test_data->curw;
	int cursor_h = test_data->curh;

	/* fully inside  */
	do_test(test_data, left, right, top, bottom);

	/* 2 pixels inside */
	do_test(test_data, left - (cursor_w-2), right + (cursor_w-2), top               , bottom               );
	do_test(test_data, left               , right               , top - (cursor_h-2), bottom + (cursor_h-2));
	do_test(test_data, left - (cursor_w-2), right + (cursor_w-2), top - (cursor_h-2), bottom + (cursor_h-2));

	/* 1 pixel inside */
	do_test(test_data, left - (cursor_w-1), right + (cursor_w-1), top               , bottom               );
	do_test(test_data, left               , right               , top - (cursor_h-1), bottom + (cursor_h-1));
	do_test(test_data, left - (cursor_w-1), right + (cursor_w-1), top - (cursor_h-1), bottom + (cursor_h-1));
}

static void test_crc_offscreen(test_data_t *test_data)
{
	int left = test_data->left;
	int right = test_data->right;
	int top = test_data->top;
	int bottom = test_data->bottom;
	int cursor_w = test_data->curw;
	int cursor_h = test_data->curh;

	/* fully outside */
	do_test(test_data, left - (cursor_w), right + (cursor_w), top             , bottom             );
	do_test(test_data, left             , right             , top - (cursor_h), bottom + (cursor_h));
	do_test(test_data, left - (cursor_w), right + (cursor_w), top - (cursor_h), bottom + (cursor_h));

	/* fully outside by 1 extra pixels */
	do_test(test_data, left - (cursor_w+1), right + (cursor_w+1), top               , bottom               );
	do_test(test_data, left               , right               , top - (cursor_h+1), bottom + (cursor_h+1));
	do_test(test_data, left - (cursor_w+1), right + (cursor_w+1), top - (cursor_h+1), bottom + (cursor_h+1));

	/* fully outside by 2 extra pixels */
	do_test(test_data, left - (cursor_w+2), right + (cursor_w+2), top               , bottom               );
	do_test(test_data, left               , right               , top - (cursor_h+2), bottom + (cursor_h+2));
	do_test(test_data, left - (cursor_w+2), right + (cursor_w+2), top - (cursor_h+2), bottom + (cursor_h+2));

	/* fully outside by a lot of extra pixels */
	do_test(test_data, left - (cursor_w+512), right + (cursor_w+512), top                 , bottom                 );
	do_test(test_data, left                 , right                 , top - (cursor_h+512), bottom + (cursor_h+512));
	do_test(test_data, left - (cursor_w+512), right + (cursor_w+512), top - (cursor_h+512), bottom + (cursor_h+512));

	/* go nuts */
	do_test(test_data, INT_MIN, INT_MAX, INT_MIN, INT_MAX);
}

static void test_crc_sliding(test_data_t *test_data)
{
	int i;

	/* Make sure cursor moves smoothly and pixel-by-pixel, and that there are
	 * no alignment issues. Horizontal, vertical and diagonal test.
	 */
	for (i = 0; i < 16; i++) {
		do_single_test(test_data, i, 0);
		do_single_test(test_data, 0, i);
		do_single_test(test_data, i, i);
	}
}

static void test_crc_random(test_data_t *test_data)
{
	int i;

	/* Random cursor placement */
	for (i = 0; i < 50; i++) {
		int x = rand() % (test_data->screenw + test_data->curw * 2) - test_data->curw;
		int y = rand() % (test_data->screenh + test_data->curh * 2) - test_data->curh;
		do_single_test(test_data, x, y);
	}
}

static bool prepare_crtc(test_data_t *test_data, igt_output_t *output,
			 int cursor_w, int cursor_h)
{
	drmModeModeInfo *mode;
	data_t *data = test_data->data;
	igt_display_t *display = &data->display;
	igt_plane_t *primary;

	/* select the pipe we want to use */
	igt_output_set_pipe(output, test_data->pipe);
	igt_display_commit(display);

	if (!output->valid) {
		igt_output_set_pipe(output, PIPE_ANY);
		igt_display_commit(display);
		return false;
	}

	/* create and set the primary plane fb */
	mode = igt_output_get_mode(output);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
				DRM_FORMAT_XRGB8888,
				false, /* tiled */
				0.0, 0.0, 0.0,
				&data->primary_fb);

	primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
	igt_plane_set_fb(primary, &data->primary_fb);

	igt_display_commit(display);

	/* create the pipe_crc object for this pipe */
	if (test_data->pipe_crc)
		igt_pipe_crc_free(test_data->pipe_crc);

	test_data->pipe_crc = igt_pipe_crc_new(test_data->pipe,
					       INTEL_PIPE_CRC_SOURCE_AUTO);
	if (!test_data->pipe_crc) {
		igt_info("auto crc not supported on this connector with pipe %i\n",
			 test_data->pipe);
		return false;
	}

	/* x/y position where the cursor is still fully visible */
	test_data->left = 0;
	test_data->right = mode->hdisplay - cursor_w;
	test_data->top = 0;
	test_data->bottom = mode->vdisplay - cursor_h;
	test_data->screenw = mode->hdisplay;
	test_data->screenh = mode->vdisplay;
	test_data->curw = cursor_w;
	test_data->curh = cursor_h;
	test_data->cursor_max_size = cursor_w;

	/* make sure cursor is disabled */
	cursor_disable(test_data);
	igt_wait_for_vblank(data->drm_fd, test_data->pipe);

	/* get reference crc w/o cursor */
	igt_pipe_crc_collect_crc(test_data->pipe_crc, &test_data->ref_crc);

	return true;
}

static void cleanup_crtc(test_data_t *test_data, igt_output_t *output)
{
	data_t *data = test_data->data;
	igt_display_t *display = &data->display;
	igt_plane_t *primary;

	igt_pipe_crc_free(test_data->pipe_crc);
	test_data->pipe_crc = NULL;

	igt_remove_fb(data->drm_fd, &data->primary_fb);

	primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
	igt_plane_set_fb(primary, NULL);

	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit(display);
}

static void run_test(data_t *data, void (*testfunc)(test_data_t *), int cursor_w, int cursor_h)
{
	igt_display_t *display = &data->display;
	igt_output_t *output;
	enum pipe p;
	test_data_t test_data = {
		.data = data,
	};
	int valid_tests = 0;

	for_each_connected_output(display, output) {
		test_data.output = output;
		for (p = 0; p < igt_display_get_n_pipes(display); p++) {
			test_data.pipe = p;

			if (!prepare_crtc(&test_data, output, cursor_w, cursor_h))
				continue;

			valid_tests++;

			igt_info("Beginning %s on pipe %c, connector %s\n",
				 igt_subtest_name(), pipe_name(test_data.pipe),
				 igt_output_name(output));

			testfunc(&test_data);

			igt_info("\n%s on pipe %c, connector %s: PASSED\n\n",
				 igt_subtest_name(), pipe_name(test_data.pipe),
				 igt_output_name(output));

			/* cleanup what prepare_crtc() has done */
			cleanup_crtc(&test_data, output);
		}
	}

	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

static void create_cursor_fb(data_t *data, int cur_w, int cur_h)
{
	cairo_t *cr;
	uint32_t fb_id;

	fb_id = igt_create_fb(data->drm_fd, cur_w, cur_h,
			      DRM_FORMAT_ARGB8888, false,
			      &data->fb);
	igt_assert(fb_id);

	cr = igt_get_cairo_ctx(data->drm_fd, &data->fb);
	draw_cursor(cr, 0, 0, cur_w);
	igt_assert(cairo_status(cr) == 0);
}

static void test_cursor_size(test_data_t *test_data)
{
	data_t *data = test_data->data;
	igt_display_t *display = &data->display;
	igt_pipe_crc_t *pipe_crc = test_data->pipe_crc;
	igt_crc_t crc[10], ref_crc;
	igt_plane_t *cursor;
	cairo_t *cr;
	uint32_t fb_id;
	int i, size, cursor_max_size = test_data->cursor_max_size;

	/* Create a maximum size cursor, then change the size in flight to
	 * smaller ones to see that the size is applied correctly
	 */
	fb_id = igt_create_fb(data->drm_fd, cursor_max_size, cursor_max_size,
			      DRM_FORMAT_ARGB8888, false, &data->fb);
	igt_assert(fb_id);

	/* Use a solid white rectangle as the cursor */
	cr = igt_get_cairo_ctx(data->drm_fd, &data->fb);
	igt_paint_color_alpha(cr, 0, 0, cursor_max_size, cursor_max_size, 1.0, 1.0, 1.0, 1.0);

	/* Hardware test loop */
	cursor_enable(test_data);
	cursor = igt_output_get_plane(test_data->output, IGT_PLANE_CURSOR);
	igt_plane_set_position(cursor, 0, 0);
	for (i = 0, size = cursor_max_size; size >= 64; size /= 2, i++) {
		/* Change size in flight: */
		int ret = drmModeSetCursor(data->drm_fd, test_data->output->config.crtc->crtc_id,
					   data->fb.gem_handle, size, size);
		igt_assert(ret == 0);
		igt_wait_for_vblank(data->drm_fd, test_data->pipe);
		igt_pipe_crc_collect_crc(pipe_crc, &crc[i]);
	}
	cursor_disable(test_data);
	/* Software test loop */
	cr = igt_get_cairo_ctx(data->drm_fd, &data->primary_fb);
	for (i = 0, size = cursor_max_size; size >= 64; size /= 2, i++) {
		/* Now render the same in software and collect crc */
		igt_paint_color_alpha(cr, 0, 0, size, size, 1.0, 1.0, 1.0, 1.0);
		igt_display_commit(display);
		igt_wait_for_vblank(data->drm_fd, test_data->pipe);
		igt_pipe_crc_collect_crc(pipe_crc, &ref_crc);
		/* Clear screen afterwards */
		igt_paint_color(cr, 0, 0, test_data->screenw, test_data->screenh,
				0.0, 0.0, 0.0);
		igt_assert(igt_crc_equal(&crc[i], &ref_crc));
	}
}

static void run_test_generic(data_t *data, int cursor_max_size)
{
	int cursor_size;
	for (cursor_size = 64; cursor_size <= 256; cursor_size *= 2)
	{
		igt_fixture
			igt_require(cursor_max_size >= cursor_size);

		igt_fixture
			create_cursor_fb(data, cursor_size, cursor_size);

		/* Using created cursor FBs to test cursor support */
		igt_subtest_f("cursor-%d-onscreen", cursor_size)
			run_test(data, test_crc_onscreen, cursor_size, cursor_size);
		igt_subtest_f("cursor-%d-offscreen", cursor_size)
			run_test(data, test_crc_offscreen, cursor_size, cursor_size);
		igt_subtest_f("cursor-%d-sliding", cursor_size)
			run_test(data, test_crc_sliding, cursor_size, cursor_size);
		igt_subtest_f("cursor-%d-random", cursor_size)
			run_test(data, test_crc_random, cursor_size, cursor_size);

		igt_fixture
			igt_remove_fb(data->drm_fd, &data->fb);
	}
}

igt_main
{
	uint64_t cursor_width = 64, cursor_height = 64;
	data_t data = {};
	int ret;

	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_any();

		ret = drmGetCap(data.drm_fd, DRM_CAP_CURSOR_WIDTH, &cursor_width);
		igt_assert(ret == 0 || errno == EINVAL);
		/* Not making use of cursor_height since it is same as width, still reading */
		ret = drmGetCap(data.drm_fd, DRM_CAP_CURSOR_HEIGHT, &cursor_height);
		igt_assert(ret == 0 || errno == EINVAL);

		/* We assume width and height are same so max is assigned width */
		igt_assert_cmpint(cursor_width, ==, cursor_height);

		igt_set_vt_graphics_mode();

		igt_require_pipe_crc();

		igt_display_init(&data.display, data.drm_fd);
	}

	igt_subtest_f("cursor-size-change")
		run_test(&data, test_cursor_size, cursor_width, cursor_height);

	run_test_generic(&data, cursor_width);

	igt_fixture {
		igt_display_fini(&data.display);
	}
}
