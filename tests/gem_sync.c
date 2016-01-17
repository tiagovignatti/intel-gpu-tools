/*
 * Copyright © 2016 Intel Corporation
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

#include <time.h>

#include "igt.h"

IGT_TEST_DESCRIPTION("Basic check of ring<->ring write synchronisation.");

/*
 * Testcase: Basic check of sync
 *
 * Extremely efficient at catching missed irqs
 */

static unsigned intel_detect_and_clear_missed_irq(int fd)
{
	unsigned missed = 0;
	FILE *file;

	gem_quiescent_gpu(fd);

	file = igt_debugfs_fopen("i915_ring_missed_irq", "r");
	if (file) {
		igt_assert(fscanf(file, "%x", &missed) == 1);
		fclose(file);
	}
	if (missed) {
		file = igt_debugfs_fopen("i915_ring_missed_irq", "w");
		if (file) {
			fwrite("0\n", 1, 2, file);
			fclose(file);
		}
	}

	return missed;
}

static double gettime(void)
{
	static clockid_t clock = -1;
	struct timespec ts;

	/* Stay on the same clock for consistency. */
	if (clock != (clockid_t)-1) {
		if (clock_gettime(clock, &ts))
			goto error;
		goto out;
	}

#ifdef CLOCK_MONOTONIC_RAW
	if (!clock_gettime(clock = CLOCK_MONOTONIC_RAW, &ts))
		goto out;
#endif
#ifdef CLOCK_MONOTONIC_COARSE
	if (!clock_gettime(clock = CLOCK_MONOTONIC_COARSE, &ts))
		goto out;
#endif
	if (!clock_gettime(clock = CLOCK_MONOTONIC, &ts))
		goto out;
error:
	igt_warn("Could not read monotonic time: %s\n",
			strerror(errno));
	igt_assert(0);
	return 0;

out:
	return ts.tv_sec + 1e-9*ts.tv_nsec;
}

static int __gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *eb)
{
	int err = 0;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, eb))
		err = -errno;
	return err;
}

static void
sync_ring(int fd, int ring, unsigned flags)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 object;
	double start, elapsed;
	unsigned long cycles;

	intel_detect_and_clear_missed_irq(fd); /* clear before we begin */

	memset(&object, 0, sizeof(object));
	object.handle = gem_create(fd, 4096);
	gem_write(fd, object.handle, 4096-sizeof(bbe), &bbe, sizeof(bbe));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&object;
	execbuf.buffer_count = 1;
	execbuf.flags = ring | flags;

	igt_require(__gem_execbuf(fd, &execbuf) == 0);

	srandom(0xdeadbeef);

	start = gettime();
	cycles = 0;
	do {
		gem_execbuf(fd, &execbuf);
		gem_sync(fd, object.handle);
		cycles++;
	} while ((elapsed = gettime() - start) < SLOW_QUICK(10, 1));
	igt_info("Completed %ld cycles: %.3f us\n", cycles, elapsed*1e6/cycles);

	gem_close(fd, object.handle);
	igt_assert_eq(intel_detect_and_clear_missed_irq(fd), 0);
}

igt_main
{
	int fd = -1;

	igt_skip_on_simulation();

	igt_fixture
		fd = drm_open_driver(DRIVER_INTEL);

	igt_subtest("basic-render")
		sync_ring(fd, I915_EXEC_RENDER, 0);
	igt_subtest("basic-blt")
		sync_ring(fd, I915_EXEC_BLT, 0);
	igt_subtest("bsd")
		sync_ring(fd, I915_EXEC_BSD, 0);
	igt_subtest("bsd1")
		sync_ring(fd, I915_EXEC_BSD, 1<<13 /*I915_EXEC_BSD_RING1*/);
	igt_subtest("bsd2")
		sync_ring(fd, I915_EXEC_BSD, 2<<13 /*I915_EXEC_BSD_RING2*/);
	igt_subtest("vebox")
		sync_ring(fd, I915_EXEC_VEBOX, 0);

	igt_fixture
		close(fd);
}
