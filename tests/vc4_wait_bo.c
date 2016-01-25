/*
 * Copyright © 2016 Broadcom
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
#include "igt_vc4.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "vc4_drm.h"

igt_main
{
	int fd;
	int bo_handle;

	igt_fixture {
		fd = drm_open_driver(DRIVER_VC4);
		bo_handle = igt_vc4_create_bo(fd, 4096);
	}

	igt_subtest("bad-bo") {
		struct drm_vc4_wait_bo arg = {
			.handle = bo_handle + 1,
			.timeout_ns = 0,
		};
		do_ioctl_err(fd, DRM_IOCTL_VC4_WAIT_BO, &arg, EINVAL);
	}

	igt_subtest("bad-pad") {
		struct drm_vc4_wait_bo arg = {
			.pad = 1,
			.handle = bo_handle,
			.timeout_ns = 0,
		};
		do_ioctl_err(fd, DRM_IOCTL_VC4_WAIT_BO, &arg, EINVAL);
	}

	igt_subtest("unused-bo-0ns") {
		struct drm_vc4_wait_bo arg = {
			.handle = bo_handle,
			.timeout_ns = 0,
		};
		do_ioctl(fd, DRM_IOCTL_VC4_WAIT_BO, &arg);
	}

	igt_subtest("unused-bo-1ns") {
		struct drm_vc4_wait_bo arg = {
			.handle = bo_handle,
			.timeout_ns = 1,
		};
		do_ioctl(fd, DRM_IOCTL_VC4_WAIT_BO, &arg);
	}

	igt_fixture
		close(fd);
}
