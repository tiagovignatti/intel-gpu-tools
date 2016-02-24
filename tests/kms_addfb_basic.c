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
 * Authors:
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "drm.h"
#include "drm_fourcc.h"

uint32_t gem_bo;
uint32_t gem_bo_small;

static void invalid_tests(int fd)
{
	struct local_drm_mode_fb_cmd2 f = {};

	f.width = 512;
	f.height = 512;
	f.pixel_format = DRM_FORMAT_XRGB8888;
	f.pitches[0] = 512*4;

	igt_fixture {
		gem_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
			DRM_FORMAT_XRGB8888, 0, 0, NULL, NULL, NULL);
		igt_assert(gem_bo);
		gem_bo_small = igt_create_bo_with_dimensions(fd, 1024, 1023,
			DRM_FORMAT_XRGB8888, 0, 0, NULL, NULL, NULL);
		igt_assert(gem_bo_small);

		f.handles[0] = gem_bo;

		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == 0);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == 0);
		f.fb_id = 0;
	}

	f.flags = LOCAL_DRM_MODE_FB_MODIFIERS;

	igt_subtest("unused-handle") {
		igt_require_fb_modifiers(fd);

		f.handles[1] = gem_bo_small;
		igt_assert(drmIoctl(fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f) == -1 &&
			   errno == EINVAL);
		f.handles[1] = 0;
	}

	igt_subtest("unused-pitches") {
		igt_require_fb_modifiers(fd);

		f.pitches[1] = 512;
		igt_assert(drmIoctl(fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f) == -1 &&
			   errno == EINVAL);
		f.pitches[1] = 0;
	}

	igt_subtest("unused-offsets") {
		igt_require_fb_modifiers(fd);

		f.offsets[1] = 512;
		igt_assert(drmIoctl(fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f) == -1 &&
			   errno == EINVAL);
		f.offsets[1] = 0;
	}

	igt_subtest("unused-modifier") {
		igt_require_fb_modifiers(fd);

		f.modifier[1] =  LOCAL_I915_FORMAT_MOD_X_TILED;
		igt_assert(drmIoctl(fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f) == -1 &&
			   errno == EINVAL);
		f.modifier[1] = 0;
	}

	igt_subtest("clobberred-modifier") {
		f.flags = 0;
		f.modifier[0] = 0;
		gem_set_tiling(fd, gem_bo, I915_TILING_X, 512*4);
		igt_assert(drmIoctl(fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f) == 0);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == 0);
		f.fb_id = 0;
		igt_assert(f.modifier[0] == 0);
	}

	igt_fixture {
		gem_close(fd, gem_bo);
		gem_close(fd, gem_bo_small);
	}
}

static void pitch_tests(int fd)
{
	struct drm_mode_fb_cmd2 f = {};
	int bad_pitches[] = { 0, 32, 63, 128, 256, 256*4, 999, 64*1024 };
	int i;

	f.width = 512;
	f.height = 512;
	f.pixel_format = DRM_FORMAT_XRGB8888;
	f.pitches[0] = 1024*4;

	igt_fixture {
		gem_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
			DRM_FORMAT_XRGB8888, 0, 0, NULL, NULL, NULL);
		igt_assert(gem_bo);
	}

	igt_subtest("no-handle") {
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == -1 &&
			   errno == EINVAL);
	}

	f.handles[0] = gem_bo;
	igt_subtest("basic") {
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == 0);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == 0);
		f.fb_id = 0;
	}

	for (i = 0; i < ARRAY_SIZE(bad_pitches); i++) {
		igt_subtest_f("bad-pitch-%i", bad_pitches[i]) {
			f.pitches[0] = bad_pitches[i];
			igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == -1 &&
				   errno == EINVAL);
		}
	}

	f.pitches[0] = 1024*4;

	igt_subtest("basic-X-tiled") {
		gem_set_tiling(fd, gem_bo, I915_TILING_X, 1024*4);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == 0);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == 0);
		f.fb_id = 0;
	}

	igt_subtest("framebuffer-vs-set-tiling") {
		gem_set_tiling(fd, gem_bo, I915_TILING_X, 1024*4);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == 0);
		igt_assert(__gem_set_tiling(fd, gem_bo, I915_TILING_X, 512*4) == -EBUSY);
		igt_assert(__gem_set_tiling(fd, gem_bo, I915_TILING_X, 1024*4) == -EBUSY);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == 0);
		f.fb_id = 0;
	}

	f.pitches[0] = 512*4;
	igt_subtest("tile-pitch-mismatch") {
		gem_set_tiling(fd, gem_bo, I915_TILING_X, 1024*4);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == -1 &&
			   errno == EINVAL);
	}

	f.pitches[0] = 1024*4;
	igt_subtest("basic-Y-tiled") {
		gem_set_tiling(fd, gem_bo, I915_TILING_Y, 1024*4);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == -1 &&
			   errno == EINVAL);
	}

	igt_fixture
		gem_close(fd, gem_bo);
}

