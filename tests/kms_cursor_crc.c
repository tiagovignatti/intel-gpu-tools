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

enum cursor_type {
	WHITE_VISIBLE,
	WHITE_INVISIBLE,
	BLACK_VISIBLE,
	BLACK_INVISIBLE,
	NUM_CURSOR_TYPES,
};

typedef struct {
	int drm_fd;
	igt_display_t display;
	struct kmstest_fb primary_fb;
	struct kmstest_fb fb[NUM_CURSOR_TYPES];
	igt_pipe_crc_t **pipe_crc;
} data_t;

typedef struct {
	data_t *data;
	igt_output_t *output;
	enum pipe pipe;
	igt_crc_t ref_crc;
	bool crc_must_match;
	int left, right, top, bottom;
} test_data_t;


static igt_pipe_crc_t *create_crc(data_t *data, enum pipe pipe)
{
	igt_pipe_crc_t *crc;

	crc = igt_pipe_crc_new(pipe, INTEL_PIPE_CRC_SOURCE_AUTO);
	return crc;
}

static void do_single_test(test_data_t *test_data, int x, int y)
{
	data_t *data = test_data->data;
	igt_display_t *display = &data->display;
	igt_pipe_crc_t *pipe_crc = data->pipe_crc[test_data->pipe];
	igt_crc_t crc;
	igt_plane_t *cursor;

	printf("."); fflush(stdout);

	cursor = igt_output_get_plane(test_data->output, IGT_PLANE_CURSOR);
	igt_plane_set_position(cursor, x, y);
	igt_display_commit(display);
	igt_wait_for_vblank(data->drm_fd, test_data->pipe);

	igt_pipe_crc_collect_crc(pipe_crc, &crc);
	if (test_data->crc_must_match)
		igt_assert(igt_crc_equal(&crc, &test_data->ref_crc));
	else
		igt_assert(!igt_crc_equal(&crc, &test_data->ref_crc));
}

static void do_test(test_data_t *test_data,
		    int left, int right, int top, int bottom)
{
	do_single_test(test_data, left, top);
	do_single_test(test_data, right, top);
	do_single_test(test_data, right, bottom);
	do_single_test(test_data, left, bottom);
}

static void cursor_enable(test_data_t *test_data, enum cursor_type cursor_type)
{
	data_t *data = test_data->data;
	igt_display_t *display = &data->display;
	igt_output_t *output = test_data->output;
	igt_plane_t *cursor;

	cursor = igt_output_get_plane(output, IGT_PLANE_CURSOR);
	igt_plane_set_fb(cursor, &data->fb[cursor_type]);
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

static void test_crc(test_data_t *test_data, enum cursor_type cursor_type,
		     bool onscreen)
{
	int left = test_data->left;
	int right = test_data->right;
	int top = test_data->top;
	int bottom = test_data->bottom;

	cursor_enable(test_data, cursor_type);

	if (onscreen) {
		/* cursor onscreen, crc should match, except when white visible cursor is used */
		test_data->crc_must_match = cursor_type != WHITE_VISIBLE;

		/* fully inside  */
		do_test(test_data, left, right, top, bottom);

		/* 2 pixels inside */
		do_test(test_data, left - 62, right + 62, top     , bottom     );
		do_test(test_data, left     , right     , top - 62, bottom + 62);
		do_test(test_data, left - 62, right + 62, top - 62, bottom + 62);

		/* 1 pixel inside */
		do_test(test_data, left - 63, right + 63, top     , bottom     );
		do_test(test_data, left     , right     , top - 63, bottom + 63);
		do_test(test_data, left - 63, right + 63, top - 63, bottom + 63);
	} else {
		/* cursor offscreen, crc should always match */
		test_data->crc_must_match = true;

		/* fully outside */
		do_test(test_data, left - 64, right + 64, top     , bottom     );
		do_test(test_data, left     , right     , top - 64, bottom + 64);
		do_test(test_data, left - 64, right + 64, top - 64, bottom + 64);

		/* fully outside by 1 extra pixels */
		do_test(test_data, left - 65, right + 65, top     , bottom     );
		do_test(test_data, left     , right     , top - 65, bottom + 65);
		do_test(test_data, left - 65, right + 65, top - 65, bottom + 65);

		/* fully outside by 2 extra pixels */
		do_test(test_data, left - 66, right + 66, top     , bottom     );
		do_test(test_data, left     , right     , top - 66, bottom + 66);
		do_test(test_data, left - 66, right + 66, top - 66, bottom + 66);

		/* fully outside by a lot of extra pixels */
		do_test(test_data, left - 512, right + 512, top      , bottom      );
		do_test(test_data, left      , right      , top - 512, bottom + 512);
		do_test(test_data, left - 512, right + 512, top - 512, bottom + 512);

		/* go nuts */
		do_test(test_data, INT_MIN, INT_MAX, INT_MIN, INT_MAX);
	}

	cursor_disable(test_data);
}

static bool prepare_crtc(test_data_t *test_data, igt_output_t *output)
{
	drmModeModeInfo *mode;
	data_t *data = test_data->data;
	igt_display_t *display = &data->display;
	igt_pipe_crc_t *pipe_crc;
	igt_plane_t *primary;

	/* select the pipe we want to use */
	igt_output_set_pipe(output, test_data->pipe);

	/* create and set the primary plane fb */
	mode = igt_output_get_mode(output);
	kmstest_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
				DRM_FORMAT_XRGB8888,
				false, /* tiled */
				0.0, 0.0, 0.0,
				&data->primary_fb);

	primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
	igt_plane_set_fb(primary, &data->primary_fb);

	igt_display_commit(display);

	/* create the pipe_crc object for this pipe */
	igt_pipe_crc_free(data->pipe_crc[test_data->pipe]);
	data->pipe_crc[test_data->pipe] = NULL;

	pipe_crc = create_crc(data, test_data->pipe);
	if (!pipe_crc) {
		printf("auto crc not supported on this connector with pipe %i\n",
		       test_data->pipe);
		return false;
	}

	data->pipe_crc[test_data->pipe] = pipe_crc;

	/* x/y position where the cursor is still fully visible */
	test_data->left = 0;
	test_data->right = mode->hdisplay - 64;
	test_data->top = 0;
	test_data->bottom = mode->vdisplay - 64;

	/* make sure cursor is disabled */
	cursor_disable(test_data);
	igt_wait_for_vblank(data->drm_fd, test_data->pipe);

	/* get reference crc w/o cursor */
	igt_pipe_crc_collect_crc(pipe_crc, &test_data->ref_crc);

	return true;
}

