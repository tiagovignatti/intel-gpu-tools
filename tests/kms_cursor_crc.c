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
#include "intel_chipset.h"
#include "igt_aux.h"

IGT_TEST_DESCRIPTION(
   "Use the display CRC support to validate cursor plane functionality. "
   "The test will position the cursor plane either fully onscreen, "
   "partially onscreen, or fully offscreen, using either a fully opaque "
   "or fully transparent surface. In each case it then reads the PF CRC "
   "and compares it with the CRC value obtained when the cursor plane "
   "was disabled.");

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
	igt_output_t *output;
	enum pipe pipe;
	igt_crc_t ref_crc;
	int left, right, top, bottom;
	int screenw, screenh;
	int curw, curh; /* cursor size */
	int cursor_max_w, cursor_max_h;
	igt_pipe_crc_t *pipe_crc;
	uint32_t devid;
} data_t;

static void draw_cursor(cairo_t *cr, int x, int y, int cw, int ch)
{
	int wl, wr, ht, hb;

	/* deal with odd cursor width/height */
	wl = cw / 2;
	wr = (cw + 1) / 2;
	ht = ch / 2;
	hb = (ch + 1) / 2;

	/* Cairo doesn't like to be fed numbers that are too wild */
	if ((x < SHRT_MIN) || (x > SHRT_MAX) || (y < SHRT_MIN) || (y > SHRT_MAX))
		return;
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
	/* 4 color rectangles in the corners, RGBY */
	igt_paint_color_alpha(cr, x,      y,      wl, ht, 1.0, 0.0, 0.0, 1.0);
	igt_paint_color_alpha(cr, x + wl, y,      wr, ht, 0.0, 1.0, 0.0, 1.0);
	igt_paint_color_alpha(cr, x,      y + ht, wl, hb, 0.0, 0.0, 1.0, 1.0);
	igt_paint_color_alpha(cr, x + wl, y + ht, wr, hb, 0.5, 0.5, 0.5, 1.0);
}

static void cursor_enable(data_t *data)
{
	igt_output_t *output = data->output;
	igt_plane_t *cursor;

	cursor = igt_output_get_plane(output, IGT_PLANE_CURSOR);
	igt_plane_set_fb(cursor, &data->fb);
	igt_plane_set_size(cursor, data->curw, data->curh);
}

static void cursor_disable(data_t *data)
{
	igt_output_t *output = data->output;
	igt_plane_t *cursor;

	cursor = igt_output_get_plane(output, IGT_PLANE_CURSOR);
	igt_plane_set_fb(cursor, NULL);
}


static void do_single_test(data_t *data, int x, int y)
{
	igt_display_t *display = &data->display;
	igt_pipe_crc_t *pipe_crc = data->pipe_crc;
	igt_crc_t crc, ref_crc;
	igt_plane_t *cursor;
	cairo_t *cr = igt_get_cairo_ctx(data->drm_fd, &data->primary_fb);

	igt_print_activity();

	/* Hardware test */
	igt_paint_test_pattern(cr, data->screenw, data->screenh);
	cursor_enable(data);
	cursor = igt_output_get_plane(data->output, IGT_PLANE_CURSOR);
	igt_plane_set_position(cursor, x, y);
	igt_display_commit(display);
	igt_wait_for_vblank(data->drm_fd, data->pipe);
	igt_pipe_crc_collect_crc(pipe_crc, &crc);
	cursor_disable(data);
	igt_display_commit(display);

	/* Now render the same in software and collect crc */
	draw_cursor(cr, x, y, data->curw, data->curh);
	igt_display_commit(display);
	igt_wait_for_vblank(data->drm_fd, data->pipe);
	igt_pipe_crc_collect_crc(pipe_crc, &ref_crc);
	/* Clear screen afterwards */
	igt_paint_color(cr, 0, 0, data->screenw, data->screenh,
			0.0, 0.0, 0.0);

	igt_assert(igt_crc_equal(&crc, &ref_crc));
}

static void do_fail_test(data_t *data, int x, int y, int expect)
{
	igt_display_t *display = &data->display;
	igt_plane_t *cursor;
	cairo_t *cr = igt_get_cairo_ctx(data->drm_fd, &data->primary_fb);
	int ret;

	igt_print_activity();

	/* Hardware test */
	igt_paint_test_pattern(cr, data->screenw, data->screenh);
	cursor_enable(data);
	cursor = igt_output_get_plane(data->output, IGT_PLANE_CURSOR);
	igt_plane_set_position(cursor, x, y);
	ret = igt_display_try_commit2(display, COMMIT_LEGACY);

	igt_plane_set_position(cursor, 0, 0);
	cursor_disable(data);
	igt_display_commit(display);

	igt_assert_eq(ret, expect);
}