static void size_tests(int fd)
{
	struct drm_mode_fb_cmd2 f = {};
	struct drm_mode_fb_cmd2 f_16 = {};
	struct drm_mode_fb_cmd2 f_8 = {};

	f.width = 1024;
	f.height = 1024;
	f.pixel_format = DRM_FORMAT_XRGB8888;
	f.pitches[0] = 1024*4;

	f_16.width = 1024;
	f_16.height = 1024*2;
	f_16.pixel_format = DRM_FORMAT_RGB565;
	f_16.pitches[0] = 1024*2;

	f_8.width = 1024*2;
	f_8.height = 1024*2;
	f_8.pixel_format = DRM_FORMAT_C8;
	f_8.pitches[0] = 1024*2;

	igt_fixture {
		gem_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
			DRM_FORMAT_XRGB8888, 0, 0, NULL, NULL, NULL);
		igt_assert(gem_bo);
		gem_bo_small = igt_create_bo_with_dimensions(fd, 1024, 1023,
			DRM_FORMAT_XRGB8888, 0, 0, NULL, NULL, NULL);
		igt_assert(gem_bo_small);
	}

	f.handles[0] = gem_bo;
	f_16.handles[0] = gem_bo;
	f_8.handles[0] = gem_bo;

	igt_subtest("size-max") {
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == 0);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == 0);
		f.fb_id = 0;
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f_16) == 0);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f_16.fb_id) == 0);
		f.fb_id = 0;
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f_8) == 0);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f_8.fb_id) == 0);
		f.fb_id = 0;
	}

	f.width++;
	f_16.width++;
	f_8.width++;
	igt_subtest("too-wide") {
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == -1 &&
			   errno == EINVAL);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f_16) == -1 &&
			   errno == EINVAL);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f_8) == -1 &&
			   errno == EINVAL);
	}
	f.width--;
	f_16.width--;
	f_8.width--;
	f.height++;
	f_16.height++;
	f_8.height++;
	igt_subtest("too-high") {
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == -1 &&
			   errno == EINVAL);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f_16) == -1 &&
			   errno == EINVAL);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f_8) == -1 &&
			   errno == EINVAL);
	}

	f.handles[0] = gem_bo_small;
	igt_subtest("bo-too-small") {
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == -1 &&
			   errno == EINVAL);
	}

	/* Just to check that the parameters would work. */
	f.height = 1020;
	igt_subtest("small-bo") {
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == 0);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == 0);
		f.fb_id = 0;
	}

	igt_fixture
		gem_set_tiling(fd, gem_bo_small, I915_TILING_X, 1024*4);

	igt_subtest("bo-too-small-due-to-tiling") {
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == -1 &&
			   errno == EINVAL);
	}


	igt_fixture {
		gem_close(fd, gem_bo);
		gem_close(fd, gem_bo_small);
	}
}