static void cleanup_crtc(test_data_t *test_data, igt_output_t *output)
{
	data_t *data = test_data->data;
	igt_plane_t *primary;

	igt_pipe_crc_free(data->pipe_crc[test_data->pipe]);
	data->pipe_crc[test_data->pipe] = NULL;

	kmstest_remove_fb(data->drm_fd, &data->primary_fb);

	primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
	igt_plane_set_fb(primary, NULL);

	igt_output_set_pipe(output, PIPE_ANY);
}

static void run_test(data_t *data, enum cursor_type cursor_type, bool onscreen)
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

			if (!prepare_crtc(&test_data, output))
				continue;

			valid_tests++;

			fprintf(stdout, "Beginning %s on pipe %c, connector %s\n",
				igt_subtest_name(), pipe_name(test_data.pipe),
				igt_output_name(output));

			test_crc(&test_data, cursor_type, onscreen);


			fprintf(stdout, "\n%s on pipe %c, connector %s: PASSED\n\n",
				igt_subtest_name(), pipe_name(test_data.pipe),
				igt_output_name(output));

			/* cleanup what prepare_crtc() has done */
			cleanup_crtc(&test_data, output);
		}
	}

	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

static void create_cursor_fb(data_t *data,
			     enum cursor_type cursor_type,
			     double r, double g, double b, double a)
{
	cairo_t *cr;
	uint32_t fb_id[NUM_CURSOR_TYPES];

	fb_id[cursor_type] = kmstest_create_fb2(data->drm_fd, 64, 64,
						DRM_FORMAT_ARGB8888, false,
						&data->fb[cursor_type]);
	igt_assert(fb_id[cursor_type]);

	cr = kmstest_get_cairo_ctx(data->drm_fd,
				   &data->fb[cursor_type]);
	kmstest_paint_color_alpha(cr, 0, 0, 64, 64, r, g, b, a);
	igt_assert(cairo_status(cr) == 0);
}

igt_main
{
	data_t data = {};

	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_any();

		igt_set_vt_graphics_mode();

		igt_require_pipe_crc();

		igt_display_init(&data.display, data.drm_fd);
		data.pipe_crc = calloc(igt_display_get_n_pipes(&data.display),
				       sizeof(data.pipe_crc[0]));

		create_cursor_fb(&data, WHITE_VISIBLE, 1.0, 1.0, 1.0, 1.0);
		create_cursor_fb(&data, WHITE_INVISIBLE, 1.0, 1.0, 1.0, 0.0);
		create_cursor_fb(&data, BLACK_VISIBLE, 0.0, 0.0, 0.0, 1.0);
		create_cursor_fb(&data, BLACK_INVISIBLE, 0.0, 0.0, 0.0, 0.0);
	}

	igt_subtest("cursor-white-visible-onscreen")
		run_test(&data, WHITE_VISIBLE, true);
	igt_subtest("cursor-white-visible-offscreen")
		run_test(&data, WHITE_VISIBLE, false);
	igt_subtest("cursor-white-invisible-onscreen")
		run_test(&data, WHITE_INVISIBLE, true);
	igt_subtest("cursor-white-invisible-offscreen")
		run_test(&data, WHITE_INVISIBLE, false);
	igt_subtest("cursor-black-visible-onscreen")
		run_test(&data, BLACK_VISIBLE, true);
	igt_subtest("cursor-black-visible-offscreen")
		run_test(&data, BLACK_VISIBLE, false);
	igt_subtest("cursor-black-invisible-onscreen")
		run_test(&data, BLACK_INVISIBLE, true);
	igt_subtest("cursor-black-invisible-offscreen")
		run_test(&data, BLACK_INVISIBLE, false);

	igt_fixture {
		free(data.pipe_crc);
		igt_display_fini(&data.display);
	}
}