static void do_test(data_t *data,
		    int left, int right, int top, int bottom)
{
	do_single_test(data, left, top);
	do_single_test(data, right, top);
	do_single_test(data, right, bottom);
	do_single_test(data, left, bottom);
}

static void test_crc_onscreen(data_t *data)
{
	int left = data->left;
	int right = data->right;
	int top = data->top;
	int bottom = data->bottom;
	int cursor_w = data->curw;
	int cursor_h = data->curh;

	/* fully inside  */
	do_test(data, left, right, top, bottom);

	/* 2 pixels inside */
	do_test(data, left - (cursor_w-2), right + (cursor_w-2), top               , bottom               );
	do_test(data, left               , right               , top - (cursor_h-2), bottom + (cursor_h-2));
	do_test(data, left - (cursor_w-2), right + (cursor_w-2), top - (cursor_h-2), bottom + (cursor_h-2));

	/* 1 pixel inside */
	do_test(data, left - (cursor_w-1), right + (cursor_w-1), top               , bottom               );
	do_test(data, left               , right               , top - (cursor_h-1), bottom + (cursor_h-1));
	do_test(data, left - (cursor_w-1), right + (cursor_w-1), top - (cursor_h-1), bottom + (cursor_h-1));
}

static void test_crc_offscreen(data_t *data)
{
	int left = data->left;
	int right = data->right;
	int top = data->top;
	int bottom = data->bottom;
	int cursor_w = data->curw;
	int cursor_h = data->curh;

	/* fully outside */
	do_test(data, left - (cursor_w), right + (cursor_w), top             , bottom             );
	do_test(data, left             , right             , top - (cursor_h), bottom + (cursor_h));
	do_test(data, left - (cursor_w), right + (cursor_w), top - (cursor_h), bottom + (cursor_h));

	/* fully outside by 1 extra pixels */
	do_test(data, left - (cursor_w+1), right + (cursor_w+1), top               , bottom               );
	do_test(data, left               , right               , top - (cursor_h+1), bottom + (cursor_h+1));
	do_test(data, left - (cursor_w+1), right + (cursor_w+1), top - (cursor_h+1), bottom + (cursor_h+1));

	/* fully outside by 2 extra pixels */
	do_test(data, left - (cursor_w+2), right + (cursor_w+2), top               , bottom               );
	do_test(data, left               , right               , top - (cursor_h+2), bottom + (cursor_h+2));
	do_test(data, left - (cursor_w+2), right + (cursor_w+2), top - (cursor_h+2), bottom + (cursor_h+2));

	/* fully outside by a lot of extra pixels */
	do_test(data, left - (cursor_w+512), right + (cursor_w+512), top                 , bottom                 );
	do_test(data, left                 , right                 , top - (cursor_h+512), bottom + (cursor_h+512));
	do_test(data, left - (cursor_w+512), right + (cursor_w+512), top - (cursor_h+512), bottom + (cursor_h+512));

	/* go nuts */
	do_test(data, INT_MIN, INT_MAX - cursor_w, INT_MIN, INT_MAX - cursor_h);
	do_test(data, SHRT_MIN, SHRT_MAX, SHRT_MIN, SHRT_MAX);

	/* Make sure we get -ERANGE on integer overflow */
	do_fail_test(data, INT_MAX - cursor_w + 1, INT_MAX - cursor_h + 1, -ERANGE);
}

static void test_crc_sliding(data_t *data)
{
	int i;

	/* Make sure cursor moves smoothly and pixel-by-pixel, and that there are
	 * no alignment issues. Horizontal, vertical and diagonal test.
	 */
	for (i = 0; i < 16; i++) {
		do_single_test(data, i, 0);
		do_single_test(data, 0, i);
		do_single_test(data, i, i);
	}
}

static void test_crc_random(data_t *data)
{
	int i;

	/* Random cursor placement */
	for (i = 0; i < 50; i++) {
		int x = rand() % (data->screenw + data->curw * 2) - data->curw;
		int y = rand() % (data->screenh + data->curh * 2) - data->curh;
		do_single_test(data, x, y);
	}
}