static void addfb25_tests(int fd)
{
	struct local_drm_mode_fb_cmd2 f = {};

	igt_fixture {
		gem_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
			DRM_FORMAT_XRGB8888, 0, 0, NULL, NULL, NULL);
		igt_assert(gem_bo);

		memset(&f, 0, sizeof(f));

		f.width = 1024;
		f.height = 1024;
		f.pixel_format = DRM_FORMAT_XRGB8888;
		f.pitches[0] = 1024*4;
		f.modifier[0] = LOCAL_DRM_FORMAT_MOD_NONE;

		f.handles[0] = gem_bo;
	}

	igt_subtest("addfb25-modifier-no-flag") {
		igt_require_fb_modifiers(fd);

		f.modifier[0] = LOCAL_I915_FORMAT_MOD_X_TILED;
		igt_assert(drmIoctl(fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f) < 0 && errno == EINVAL);
	}

	igt_fixture
		f.flags = LOCAL_DRM_MODE_FB_MODIFIERS;

	igt_subtest("addfb25-bad-modifier") {
		igt_require_fb_modifiers(fd);

		f.modifier[0] = ~0;
		igt_assert(drmIoctl(fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f) < 0 && errno == EINVAL);
	}

	igt_fixture
		gem_set_tiling(fd, gem_bo, I915_TILING_X, 1024*4);

	igt_subtest("addfb25-X-tiled-mismatch") {
		igt_require_fb_modifiers(fd);

		f.modifier[0] = LOCAL_DRM_FORMAT_MOD_NONE;
		igt_assert(drmIoctl(fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f) < 0 && errno == EINVAL);
	}

	igt_subtest("addfb25-X-tiled") {
		igt_require_fb_modifiers(fd);

		f.modifier[0] = LOCAL_I915_FORMAT_MOD_X_TILED;
		igt_assert(drmIoctl(fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f) == 0);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == 0);
		f.fb_id = 0;
	}

	igt_subtest("addfb25-framebuffer-vs-set-tiling") {
		igt_require_fb_modifiers(fd);

		f.modifier[0] = LOCAL_I915_FORMAT_MOD_X_TILED;
		igt_assert(drmIoctl(fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f) == 0);
		igt_assert(__gem_set_tiling(fd, gem_bo, I915_TILING_X, 512*4) == -EBUSY);
		igt_assert(__gem_set_tiling(fd, gem_bo, I915_TILING_X, 1024*4) == -EBUSY);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == 0);
		f.fb_id = 0;
	}

	igt_fixture
		gem_close(fd, gem_bo);
}

static void addfb25_ytile(int fd, int gen)
{
	struct local_drm_mode_fb_cmd2 f = {};
	int shouldret;

	igt_fixture {
		gem_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
			DRM_FORMAT_XRGB8888, 0, 0, NULL, NULL, NULL);
		igt_assert(gem_bo);
		gem_bo_small = igt_create_bo_with_dimensions(fd, 1024, 1023,
			DRM_FORMAT_XRGB8888, 0, 0, NULL, NULL, NULL);
		igt_assert(gem_bo_small);

		shouldret = gen >= 9 ? 0 : -1;

		memset(&f, 0, sizeof(f));

		f.width = 1024;
		f.height = 1024;
		f.pixel_format = DRM_FORMAT_XRGB8888;
		f.pitches[0] = 1024*4;
		f.flags = LOCAL_DRM_MODE_FB_MODIFIERS;
		f.modifier[0] = LOCAL_DRM_FORMAT_MOD_NONE;

		f.handles[0] = gem_bo;
	}

	igt_subtest("addfb25-Y-tiled") {
		igt_require_fb_modifiers(fd);

		f.modifier[0] = LOCAL_I915_FORMAT_MOD_Y_TILED;
		igt_assert(drmIoctl(fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f) == shouldret);
		if (!shouldret)
			igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == 0);
		f.fb_id = 0;
	}

	igt_subtest("addfb25-Yf-tiled") {
		igt_require_fb_modifiers(fd);

		f.modifier[0] = LOCAL_I915_FORMAT_MOD_Yf_TILED;
		igt_assert(drmIoctl(fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f) == shouldret);
		if (!shouldret)
			igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == 0);
		f.fb_id = 0;
	}

	igt_subtest("addfb25-Y-tiled-small") {
		igt_require_fb_modifiers(fd);
		igt_require(gen >= 9);

		f.modifier[0] = LOCAL_I915_FORMAT_MOD_Y_TILED;
		f.height = 1023;
		f.handles[0] = gem_bo_small;
		igt_assert(drmIoctl(fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f) < 0 && errno == EINVAL);
		f.fb_id = 0;
	}

	igt_fixture {
		gem_close(fd, gem_bo);
		gem_close(fd, gem_bo_small);
	}
}

int fd;
int gen;

igt_main
{
	igt_fixture {
		fd = drm_open_driver_master(DRIVER_INTEL);
		gen = intel_gen(intel_get_drm_devid(fd));
	}

	invalid_tests(fd);

	pitch_tests(fd);

	size_tests(fd);

	addfb25_tests(fd);

	addfb25_ytile(fd, gen);

	igt_fixture
		close(fd);
}
