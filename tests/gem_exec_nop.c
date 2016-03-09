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

static void single(int fd, uint32_t handle, unsigned ring_id, const char *ring_name)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	struct timespec start, now;
	unsigned int count = 0;

	gem_require_ring(fd, ring_id);

	memset(&obj, 0, sizeof(obj));
	obj.handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&obj;
	execbuf.buffer_count = 1;
	execbuf.flags = ring_id;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	if (__gem_execbuf(fd, &execbuf) == 0) {
		execbuf.flags = ring_id;
		gem_execbuf(fd, &execbuf);
	}
	gem_sync(fd, handle);

	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		for (int loop = 0; loop < 1024; loop++) {
			gem_execbuf(fd, &execbuf);
			count++;
		}
		clock_gettime(CLOCK_MONOTONIC, &now);
	} while (elapsed(&start, &now) < 20.);
	gem_sync(fd, handle);
	clock_gettime(CLOCK_MONOTONIC, &now);

	igt_info("%s: %'u cycles: %.3fus\n",
		 ring_name, count, elapsed(&start, &now)*1e6 / count);
}

static bool ignore_engine(int fd, unsigned engine)
{
	if (engine == 0)
		return true;

	if (gem_has_bsd2(fd) && engine == I915_EXEC_BSD)
		return true;

	return false;
}

static void all(int fd, uint32_t handle)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	struct timespec start, now;
	unsigned engines[16];
	unsigned nengine;
	unsigned engine;
	unsigned int count = 0;

	nengine = 0;
	for_each_engine(fd, engine)
		if (!ignore_engine(fd, engine)) engines[nengine++] = engine;
	igt_require(nengine);

	memset(&obj, 0, sizeof(obj));
	obj.handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&obj;
	execbuf.buffer_count = 1;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	if (__gem_execbuf(fd, &execbuf) == 0) {
		execbuf.flags = 0;
		gem_execbuf(fd, &execbuf);
	}
	gem_sync(fd, handle);

	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		for (int loop = 0; loop < 1024; loop++) {
			for (int n = 0; n < nengine; n++) {
				execbuf.flags &= ~ENGINE_FLAGS;
				execbuf.flags |= engines[n];
				gem_execbuf(fd, &execbuf);
			}
		}
		count += nengine * 1024;
		clock_gettime(CLOCK_MONOTONIC, &now);
	} while (elapsed(&start, &now) < 150.); /* Hang detection ~120s */
	gem_sync(fd, handle);
	clock_gettime(CLOCK_MONOTONIC, &now);

	igt_info("All (%d engines): %'u cycles: %.3fus\n",
		 nengine, count, elapsed(&start, &now)*1e6 / count);
}

igt_main
{
	const struct intel_execution_engine *e;
	uint32_t handle = 0;
	int device = -1;

	igt_fixture {
		const uint32_t bbe = MI_BATCH_BUFFER_END;

		device = drm_open_driver(DRIVER_INTEL);
		handle = gem_create(device, 4096);
		gem_write(device, handle, 0, &bbe, sizeof(bbe));
	}

	for (e = intel_execution_engines; e->name; e++)
		igt_subtest_f("%s", e->name)
			single(device, handle, e->exec_id | e->flags, e->name);

	igt_subtest("basic")
		all(device, handle);

	igt_fixture {
		gem_close(device, handle);
		close(device);
	}
}
