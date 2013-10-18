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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "drm_fourcc.h"

#include "drmtest.h"
#include "igt_debugfs.h"

enum cursor_type {
	WHITE_VISIBLE,
	WHITE_INVISIBLE,
	BLACK_VISIBLE,
	BLACK_INVISIBLE,
	NUM_CURSOR_TYPES,
};

typedef struct {
	struct kmstest_connector_config config;
	drmModeModeInfo mode;
	struct kmstest_fb fb;
} connector_t;

typedef struct {
	int drm_fd;
	igt_debugfs_t debugfs;
	drmModeRes *resources;
	FILE *ctl;
	uint32_t fb_id[NUM_CURSOR_TYPES];
	struct kmstest_fb fb[NUM_CURSOR_TYPES];
	igt_pipe_crc_t **pipe_crc;
} data_t;

typedef struct {
	data_t *data;
	uint32_t crtc_id;
	int crtc_idx;
	igt_crc_t ref_crc;
	bool crc_must_match;
	int left, right, top, bottom;
} test_data_t;


static bool
connector_set_mode(data_t *data, connector_t *connector, drmModeModeInfo *mode)
{
	struct kmstest_connector_config *config = &connector->config;
	unsigned int fb_id;
	cairo_t *cr;
	int ret;

	fb_id = kmstest_create_fb2(data->drm_fd,
				   mode->hdisplay, mode->vdisplay,
				   DRM_FORMAT_XRGB8888,
				   false, &connector->fb);
	igt_assert(fb_id);

	/* black */
	cr = kmstest_get_cairo_ctx(data->drm_fd, &connector->fb);
	kmstest_paint_color(cr, 0, 0, mode->hdisplay, mode->vdisplay,
			    0.0, 0.0, 0.0);
	igt_assert(cairo_status(cr) == 0);

#if 0
	fprintf(stdout, "Using pipe %c, %dx%d\n", pipe_name(config->pipe),
		mode->hdisplay, mode->vdisplay);
#endif

	ret = drmModeSetCrtc(data->drm_fd,
			     config->crtc->crtc_id,
			     connector->fb.fb_id,
			     0, 0, /* x, y */
			     &config->connector->connector_id,
			     1,
			     mode);
	igt_assert(ret == 0);

	return 0;
}

static igt_pipe_crc_t *create_crc(data_t *data, int crtc_idx)
{
	igt_pipe_crc_t *crc;

	crc = igt_pipe_crc_new(&data->debugfs, data->drm_fd, crtc_idx,
			       INTEL_PIPE_CRC_SOURCE_PF);
	if (crc)
		return crc;

	crc = igt_pipe_crc_new(&data->debugfs, data->drm_fd, crtc_idx,
			       INTEL_PIPE_CRC_SOURCE_PIPE);
	if (crc)
		return crc;

	return NULL;
}

static void display_init(data_t *data)
{
	int i;

	data->resources = drmModeGetResources(data->drm_fd);
	igt_assert(data->resources);

	data->pipe_crc = calloc(data->resources->count_crtcs, sizeof(data->pipe_crc[0]));
	for (i = 0; i < data->resources->count_crtcs; i++) {
		data->pipe_crc[i] = create_crc(data, i);
		igt_require_f(data->pipe_crc[i],
			      "pipe/pf crc not supported\n");
	}
}

static void display_fini(data_t *data)
{
	int i;

	for (i = 0; i < data->resources->count_crtcs; i++)
		igt_pipe_crc_free(data->pipe_crc[i]);
	free(data->pipe_crc);

	drmModeFreeResources(data->resources);
}

static void do_single_test(test_data_t *test_data, int x, int y)
{
	data_t *data = test_data->data;
	igt_pipe_crc_t *pipe_crc = data->pipe_crc[test_data->crtc_idx];
	igt_crc_t *crcs = NULL;

	printf("."); fflush(stdout);

	igt_assert(drmModeMoveCursor(data->drm_fd, test_data->crtc_id, x, y) == 0);
	igt_wait_for_vblank(data->drm_fd, test_data->crtc_idx);

	igt_pipe_crc_start(pipe_crc);
	igt_pipe_crc_get_crcs(pipe_crc, 1, &crcs);
	igt_pipe_crc_stop(pipe_crc);
	if (test_data->crc_must_match)
		igt_assert(igt_crc_equal(&crcs[0], &test_data->ref_crc));
	else
		igt_assert(!igt_crc_equal(&crcs[0], &test_data->ref_crc));
	free(crcs);
}

static void do_test(test_data_t *test_data,
		    int left, int right, int top, int bottom)
{
	do_single_test(test_data, left, top);
	do_single_test(test_data, right, top);
	do_single_test(test_data, right, bottom);
	do_single_test(test_data, left, bottom);
}

