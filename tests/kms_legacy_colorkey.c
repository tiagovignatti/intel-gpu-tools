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
 */

#include "igt.h"
#include <errno.h>


IGT_TEST_DESCRIPTION("Check that the legacy set colorkey ioctl only works on sprite planes.");

static int drm_fd;
static igt_display_t display;
static int p;
static igt_plane_t *plane;
static uint32_t max_id;

static void test_plane(uint32_t plane_id, int expected_ret)
{
	struct drm_intel_sprite_colorkey ckey = {
		.plane_id = plane_id,
	};

	igt_assert(drmCommandWrite(drm_fd, DRM_I915_SET_SPRITE_COLORKEY, &ckey,
				   sizeof(ckey)) == expected_ret);
}

igt_simple_main
{
	igt_skip_on_simulation();

	drm_fd = drm_open_driver_master(DRIVER_INTEL);

	kmstest_set_vt_graphics_mode();

	igt_display_init(&display, drm_fd);

	for_each_pipe(&display, p) {
		for_each_plane_on_pipe(&display, p, plane) {
			test_plane(plane->drm_plane->plane_id,
				   (plane->is_cursor || plane->is_primary) ? -ENOENT : 0);

			max_id = max(max_id, plane->drm_plane->plane_id);
		}
	}

	/* try some invalid IDs too */
	test_plane(0, -ENOENT);
	test_plane(max_id + 1, -ENOENT);

	igt_display_fini(&display);
}