static bool prepare_crtc(data_t *data, igt_output_t *output,
			 int cursor_w, int cursor_h)
{
	drmModeModeInfo *mode;
	igt_display_t *display = &data->display;
	igt_plane_t *primary;

	/* select the pipe we want to use */
	igt_output_set_pipe(output, data->pipe);
	cursor_disable(data);
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
			    I915_TILING_NONE,
			    0.0, 0.0, 0.0,
			    &data->primary_fb);

	primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
	igt_plane_set_fb(primary, &data->primary_fb);

	igt_display_commit(display);

	/* create the pipe_crc object for this pipe */
	if (data->pipe_crc)
		igt_pipe_crc_free(data->pipe_crc);

	data->pipe_crc = igt_pipe_crc_new(data->pipe,
					  INTEL_PIPE_CRC_SOURCE_AUTO);

	/* x/y position where the cursor is still fully visible */
	data->left = 0;
	data->right = mode->hdisplay - cursor_w;
	data->top = 0;
	data->bottom = mode->vdisplay - cursor_h;
	data->screenw = mode->hdisplay;
	data->screenh = mode->vdisplay;
	data->curw = cursor_w;
	data->curh = cursor_h;

	/* make sure cursor is disabled */
	cursor_disable(data);
	igt_wait_for_vblank(data->drm_fd, data->pipe);

	/* get reference crc w/o cursor */
	igt_pipe_crc_collect_crc(data->pipe_crc, &data->ref_crc);

	return true;
}

static void cleanup_crtc(data_t *data, igt_output_t *output)
{
	igt_display_t *display = &data->display;
	igt_plane_t *primary;

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	igt_remove_fb(data->drm_fd, &data->primary_fb);

	primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
	igt_plane_set_fb(primary, NULL);

	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit(display);
}