static void test_crc(test_data_t *test_data, enum cursor_type cursor_type,
		     bool onscreen)
{
	data_t *data = test_data->data;
	int left = test_data->left;
	int right = test_data->right;
	int top = test_data->top;
	int bottom = test_data->bottom;

	/* enable cursor */
	igt_assert(drmModeSetCursor(data->drm_fd, test_data->crtc_id,
				    data->fb[cursor_type].gem_handle, 64, 64) == 0);

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

	/* disable cursor again */
	igt_assert(drmModeSetCursor(data->drm_fd, test_data->crtc_id, 0, 0, 0) == 0);
}

static bool prepare_crtc(test_data_t *test_data, uint32_t connector_id)
{
	connector_t connector;
	igt_crc_t *crcs = NULL;
	data_t *data = test_data->data;
	igt_pipe_crc_t *pipe_crc = data->pipe_crc[test_data->crtc_idx];
	int ret;

	ret = kmstest_get_connector_config(data->drm_fd,
					   connector_id,
					   1 << test_data->crtc_idx,
					   &connector.config);
	if (ret)
		return false;

	connector_set_mode(data, &connector, &connector.config.default_mode);

	/* x/y position where the cursor is still fully visible */
	test_data->left = 0;
	test_data->right = connector.config.default_mode.hdisplay - 64;
	test_data->top = 0;
	test_data->bottom = connector.config.default_mode.vdisplay - 64;

	/* make sure cursor is disabled */
	igt_assert(drmModeSetCursor(data->drm_fd, test_data->crtc_id, 0, 0, 0) == 0);
	igt_wait_for_vblank(data->drm_fd, test_data->crtc_idx);

	/* get reference crc w/o cursor */
	igt_pipe_crc_start(pipe_crc);
	igt_pipe_crc_get_crcs(pipe_crc, 1, &crcs);
	test_data->ref_crc = crcs[0];
	igt_pipe_crc_stop(pipe_crc);
	free(crcs);

	kmstest_free_connector_config(&connector.config);

	return true;
}

static void run_test(data_t *data, enum cursor_type cursor_type, bool onscreen)
{
	test_data_t test_data = {
		.data = data,
	};
	int i, n;

	for (i = 0; i < data->resources->count_connectors; i++) {
		uint32_t connector_id = data->resources->connectors[i];

		for (n = 0; n < data->resources->count_crtcs; n++) {
			test_data.crtc_idx = n;
			test_data.crtc_id = data->resources->crtcs[n];

			if (!prepare_crtc(&test_data, connector_id))
				continue;

			fprintf(stdout, "Beginning %s on crtc %d, connector %d\n",
				igt_subtest_name(), test_data.crtc_id, connector_id);

			test_crc(&test_data, cursor_type, onscreen);


			fprintf(stdout, "\n%s on crtc %d, connector %d: PASSED\n\n",
				igt_subtest_name(), test_data.crtc_id, connector_id);
		}
	}
}

static void exit_handler(int sig)
{
	igt_pipe_crc_reset();
}

static void create_cursor_fb(data_t *data,
			     enum cursor_type cursor_type,
			     double r, double g, double b, double a)
{
	cairo_t *cr;

	data->fb_id[cursor_type] = kmstest_create_fb2(data->drm_fd, 64, 64,
						      DRM_FORMAT_ARGB8888, false,
						      &data->fb[cursor_type]);
	igt_assert(data->fb_id[cursor_type]);

	cr = kmstest_get_cairo_ctx(data->drm_fd,
				   &data->fb[cursor_type]);
	kmstest_paint_color_alpha(cr, 0, 0, 64, 64, r, g, b, a);
	igt_assert(cairo_status(cr) == 0);
}

int main(int argc, char **argv)
{
	data_t data = {};

	igt_subtest_init(argc, argv);
	igt_skip_on_simulation();

	igt_fixture {
		size_t written;
		int ret;
		const char *cmd = "pipe A none";

		data.drm_fd = drm_open_any();
		do_or_die(igt_set_vt_graphics_mode());
		do_or_die(igt_install_exit_handler(exit_handler));

		igt_debugfs_init(&data.debugfs);
		data.ctl = igt_debugfs_fopen(&data.debugfs,
					     "i915_display_crc_ctl", "r+");
		igt_require_f(data.ctl,
			      "No display_crc_ctl found, kernel too old\n");
		written = fwrite(cmd, 1, strlen(cmd), data.ctl);
		ret = fflush(data.ctl);
		igt_require_f((written == strlen(cmd) && ret == 0) || errno != ENODEV,
			      "CRCs not supported on this platform\n");

		display_init(&data);

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
		igt_pipe_crc_reset();
		display_fini(&data);
		fclose(data.ctl);
	}

	return 0;
}
