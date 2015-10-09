/*
 * Copyright © 2012 Intel Corporation
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

IGT_TEST_DESCRIPTION("Checks whether the kernel handles mmap offset exhaustion"
		     " correctly.");

#define OBJECT_SIZE (1024*1024)

/* Testcase: checks whether the kernel handles mmap offset exhaustion correctly
 *
 * Currently the kernel doesn't reap the mmap offset of purged objects, albeit
 * there's nothing that prevents it ABI-wise and it helps to get out of corners
 * (because drm_mm is only 32bit on 32bit archs unfortunately.
 *
 * Note that on 64bit machines we have plenty of address space (because drm_mm
 * uses unsigned long).
 */

static void
create_and_map_bo(int fd)
{
	uint32_t handle;
	char *ptr;

	handle = gem_create(fd, OBJECT_SIZE);

	ptr = gem_mmap__gtt(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);

	/* touch it to force it into the gtt */
	*ptr = 0;

	/* but then unmap it again because we only have limited address space on
	 * 32 bit */
	munmap(ptr, OBJECT_SIZE);

	/* we happily leak objects to exhaust mmap offset space, the kernel will
	 * reap backing storage. */
	gem_madvise(fd, handle, I915_MADV_DONTNEED);
}

igt_simple_main
{
	int fd, i;

	igt_skip_on_simulation();

	fd = drm_open_driver(DRIVER_INTEL);

	/* we have 32bit of address space, so try to fit one MB more
	 * than that. */
	for (i = 0; i < 4096 + 1; i++)
		create_and_map_bo(fd);

	close(fd);
}
