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

	igt_fixture {
		fd = drm_open_driver(DRIVER_VC4);
	}

	igt_subtest("create-bo-4096") {
		int handle = igt_vc4_create_bo(fd, 4096);
		gem_close(fd, handle);
	}

	igt_subtest("create-bo-0") {
		struct drm_vc4_create_bo arg = {
			.size = 0,
		};
		do_ioctl_err(fd, DRM_IOCTL_VC4_CREATE_BO, &arg, EINVAL);
	}

	igt_subtest("create-bo-zeroed") {
		int fd2 = drm_open_driver(DRIVER_VC4);
		int handle;
		uint32_t *map;
		/* A size different from any used in our other tests, to try
		 * to convince it to land as the only one of its size in the
		 * kernel BO cache
		 */
		size_t size = 3 * 4096, i;

		/* Make a BO and free it on our main fd. */
		handle = igt_vc4_create_bo(fd, size);
		map = igt_vc4_mmap_bo(fd, handle, size, PROT_READ | PROT_WRITE);
		memset(map, 0xd0, size);
		munmap(map, size);
		gem_close(fd, handle);

		/* Now, allocate a BO on the other fd and make sure it doesn't
		 * have the old contents.
		 */
		handle = igt_vc4_create_bo(fd2, size);
		map = igt_vc4_mmap_bo(fd2, handle, size, PROT_READ | PROT_WRITE);
		for (i = 0; i < size / 4; i++)
			igt_assert_eq_u32(map[i], 0x0);
		munmap(map, size);
		gem_close(fd2, handle);

		close(fd2);
	}

	igt_fixture
		close(fd);
}
