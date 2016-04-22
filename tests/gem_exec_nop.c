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

static double nop_on_ring(int fd, uint32_t handle, unsigned ring_id,
			  int timeout, unsigned long *out)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	struct timespec start, now;
	unsigned long count;

	memset(&obj, 0, sizeof(obj));
	obj.handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&obj;
	execbuf.buffer_count = 1;
	execbuf.flags = ring_id;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	if (__gem_execbuf(fd, &execbuf)) {
		execbuf.flags = ring_id;
		gem_execbuf(fd, &execbuf);
	}
	gem_sync(fd, handle);
	intel_detect_and_clear_missed_interrupts(fd);

	count = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		for (int loop = 0; loop < 1024; loop++)
			gem_execbuf(fd, &execbuf);

		count += 1024;
		clock_gettime(CLOCK_MONOTONIC, &now);
	} while (elapsed(&start, &now) < timeout);
	gem_sync(fd, handle);
	clock_gettime(CLOCK_MONOTONIC, &now);
	igt_assert_eq(intel_detect_and_clear_missed_interrupts(fd), 0);

	*out = count;
	return elapsed(&start, &now);
}

static void single(int fd, uint32_t handle,
		   unsigned ring_id, const char *ring_name)
{
	double time;
	unsigned long count;

	gem_require_ring(fd, ring_id);

	time = nop_on_ring(fd, handle, ring_id, 20, &count);
	igt_info("%s: %'lu cycles: %.3fus\n",
		 ring_name, count, time*1e6 / count);
}

static bool ignore_engine(int fd, unsigned engine)
{
	if (engine == 0)
		return true;

	if (gem_has_bsd2(fd) && engine == I915_EXEC_BSD)
		return true;

	return false;
}

static void all(int fd, uint32_t handle, int timeout)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	struct timespec start, now;
	unsigned engines[16];
	unsigned nengine;
	unsigned engine;
	unsigned long count;
	double time, max = 0, min = HUGE_VAL, sum = 0;
	const char *name;

	nengine = 0;
	for_each_engine(fd, engine) {
		if (ignore_engine(fd, engine))
			continue;

		time = nop_on_ring(fd, handle, engine, 1, &count) / count;
		if (time > max) {
			name = e__->name;
			max = time;
		}
		if (time < min)
			min = time;
		sum += time;
		engines[nengine++] = engine;
	}
	igt_require(nengine);
	igt_info("Maximum execution latency on %s, %.3fus, total %.3fus per cycle\n",
		 name, max*1e6, sum*1e6);

	memset(&obj, 0, sizeof(obj));
	obj.handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&obj;
	execbuf.buffer_count = 1;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	if (__gem_execbuf(fd, &execbuf)) {
		execbuf.flags = 0;
		gem_execbuf(fd, &execbuf);
	}
	gem_sync(fd, handle);
	intel_detect_and_clear_missed_interrupts(fd);

	count = 0;
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
	} while (elapsed(&start, &now) < timeout); /* Hang detection ~120s */
	gem_sync(fd, handle);
	clock_gettime(CLOCK_MONOTONIC, &now);
	igt_assert_eq(intel_detect_and_clear_missed_interrupts(fd), 0);

	time = elapsed(&start, &now) / count;
	igt_info("All (%d engines): %'lu cycles, average %.3fus per cycle\n",
		 nengine, count, 1e6*time);

	/* The rate limiting step is how fast the slowest engine can
	 * its queue of requests, if we wait upon a full ring all dispatch
	 * is frozen. So in general we cannot go faster than the slowest
	 * engine, but we should equally not go any slower.
	 */
	igt_assert_f(time < max + 10*min/9, /* ensure parallel execution */
		     "Average time (%.3fus) exceeds expecation for parallel execution (min %.3fus, max %.3fus; limit set at %.3fus)\n",
		     1e6*time, 1e6*min, 1e6*max, 1e6*(max + 10*min/9));
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

	igt_fork_hang_detector(device);

	igt_subtest("basic")
		all(device, handle, 10);

	for (e = intel_execution_engines; e->name; e++)
		igt_subtest_f("%s", e->name)
			single(device, handle, e->exec_id | e->flags, e->name);

	igt_subtest("all")
		all(device, handle, 150);

	igt_stop_hang_detector();

	igt_fixture {
		gem_close(device, handle);
		close(device);
	}
}
