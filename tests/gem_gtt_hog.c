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
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_chipset.h"

static const uint32_t canary = 0xdeadbeef;

typedef struct data {
	int fd;
	int devid;
	int intel_gen;
} data_t;

static double elapsed(const struct timeval *start,
		      const struct timeval *end)
{
	return 1e6*(end->tv_sec - start->tv_sec) + (end->tv_usec - start->tv_usec);
}

static void busy(data_t *data, uint32_t handle, int size, int loops)
{
	struct drm_i915_gem_relocation_entry reloc[20];
	struct drm_i915_gem_exec_object2 gem_exec[2];
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_pwrite gem_pwrite;
	struct drm_i915_gem_create create;
	uint32_t buf[170], *b;
	int i;

	memset(reloc, 0, sizeof(reloc));
	memset(gem_exec, 0, sizeof(gem_exec));
	memset(&execbuf, 0, sizeof(execbuf));

	b = buf;
	for (i = 0; i < 20; i++) {
		*b++ = XY_COLOR_BLT_CMD_NOLEN |
			((data->intel_gen >= 8) ? 5 : 4) |
			COLOR_BLT_WRITE_ALPHA | XY_COLOR_BLT_WRITE_RGB;
		*b++ = 0xf0 << 16 | 1 << 25 | 1 << 24 | 4096;
		*b++ = 0;
		*b++ = size >> 12 << 16 | 1024;
		reloc[i].offset = (b - buf) * sizeof(uint32_t);
		reloc[i].target_handle = handle;
		reloc[i].read_domains = I915_GEM_DOMAIN_RENDER;
		reloc[i].write_domain = I915_GEM_DOMAIN_RENDER;
		*b++ = 0;
		if (data->intel_gen >= 8)
			*b++ = 0;
		*b++ = canary;
	}
	*b++ = MI_BATCH_BUFFER_END;
	if ((b - buf) & 1)
		*b++ = 0;

	gem_exec[0].handle = handle;
	gem_exec[0].flags = EXEC_OBJECT_NEEDS_FENCE;

	create.handle = 0;
	create.size = 4096;
	drmIoctl(data->fd, DRM_IOCTL_I915_GEM_CREATE, &create);
	gem_exec[1].handle = create.handle;
	gem_exec[1].relocation_count = 20;
	gem_exec[1].relocs_ptr = (uintptr_t)reloc;

	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 2;
	execbuf.batch_len = (b - buf) * sizeof(buf[0]);
	execbuf.flags = 1 << 11;
	if (HAS_BLT_RING(data->devid))
		execbuf.flags |= I915_EXEC_BLT;

	gem_pwrite.handle = gem_exec[1].handle;
	gem_pwrite.offset = 0;
	gem_pwrite.size = execbuf.batch_len;
	gem_pwrite.data_ptr = (uintptr_t)buf;
	if (drmIoctl(data->fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite) == 0) {
		while (loops--)
			drmIoctl(data->fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
	}

	drmIoctl(data->fd, DRM_IOCTL_GEM_CLOSE, &create.handle);
}

static void run(data_t *data, int child)
{
	const int size = 4096 * (256 + child * child);
	const int tiling = child % 2;
	const int write = child % 2;
	uint32_t handle = gem_create(data->fd, size);
	uint32_t *ptr;
	uint32_t x;

	igt_assert(handle);

	if (tiling != I915_TILING_NONE)
		gem_set_tiling(data->fd, handle, tiling, 4096);

	/* load up the unfaulted bo */
	busy(data, handle, size, 100);

	/* Note that we ignore the API and rely on the implict
	 * set-to-gtt-domain within the fault handler.
	 */
	if (write) {
		ptr = gem_mmap(data->fd, handle, size, PROT_READ | PROT_WRITE);
		ptr[rand() % (size / 4)] = canary;
	} else
		ptr = gem_mmap(data->fd, handle, size, PROT_READ);
	x = ptr[rand() % (size / 4)];
	munmap(ptr, size);

	igt_assert(x == canary);
	exit(0);
}

igt_simple_main
{
	struct timeval start, end;
	pid_t children[64];
	data_t data = {};
	int n;

	/* check for an intel gpu before goint nuts. */
	int fd = drm_open_any();
	close(fd);

	igt_skip_on_simulation();

	data.fd = drm_open_any();
	data.devid = intel_get_drm_devid(data.fd);
	data.intel_gen = intel_gen(data.devid);

	gettimeofday(&start, NULL);
	for (n = 0; n < ARRAY_SIZE(children); n++) {
		switch ((children[n] = fork())) {
		case -1: igt_assert(0);
		case 0: run(&data, n); break;
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
	igt_info("Time to execute %lu children:		%7.3fms\n",
		 ARRAY_SIZE(children), elapsed(&start, &end) / 1000);
}
