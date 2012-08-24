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
 *    Ben Widawsky <ben@bwidawsk.net>
 *
 */

#include <stdio.h>
#include <time.h>
#include "drm.h"
#include "rendercopy.h"

#define MSEC_PER_SEC	1000L
#define USEC_PER_MSEC	1000L
#define NSEC_PER_USEC	1000L
#define NSEC_PER_MSEC	1000000L
#define USEC_PER_SEC	1000000L
#define NSEC_PER_SEC	1000000000L

#define ENOUGH_WORK_IN_SECONDS 2
#define BUF_SIZE (8<<20)
#define BUF_PAGES ((8<<20)>>12)
drm_intel_bo *dst, *dst2;

/* returns time diff in milliseconds */
static int64_t
do_time_diff(struct timespec *end, struct timespec *start)
{
	int64_t ret;
	ret = (MSEC_PER_SEC * difftime(end->tv_sec, start->tv_sec)) +
	      ((end->tv_nsec/NSEC_PER_MSEC) - (start->tv_nsec/NSEC_PER_MSEC));
	return ret;
}

/* to avoid stupid depencies on libdrm, copy&paste */
struct local_drm_i915_gem_wait {
	/** Handle of BO we shall wait on */
	__u32 bo_handle;
	__u32 flags;
	/** Number of nanoseconds to wait, Returns time remaining. */
	__u64 timeout_ns;
};

# define WAIT_IOCTL DRM_IOWR(DRM_COMMAND_BASE + 0x2c, struct local_drm_i915_gem_wait)

static int
gem_bo_wait_timeout(int fd, uint32_t handle, uint64_t *timeout_ns)
{
	struct local_drm_i915_gem_wait wait;
	int ret;

	assert(timeout_ns);

	wait.bo_handle = handle;
	wait.timeout_ns = *timeout_ns;
	wait.flags = 0;
	ret = drmIoctl(fd, WAIT_IOCTL, &wait);
	*timeout_ns = wait.timeout_ns;

	return ret ? -errno : 0;
}

static bool
gem_bo_busy(int fd, uint32_t handle)
{
	struct drm_i915_gem_busy busy;

	busy.handle = handle;
	do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_BUSY, &busy));

	return !!busy.busy;
}

static void blt_color_fill(struct intel_batchbuffer *batch,
			   drm_intel_bo *buf,
			   const unsigned int pages)
{
	const unsigned short height = pages/4;
	const unsigned short width =  4096;
	BEGIN_BATCH(5);
	OUT_BATCH(COLOR_BLT_CMD		|
		  COLOR_BLT_WRITE_ALPHA	|
		  COLOR_BLT_WRITE_RGB);
	OUT_BATCH((3 << 24)	| /* 32 Bit Color */
		  0xF0		| /* Raster OP copy background register */
		  0);		  /* Dest pitch is 0 */
	OUT_BATCH(width << 16	|
		  height);
	OUT_RELOC(buf, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(rand()); /* random pattern */
	ADVANCE_BATCH();
}

int main(int argc, char **argv)
{
	drm_intel_bufmgr *bufmgr;
	struct intel_batchbuffer *batch;
	uint64_t timeout = ENOUGH_WORK_IN_SECONDS * NSEC_PER_SEC;
	int fd, ret;
	const bool do_signals = true; /* signals will seem to make the operation
				       * use less process CPU time */
	bool done = false;
	int i, iter = 1;

	fd = drm_open_any();

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));

	dst = drm_intel_bo_alloc(bufmgr, "dst", BUF_SIZE, 4096);
	dst2 = drm_intel_bo_alloc(bufmgr, "dst2", BUF_SIZE, 4096);

	if (gem_bo_wait_timeout(fd, dst->handle, &timeout) == -EINVAL) {
		printf("kernel doesn't support wait_timeout, skipping test\n");
		return -77;
	}
	timeout = ENOUGH_WORK_IN_SECONDS * NSEC_PER_SEC;

	/* Figure out a rough number of fills required to consume 1 second of
	 * GPU work.
	 */
	do {
		struct timespec start, end;
		long diff;

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

		assert(clock_gettime(CLOCK_MONOTONIC_RAW, &start) == 0);
		for (i = 0; i < iter; i++)
			blt_color_fill(batch, dst, BUF_PAGES);
		intel_batchbuffer_flush(batch);
		drm_intel_bo_wait_rendering(dst);
		assert(clock_gettime(CLOCK_MONOTONIC_RAW, &end) == 0);

		diff = do_time_diff(&end, &start);
		assert(diff >= 0);

		if ((diff / MSEC_PER_SEC) > ENOUGH_WORK_IN_SECONDS)
			done = true;
		else
			iter <<= 1;
	} while (!done && iter < 1000000);

	assert(iter < 1000000);

	printf("%d iters is enough work\n", iter);
	gem_quiescent_gpu(fd);
	if (do_signals)
		drmtest_fork_signal_helper();

	/* We should be able to do half as much work in the same amount of time,
	 * but because we might schedule almost twice as much as required, we
	 * might accidentally time out. Hence add some fudge. */
	for (i = 0; i < iter/3; i++)
		blt_color_fill(batch, dst2, BUF_PAGES);

	intel_batchbuffer_flush(batch);
	assert(gem_bo_busy(fd, dst2->handle) == true);

	ret = gem_bo_wait_timeout(fd, dst2->handle, &timeout);
	if (ret) {
		fprintf(stderr, "Timed wait failed %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	assert(gem_bo_busy(fd, dst2->handle) == false);
	assert(timeout != 0);
	if (timeout ==  (ENOUGH_WORK_IN_SECONDS * NSEC_PER_SEC))
		printf("Buffer was already done!\n");
	else {
		printf("Finished with %lu time remaining\n", timeout);
	}

	/* check that polling with timeout=0 works. */
	timeout = 0;
	assert(gem_bo_wait_timeout(fd, dst2->handle, &timeout) == 0);
	assert(timeout == 0);

	/* Now check that we correctly time out, twice the auto-tune load should
	 * be good enough. */
	timeout = ENOUGH_WORK_IN_SECONDS * NSEC_PER_SEC;
	for (i = 0; i < iter*2; i++)
		blt_color_fill(batch, dst2, BUF_PAGES);

	intel_batchbuffer_flush(batch);

	ret = gem_bo_wait_timeout(fd, dst2->handle, &timeout);
	assert(ret == -ETIME);
	assert(timeout == 0);
	assert(gem_bo_busy(fd, dst2->handle) == true);

	/* check that polling with timeout=0 works. */
	timeout = 0;
	assert(gem_bo_wait_timeout(fd, dst2->handle, &timeout) == -ETIME);
	assert(timeout == 0);


	if (do_signals)
		drmtest_stop_signal_helper();
	drm_intel_bo_unreference(dst2);
	drm_intel_bo_unreference(dst);
	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
