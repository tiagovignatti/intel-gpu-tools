/*
 * Copyright © 2015 Intel Corporation
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

#include "igt.h"

IGT_TEST_DESCRIPTION(
"Exercise the FDI lane bifurcation code for IVB in the kernel by setting"
"different combinations of modes for pipes B and C.");

typedef struct {
	int drm_fd;
	igt_display_t display;
} data_t;

drmModeModeInfo mode_3_lanes = {
	.clock = 173000,
	.hdisplay = 1920,
	.hsync_start = 2048,
	.hsync_end = 2248,
	.htotal = 2576,
	.vdisplay = 1080,
	.vsync_start = 1083,
	.vsync_end = 1088,
	.vtotal = 1120,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC,
	.name = "3_lanes",
};

drmModeModeInfo mode_2_lanes = {
	.clock = 138500,
	.hdisplay = 1920,
	.hsync_start = 1968,
	.hsync_end = 2000,
	.htotal = 2080,
	.vdisplay = 1080,
	.vsync_start = 1083,
	.vsync_end = 1088,
	.vtotal = 1111,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC,
	.name = "2_lanes",
};

static int
disable_pipe(data_t *data, enum pipe pipe, igt_output_t *output)
{
	igt_plane_t *primary;

	igt_output_set_pipe(output, pipe);
	primary = igt_output_get_plane(output, 0);
	igt_plane_set_fb(primary, NULL);
	return igt_display_commit(&data->display);
}

static int
set_mode_on_pipe(data_t *data, enum pipe pipe, igt_output_t *output)
{
	igt_plane_t *primary;
	drmModeModeInfo *mode;
	struct igt_fb fb;
	int fb_id;

	igt_output_set_pipe(output, pipe);

	mode = igt_output_get_mode(output);

	primary = igt_output_get_plane(output, 0);

	fb_id = igt_create_color_fb(data->drm_fd,
				    mode->hdisplay, mode->vdisplay,
				    DRM_FORMAT_XRGB8888, I915_TILING_NONE,
				    1.0, 1.0, 1.0, &fb);
	igt_assert(fb_id >= 0);

	igt_plane_set_fb(primary, &fb);
	return igt_display_try_commit2(&data->display, COMMIT_LEGACY);
}

static int
set_big_mode_on_pipe(data_t *data, enum pipe pipe, igt_output_t *output)
{
	igt_output_override_mode(output, &mode_3_lanes);
	return set_mode_on_pipe(data, pipe, output);
}

static int
set_normal_mode_on_pipe(data_t *data, enum pipe pipe, igt_output_t *output)
{
	igt_output_override_mode(output, &mode_2_lanes);
	return set_mode_on_pipe(data, pipe, output);
}

static void
find_outputs(data_t *data, igt_output_t **output1, igt_output_t **output2)
{
	int count = 0;
	igt_output_t *output;

	*output1 = NULL;
	*output2 = NULL;

	for_each_connected_output(&data->display, output) {
		if (!(*output1))
			*output1 = output;
		else if (!(*output2))
			*output2 = output;

		igt_output_set_pipe(output, PIPE_ANY);
		count++;
	}

	igt_skip_on_f(count < 2, "Not enough connected outputs\n");
}

static void
test_dpms(data_t *data)
{
	igt_output_t *output1, *output2;
	int ret;

	find_outputs(data, &output1, &output2);

	igt_info("Pipe %s will use connector %s\n",
		 kmstest_pipe_name(PIPE_B), igt_output_name(output1));
	igt_info("Pipe %s will use connector %s\n",
		 kmstest_pipe_name(PIPE_C), igt_output_name(output2));

	ret = set_big_mode_on_pipe(data, PIPE_B, output1);
	igt_assert(ret == 0);

	kmstest_set_connector_dpms(data->drm_fd, output1->config.connector, DRM_MODE_DPMS_OFF);

	ret = set_big_mode_on_pipe(data, PIPE_C, output2);
	igt_assert(ret != 0);
}

static void
test_lane_reduction(data_t *data)
{
	igt_output_t *output1, *output2;
	int ret;

	find_outputs(data, &output1, &output2);

	igt_info("Pipe %s will use connector %s\n",
		 kmstest_pipe_name(PIPE_B), igt_output_name(output1));
	igt_info("Pipe %s will use connector %s\n",
		 kmstest_pipe_name(PIPE_C), igt_output_name(output2));

	ret = set_big_mode_on_pipe(data, PIPE_B, output1);
	igt_assert(ret == 0);

	ret = set_normal_mode_on_pipe(data, PIPE_B, output1);
	igt_assert(ret == 0);

	ret = set_normal_mode_on_pipe(data, PIPE_C, output2);
	igt_assert(ret == 0);
}

static void
test_disable_pipe_B(data_t *data)
{
	igt_output_t *output1, *output2;
	int ret;

	find_outputs(data, &output1, &output2);

	igt_info("Pipe %s will use connector %s\n",
		 kmstest_pipe_name(PIPE_B), igt_output_name(output1));
	igt_info("Pipe %s will use connector %s\n",
		 kmstest_pipe_name(PIPE_C), igt_output_name(output2));

	ret = set_big_mode_on_pipe(data, PIPE_B, output1);
	igt_assert(ret == 0);

	ret = disable_pipe(data, PIPE_B, output1);
	igt_assert(ret == 0);

	ret = set_normal_mode_on_pipe(data, PIPE_C, output2);
	igt_assert(ret == 0);

	ret = set_normal_mode_on_pipe(data, PIPE_B, output1);
	igt_assert(ret == 0);
}

static void
test_from_C_to_B_with_3_lanes(data_t *data)
{
	igt_output_t *output1, *output2;
	int ret;

	find_outputs(data, &output1, &output2);

	igt_info("Pipe %s will use connector %s\n",
		 kmstest_pipe_name(PIPE_B), igt_output_name(output1));
	igt_info("Pipe %s will use connector %s\n",
		 kmstest_pipe_name(PIPE_C), igt_output_name(output2));

	ret = set_normal_mode_on_pipe(data, PIPE_C, output2);
	igt_assert(ret == 0);

	ret = disable_pipe(data, PIPE_C, output2);
	igt_assert(ret == 0);

	ret = set_big_mode_on_pipe(data, PIPE_B, output1);
	igt_assert(ret == 0);
}

static void
test_fail_enable_pipe_C_while_B_has_3_lanes(data_t *data)
{
	igt_output_t *output1, *output2;
	int ret;

	find_outputs(data, &output1, &output2);

	igt_info("Pipe %s will use connector %s\n",
		 kmstest_pipe_name(PIPE_B), igt_output_name(output1));
	igt_info("Pipe %s will use connector %s\n",
		 kmstest_pipe_name(PIPE_C), igt_output_name(output2));

	ret = set_big_mode_on_pipe(data, PIPE_B, output1);
	igt_assert(ret == 0);

	ret = set_normal_mode_on_pipe(data, PIPE_C, output2);
	igt_assert(ret != 0);
}

static data_t data;
igt_main
{
	int devid;

	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_driver_master(DRIVER_INTEL);
		devid = intel_get_drm_devid(data.drm_fd);
		igt_skip_on(!IS_IVYBRIDGE(devid));

		kmstest_set_vt_graphics_mode();
		igt_display_init(&data.display, data.drm_fd);
	}

	igt_subtest("pipe-B-dpms-off-modeset-pipe-C")
		test_dpms(&data);

	igt_subtest("pipe-B-double-modeset-then-modeset-pipe-C")
		test_lane_reduction(&data);

	igt_subtest("disable-pipe-B-enable-pipe-C")
		test_disable_pipe_B(&data);

	igt_subtest("from-pipe-C-to-B-with-3-lanes")
		test_from_C_to_B_with_3_lanes(&data);

	igt_subtest("enable-pipe-C-while-B-has-3-lanes")
		test_fail_enable_pipe_C_while_B_has_3_lanes(&data);

	igt_fixture {
		igt_display_fini(&data.display);
	}
}
