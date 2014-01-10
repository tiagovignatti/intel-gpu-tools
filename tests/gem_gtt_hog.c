/*
 * Copyright © 2014 Intel Corporation
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
#include <sys/wait.h>

#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"

#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)

static const uint32_t canary = 0xdeadbeef;

static double elapsed(const struct timeval *start,
		      const struct timeval *end)
{
	return 1e6*(end->tv_sec - start->tv_sec) + (end->tv_usec - start->tv_usec);
}

static void busy(int fd, uint32_t handle, int size, int loops)
{
	struct drm_i915_gem_relocation_entry reloc[20];
	struct drm_i915_gem_exec_object2 gem_exec[2];
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_pwrite gem_pwrite;
	struct drm_i915_gem_create create;
	uint32_t buf[102], *b;
	int i;

	memset(reloc, 0, sizeof(reloc));
	memset(gem_exec, 0, sizeof(gem_exec));
	memset(&execbuf, 0, sizeof(execbuf));

	b = buf;
	for (i = 0; i < 20; i++) {
		*b++ = COLOR_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB;
		*b++ = 0xf0 << 16 | 1 << 25 | 1 << 24 | 4096;
		*b++ = size >> 12 << 16 | 4096;
		reloc[i].offset = (b - buf) * sizeof(uint32_t);
		reloc[i].target_handle = handle;
		reloc[i].read_domains = I915_GEM_DOMAIN_RENDER;
		reloc[i].write_domain = I915_GEM_DOMAIN_RENDER;
		*b++ = 0;
		*b++ = canary;
	}
	*b++ = MI_BATCH_BUFFER_END;
	*b++ = 0;

	gem_exec[0].handle = handle;
	gem_exec[0].flags = EXEC_OBJECT_NEEDS_FENCE;

	create.handle = 0;
	create.size = 4096;
	drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);
	gem_exec[1].handle = create.handle;
	gem_exec[1].relocation_count = 20;
	gem_exec[1].relocs_ptr = (uintptr_t)reloc;

	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 2;
	execbuf.batch_len = sizeof(buf);
	execbuf.flags = 1 << 11;
	if (HAS_BLT_RING(intel_get_drm_devid(fd)))
		execbuf.flags |= I915_EXEC_BLT;

	gem_pwrite.handle = gem_exec[1].handle;
	gem_pwrite.offset = 0;
	gem_pwrite.size = sizeof(buf);
	gem_pwrite.data_ptr = (uintptr_t)buf;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite) == 0) {
		while (loops--)
			drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
	}

	drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &create.handle);
}

static void run(int child)
{
	const int size = 4096 * (256 + child * child);
	const int tiling = child % 2;
	const int write = child % 2;
	int fd = drm_open_any();
	uint32_t handle = gem_create(fd, size);
	uint32_t *ptr;
	uint32_t x;

	igt_assert(handle);

	if (tiling != I915_TILING_NONE)
		gem_set_tiling(fd, handle, tiling, 4096);

	/* load up the unfaulted bo */
	busy(fd, handle, size, 100);

	/* Note that we ignore the API and rely on the implict
	 * set-to-gtt-domain within the fault handler.
	 */
	if (write) {
		ptr = gem_mmap(fd, handle, size, PROT_READ | PROT_WRITE);
		ptr[rand() % (size / 4)] = canary;
	} else
		ptr = gem_mmap(fd, handle, size, PROT_READ);
	x = ptr[rand() % (size / 4)];
	munmap(ptr, size);

	igt_assert(x == canary);
	igt_exit();
}

int main(int argc, char **argv)
{
	struct timeval start, end;
	pid_t children[64];
	int n;

	igt_simple_init();
	igt_skip_on_simulation();

	gettimeofday(&start, NULL);
	for (n = 0; n < ARRAY_SIZE(children); n++) {
		switch ((children[n] = fork())) {
		case -1: igt_assert(0);
		case 0: run(n); break;
		default: break;
		}
	}

	for (n = 0; n < ARRAY_SIZE(children); n++) {
		int status = -1;
		while (waitpid(children[n], &status, 0) == -1 &&
		       errno == -EINTR)
			;
		igt_assert(status == 0);
	}
	gettimeofday(&end, NULL);
	printf("Time to execute %lu children:		%7.3fms\n",
	       ARRAY_SIZE(children), elapsed(&start, &end) / 1000);

	return 0;
}
