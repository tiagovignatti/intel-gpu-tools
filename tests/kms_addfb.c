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
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "drm_fourcc.h"

uint32_t gem_bo;
uint32_t gem_bo_small;

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
		gem_bo = gem_create(fd, 1024*1024*4);
		igt_assert(gem_bo);
	}

	igt_subtest("no-handle") {
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == -1 &&
			   errno == EINVAL);
	}

	f.handles[0] = gem_bo;
	igt_subtest("normal") {
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

	igt_fixture
		gem_set_tiling(fd, gem_bo, I915_TILING_X, 1024*4);
	f.pitches[0] = 1024*4;

	igt_subtest("X-tiled") {
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == 0);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == 0);
		f.fb_id = 0;
	}

	igt_subtest("framebuffer-vs-set-tiling") {
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == 0);
		igt_assert(__gem_set_tiling(fd, gem_bo, I915_TILING_X, 512*4) == -EBUSY);
		igt_assert(__gem_set_tiling(fd, gem_bo, I915_TILING_X, 1024*4) == -EBUSY);
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == 0);
		f.fb_id = 0;
	}

	f.pitches[0] = 512*4;
	igt_subtest("tile-pitch-mismatch") {
		igt_assert(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f) == -1 &&
			   errno == EINVAL);
	}

	igt_fixture
		gem_set_tiling(fd, gem_bo, I915_TILING_Y, 1024*4);
	f.pitches[0] = 1024*4;
	igt_subtest("Y-tiled") {
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
		gem_bo = gem_create(fd, 1024*1024*4);
		igt_assert(gem_bo);
		gem_bo_small = gem_create(fd, 1024*1024*4 - 4096);
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

int fd;

igt_main
{
	igt_fixture
		fd = drm_open_any();

	pitch_tests(fd);

	size_tests(fd);

	igt_fixture
		close(fd);
}
