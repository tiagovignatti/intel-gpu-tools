/*
 * Copyright © 2011 Intel Corporation
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
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_io.h"

#define LOCAL_I915_EXEC_NO_RELOC (1<<11)
#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

#define LOCAL_I915_EXEC_VEBOX (4<<0)

static int dcmp(const void *A, const void *B)
{
	double a = *(double *)A, b = *(double *)B;
	if (a < b)
		return -1;
	else if (a > b)
		return 1;
	else
		return 0;
}

static double elapsed(const struct timeval *start,
		      const struct timeval *end,
		      int loop)
{
	return (1e6*(end->tv_sec - start->tv_sec) + (end->tv_usec - start->tv_usec))/loop;
}

static void loop(int fd, uint32_t handle, unsigned ring_id, const char *ring_name)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];
	int count;

	gem_require_ring(fd, ring_id);

	memset(&gem_exec, 0, sizeof(gem_exec));
	gem_exec[0].handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 1;
	execbuf.flags = ring_id;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf)) {
		execbuf.flags = ring_id;
		do_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
	}
	gem_sync(fd, handle);

	for (count = 1; count <= SLOW_QUICK(1<<17, 1<<4); count <<= 1) {
		const int reps = 9;
		double t[reps], sum;
		int n;

		for (n = 0; n < reps; n++) {
			struct timeval start, end;
			int loops = count;
			gettimeofday(&start, NULL);
			while (loops--)
				do_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
			gem_sync(fd, handle);
			gettimeofday(&end, NULL);
			t[n] = elapsed(&start, &end, count);
		}

		qsort(t, reps, sizeof(double), dcmp);
		sum = 0;
		for (n = 2; n < reps-2; n++)
			sum += t[n];
		sum /= reps - 4;

		igt_info("Time to exec x %d:		%7.3fµs (ring=%s)\n",
				count, sum, ring_name);
		fflush(stdout);
	}
}

uint32_t batch[2] = {MI_BATCH_BUFFER_END};
uint32_t handle;
int fd;

igt_main
{
	igt_fixture {
		fd = drm_open_any();

		handle = gem_create(fd, 4096);
		gem_write(fd, handle, 0, batch, sizeof(batch));
	}

	igt_subtest("render")
		loop(fd, handle, I915_EXEC_RENDER, "render");

	igt_subtest("bsd")
		loop(fd, handle, I915_EXEC_BSD, "bsd");

	igt_subtest("blt")
		loop(fd, handle, I915_EXEC_BLT, "blt");

	igt_subtest("vebox")
		loop(fd, handle, LOCAL_I915_EXEC_VEBOX, "vebox");

	igt_fixture {
		gem_close(fd, handle);

		close(fd);
	}
}