static void run_test(data_t *data, void (*testfunc)(data_t *), int cursor_w, int cursor_h)
{
	igt_display_t *display = &data->display;
	igt_output_t *output;
	enum pipe p;
	int valid_tests = 0;

	igt_require(cursor_w <= data->cursor_max_w &&
		    cursor_h <= data->cursor_max_h);

	for_each_connected_output(display, output) {
		data->output = output;
		for_each_pipe(display, p) {
			data->pipe = p;

			if (!prepare_crtc(data, output, cursor_w, cursor_h))
				continue;

			valid_tests++;

			igt_info("Beginning %s on pipe %s, connector %s\n",
				 igt_subtest_name(),
				 kmstest_pipe_name(data->pipe),
				 igt_output_name(output));

			testfunc(data);

			igt_info("\n%s on pipe %s, connector %s: PASSED\n\n",
				 igt_subtest_name(),
				 kmstest_pipe_name(data->pipe),
				 igt_output_name(output));

			/* cleanup what prepare_crtc() has done */
			cleanup_crtc(data, output);
		}
	}

	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

static void create_cursor_fb(data_t *data, int cur_w, int cur_h)
{
	cairo_t *cr;
	uint32_t fb_id;

	/*
	 * Make the FB slightly taller and leave the extra
	 * line opaque white, so that we can see that the
	 * hardware won't scan beyond what it should (esp.
	 * with non-square cursors).
	 */
	fb_id = igt_create_color_fb(data->drm_fd, cur_w, cur_h + 1,
				    DRM_FORMAT_ARGB8888, I915_TILING_NONE,
				    1.0, 1.0, 1.0,
				    &data->fb);

	igt_assert(fb_id);

	cr = igt_get_cairo_ctx(data->drm_fd, &data->fb);
	draw_cursor(cr, 0, 0, cur_w, cur_h);
	igt_assert(cairo_status(cr) == 0);
}

static bool has_nonsquare_cursors(uint32_t devid)
{
	/*
	 * Test non-square cursors a bit on the platforms
	 * that support such things.
	 */
	return devid == PCI_CHIP_845_G || devid == PCI_CHIP_I865_G;
}

static void test_cursor_size(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_pipe_crc_t *pipe_crc = data->pipe_crc;
	igt_crc_t crc[10], ref_crc;
	cairo_t *cr;
	uint32_t fb_id;
	int i, size;
	int cursor_max_size = data->cursor_max_w;
	int ret;

	/* Create a maximum size cursor, then change the size in flight to
	 * smaller ones to see that the size is applied correctly
	 */
	fb_id = igt_create_fb(data->drm_fd, cursor_max_size, cursor_max_size,
			      DRM_FORMAT_ARGB8888, I915_TILING_NONE, &data->fb);
	igt_assert(fb_id);

	/* Use a solid white rectangle as the cursor */
	cr = igt_get_cairo_ctx(data->drm_fd, &data->fb);
	igt_paint_color_alpha(cr, 0, 0, cursor_max_size, cursor_max_size, 1.0, 1.0, 1.0, 1.0);

	/* Hardware test loop */
	cursor_enable(data);
	ret = drmModeMoveCursor(data->drm_fd, data->output->config.crtc->crtc_id, 0, 0);
	igt_assert_eq(ret, 0);
	for (i = 0, size = cursor_max_size; size >= 64; size /= 2, i++) {
		/* Change size in flight: */
		ret = drmModeSetCursor(data->drm_fd, data->output->config.crtc->crtc_id,
				       data->fb.gem_handle, size, size);
		igt_assert_eq(ret, 0);
		igt_wait_for_vblank(data->drm_fd, data->pipe);
		igt_pipe_crc_collect_crc(pipe_crc, &crc[i]);
	}
	cursor_disable(data);
	/* Software test loop */
	cr = igt_get_cairo_ctx(data->drm_fd, &data->primary_fb);
	for (i = 0, size = cursor_max_size; size >= 64; size /= 2, i++) {
		/* Now render the same in software and collect crc */
		igt_paint_color_alpha(cr, 0, 0, size, size, 1.0, 1.0, 1.0, 1.0);
		igt_display_commit(display);
		igt_wait_for_vblank(data->drm_fd, data->pipe);
		igt_pipe_crc_collect_crc(pipe_crc, &ref_crc);
		/* Clear screen afterwards */
		igt_paint_color(cr, 0, 0, data->screenw, data->screenh,
				0.0, 0.0, 0.0);
		igt_assert(igt_crc_equal(&crc[i], &ref_crc));
	}
}

static void run_test_generic(data_t *data)
{
	int cursor_size;
	for (cursor_size = 64; cursor_size <= 512; cursor_size *= 2) {
		int w = cursor_size;
		int h = cursor_size;

		igt_fixture
			create_cursor_fb(data, w, h);

		/* Using created cursor FBs to test cursor support */
		igt_subtest_f("cursor-%dx%d-onscreen", w, h)
			run_test(data, test_crc_onscreen, w, h);
		igt_subtest_f("cursor-%dx%d-offscreen", w, h)
			run_test(data, test_crc_offscreen, w, h);
		igt_subtest_f("cursor-%dx%d-sliding", w, h)
			run_test(data, test_crc_sliding, w, h);
		igt_subtest_f("cursor-%dx%d-random", w, h)
			run_test(data, test_crc_random, w, h);

		igt_fixture
			igt_remove_fb(data->drm_fd, &data->fb);

		/*
		 * Test non-square cursors a bit on the platforms
		 * that support such things. And make it a bit more
		 * interesting by using a non-pot height.
		 */
		h /= 3;

		igt_fixture
			create_cursor_fb(data, w, h);

		/* Using created cursor FBs to test cursor support */
		igt_subtest_f("cursor-%dx%d-onscreen", w, h) {
			igt_require(has_nonsquare_cursors(data->devid));
			run_test(data, test_crc_onscreen, w, h);
		}
		igt_subtest_f("cursor-%dx%d-offscreen", w, h) {
			igt_require(has_nonsquare_cursors(data->devid));
			run_test(data, test_crc_offscreen, w, h);
		}
		igt_subtest_f("cursor-%dx%d-sliding", w, h) {
			igt_require(has_nonsquare_cursors(data->devid));
			run_test(data, test_crc_sliding, w, h);
		}
		igt_subtest_f("cursor-%dx%d-random", w, h) {
			igt_require(has_nonsquare_cursors(data->devid));
			run_test(data, test_crc_random, w, h);
		}

		igt_fixture
			igt_remove_fb(data->drm_fd, &data->fb);
	}
}

static data_t data;

igt_main
{
	uint64_t cursor_width = 64, cursor_height = 64;
	int ret;

	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_any_master();

		data.devid = intel_get_drm_devid(data.drm_fd);

		ret = drmGetCap(data.drm_fd, DRM_CAP_CURSOR_WIDTH, &cursor_width);
		igt_assert(ret == 0 || errno == EINVAL);
		/* Not making use of cursor_height since it is same as width, still reading */
		ret = drmGetCap(data.drm_fd, DRM_CAP_CURSOR_HEIGHT, &cursor_height);
		igt_assert(ret == 0 || errno == EINVAL);

		/* We assume width and height are same so max is assigned width */
		igt_assert_eq(cursor_width, cursor_height);

		kmstest_set_vt_graphics_mode();

		igt_require_pipe_crc();

		igt_display_init(&data.display, data.drm_fd);
	}

	data.cursor_max_w = cursor_width;
	data.cursor_max_h = cursor_height;

	igt_subtest_f("cursor-size-change")
		run_test(&data, test_cursor_size, cursor_width, cursor_height);

	run_test_generic(&data);

	igt_fixture {
		igt_display_fini(&data.display);
	}
}
