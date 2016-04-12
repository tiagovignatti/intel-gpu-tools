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

#include "igt.h"
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
#include <time.h>
#include "drm.h"

#define LOCAL_I915_EXEC_NO_RELOC (1<<11)
#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

#define LOCAL_I915_EXEC_BSD_SHIFT      (13)
#define LOCAL_I915_EXEC_BSD_MASK       (3 << LOCAL_I915_EXEC_BSD_SHIFT)

#define ENGINE_FLAGS  (I915_EXEC_RING_MASK | LOCAL_I915_EXEC_BSD_MASK)

static double elapsed(const struct timespec *start, const struct timespec *end)
{
	return ((end->tv_sec - start->tv_sec) +
		(end->tv_nsec - start->tv_nsec)*1e-9);
}

static bool ignore_engine(int fd, unsigned engine)
{
	if (engine == 0)
		return true;

	if (gem_has_bsd2(fd) && engine == I915_EXEC_BSD)
		return true;

	return false;
}

static void all(int fd, int timeout)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	struct timespec start, now;
	unsigned engines[16];
	unsigned nengine;
	unsigned engine;
	unsigned long count;
	double time;

	nengine = 0;
	for_each_engine(fd, engine) {
		if (ignore_engine(fd, engine))
			continue;

		engines[nengine++] = engine;
	}
	igt_require(nengine);

	memset(&obj, 0, sizeof(obj));
	obj.handle =  gem_create(fd, 4096);
	gem_write(fd, obj.handle, 0, &bbe, sizeof(bbe));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&obj;
	execbuf.buffer_count = 1;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	if (__gem_execbuf(fd, &execbuf)) {
		execbuf.flags = 0;
		gem_execbuf(fd, &execbuf);
	}
	gem_sync(fd, obj.handle);
	gem_close(fd, obj.handle);

	count = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		for (int loop = 0; loop < 1024; loop++) {
			for (int n = 0; n < nengine; n++) {
				obj.handle =  gem_create(fd, 4096);
				gem_write(fd, obj.handle, 0, &bbe, sizeof(bbe));
				execbuf.flags &= ~ENGINE_FLAGS;
				execbuf.flags |= engines[n];
				gem_execbuf(fd, &execbuf);
				gem_close(fd, obj.handle);
			}
		}
		count += nengine * 1024;
		clock_gettime(CLOCK_MONOTONIC, &now);
	} while (elapsed(&start, &now) < timeout); /* Hang detection ~120s */
	gem_quiescent_gpu(fd);
	clock_gettime(CLOCK_MONOTONIC, &now);

	time = elapsed(&start, &now) / count;
	igt_info("All (%d engines): %'lu cycles, average %.3fus per cycle\n",
		 nengine, count, 1e6*time);
}

igt_main
{
	int device = -1;

	igt_fixture
		device = drm_open_driver(DRIVER_INTEL);

	igt_fork_hang_detector(device);

	igt_subtest("basic")
		all(device, 20);

	igt_stop_hang_detector();

	igt_fixture
		close(device);
}
