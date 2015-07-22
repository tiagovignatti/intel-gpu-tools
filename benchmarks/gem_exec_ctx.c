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
#include <time.h>

#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_io.h"
#include "igt_stats.h"

enum mode { NOP, CREATE, SWITCH, };

#define LOCAL_I915_EXEC_NO_RELOC (1<<11)
#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

static uint64_t elapsed(const struct timespec *start,
		const struct timespec *end,
		int loop)
{
	return (1000000000ULL*(end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec))/loop;
}

static int __gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *execbuf)
{
	int err = 0;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, execbuf))
		err = -errno;
	return err;
}

static uint32_t batch(int fd)
{
	const uint32_t buf[] = {MI_BATCH_BUFFER_END};
	uint32_t handle = gem_create(fd, 4096);
	gem_write(fd, handle, 0, buf, sizeof(buf));
	return handle;
}

static uint32_t __gem_context_create(int fd)
{
	struct drm_i915_gem_context_create create;

	memset(&create, 0, sizeof(create));
	drmIoctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, &create);

	return create.ctx_id;
}

static int loop(unsigned ring, int reps, enum mode mode)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec;
	int count, fd;
	uint32_t ctx;

	fd = drm_open_any();

	memset(&gem_exec, 0, sizeof(gem_exec));
	gem_exec.handle = batch(fd);

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&gem_exec;
	execbuf.buffer_count = 1;
	execbuf.flags = ring;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	execbuf.rsvd1 = __gem_context_create(fd);
	if (execbuf.rsvd1 == 0)
		return 77;

	if (__gem_execbuf(fd, &execbuf)) {
		execbuf.flags = ring;
		if (__gem_execbuf(fd, &execbuf))
			return 77;
	}

	ctx = gem_context_create(fd);

	for (count = 1; count <= 1<<16; count <<= 1) {
		igt_stats_t stats;
		int n;

		igt_stats_init_with_size(&stats, reps);

		for (n = 0; n < reps; n++) {
			struct timespec start, end;
			int loops = count;
			sleep(1); /* wait for the hw to go back to sleep */
			clock_gettime(CLOCK_MONOTONIC, &start);
			while (loops--) {
				uint32_t tmp;
				switch (mode) {
				case CREATE:
					ctx = execbuf.rsvd1;
					execbuf.rsvd1 = gem_context_create(fd);
					break;

				case SWITCH:
					tmp = execbuf.rsvd1;
					execbuf.rsvd1 = ctx;
					ctx = tmp;
					break;
				case NOP:
					break;
				}
				do_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
				if (mode == CREATE)
					gem_context_destroy(fd, ctx);
			}
			gem_sync(fd, gem_exec.handle);
			clock_gettime(CLOCK_MONOTONIC, &end);
			igt_stats_push(&stats, elapsed(&start, &end, count));
		}

		printf("%7.3f\n", igt_stats_get_trimean(&stats)/1000);
		igt_stats_fini(&stats);
	}
	return 0;
}

int main(int argc, char **argv)
{
	unsigned ring = I915_EXEC_RENDER;
	enum mode mode = NOP;
	int reps = 13;
	int c;

	while ((c = getopt (argc, argv, "e:r:b:")) != -1) {
		switch (c) {
		case 'e':
			if (strcmp(optarg, "rcs") == 0)
				ring = I915_EXEC_RENDER;
			else if (strcmp(optarg, "vcs") == 0)
				ring = I915_EXEC_BSD;
			else if (strcmp(optarg, "bcs") == 0)
				ring = I915_EXEC_BLT;
			else if (strcmp(optarg, "vecs") == 0)
				ring = I915_EXEC_VEBOX;
			else
				ring = atoi(optarg);
			break;

		case 'b':
			if (strcmp(optarg, "create") == 0)
				mode = CREATE;
			else if (strcmp(optarg, "switch") == 0)
				mode = SWITCH;
			else if (strcmp(optarg, "nop") == 0)
				mode = NOP;
			else
				abort();
			break;

		case 'r':
			reps = atoi(optarg);
			if (reps < 1)
				reps = 1;
			break;

		default:
			break;
		}
	}

	return loop(ring, reps, mode);
}
