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
 *   Ander Conselvan de Oliveira <ander.conselvan.de.oliveira@intel.com>
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"
#include "ioctl_wrappers.h"

typedef struct {
	int drm_fd;
	igt_display_t display;
} data_t;

/*
 * Test that a page flip from a tiled buffer to a linear one works
 * correctly. First, it sets the crtc with the linear buffer and generate
 * a reference crc for the pipe. Then, the crtc is set with the tiled one
 * and page flip to the linear one issued. A new crc is generated and
 * compared to the rerence one.
 */

static void
fill_linear_fb(struct igt_fb *fb, data_t *data, drmModeModeInfo *mode)
{
	cairo_t *cr;

	cr = igt_get_cairo_ctx(data->drm_fd, fb);
	igt_paint_test_pattern(cr, mode->hdisplay, mode->vdisplay);
	cairo_destroy(cr);
}

static void
test_flip_changes_tiling(data_t *data, igt_output_t *output)
{
	struct igt_fb linear, tiled;
	drmModeModeInfo *mode;
	igt_plane_t *primary;
	igt_pipe_crc_t *pipe_crc;
	igt_crc_t reference_crc, crc;
	int fb_id, pipe, ret, width;

	pipe = 0;
	pipe_crc = igt_pipe_crc_new(pipe, INTEL_PIPE_CRC_SOURCE_AUTO);
	igt_output_set_pipe(output, 0);

	mode = igt_output_get_mode(output);
	primary = igt_output_get_plane(output, 0);

	/* Allocate a linear buffer. Since a page flip to a buffer with
	 * different stride doesn't work, choose width so that the stride of
	 * both buffers is the same. */
	width = 512;
	while (width < mode->hdisplay)
		width *= 2;
	fb_id = igt_create_fb(data->drm_fd, width, mode->vdisplay,
			      DRM_FORMAT_XRGB8888, false, &linear);

	/* fill it with a pattern that will look wrong if tiling is wrong */
	fill_linear_fb(&linear, data, mode);

	/* set the crtc and generate a reference crc */
	igt_plane_set_fb(primary, &linear);
	igt_display_commit(&data->display);
	igt_pipe_crc_collect_crc(pipe_crc, &reference_crc);

	/* allocate a tiled buffer and set the crtc with it */
	igt_create_color_fb(data->drm_fd, width, mode->vdisplay,
			    DRM_FORMAT_XRGB8888, true, 0.0, 0.0, 0.0, &tiled);
	igt_plane_set_fb(primary, &tiled);
	igt_display_commit(&data->display);

	/* flip to the linear buffer */
	ret = drmModePageFlip(data->drm_fd, output->config.crtc->crtc_id,
			      fb_id, 0, NULL);
	igt_assert(ret == 0);

	igt_wait_for_vblank(data->drm_fd, pipe);

	/* get a crc and compare with the reference */
	igt_pipe_crc_collect_crc(pipe_crc, &crc);
	igt_assert(igt_crc_equal(&reference_crc, &crc));

	/* clean up */
	igt_plane_set_fb(primary, NULL);
	igt_pipe_crc_free(pipe_crc);
	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit(&data->display);

	igt_remove_fb(data->drm_fd, &tiled);
	igt_remove_fb(data->drm_fd, &linear);
}

static data_t data;
igt_output_t *output;

igt_main
{
	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_any();

		igt_set_vt_graphics_mode();

		igt_require_pipe_crc();
		igt_display_init(&data.display, data.drm_fd);
	}

	igt_subtest_f("flip-changes-tiling") {
		for_each_connected_output(&data.display, output)
			test_flip_changes_tiling(&data, output);
	}

	igt_fixture {
		igt_display_fini(&data.display);
	}
}
