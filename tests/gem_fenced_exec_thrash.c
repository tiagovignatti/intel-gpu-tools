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

#define _GNU_SOURCE

#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <drm.h>
#include <i915_drm.h>

#include "drmtest.h"

#define WIDTH 1024
#define HEIGHT 1024
#define OBJECT_SIZE (4*WIDTH*HEIGHT)

#define BATCH_SIZE 4096

#define MAX_FENCES 16

#define MI_BATCH_BUFFER_END	(0xA<<23)

/*
 * Testcase: execbuf fence accounting
 *
 * We had a bug where we were falsely accounting upon reservation already
 * fenced buffers as occupying a fence register even if they did not require
 * one for the batch.
 *
 * We aim to exercise this by performing a sequence of fenced BLT
 * with 2*num_avail_fence buffers, but alternating which half are fenced in
 * each command.
 */

static uint32_t
tiled_bo_create (int fd)
{
	uint32_t handle;

	handle = gem_create(fd, OBJECT_SIZE);

	gem_set_tiling(fd, handle, I915_TILING_X, WIDTH*4);

	return handle;
}

static uint32_t
batch_create (int fd)
{
	uint32_t buf[] = { MI_BATCH_BUFFER_END, 0 };
	uint32_t batch_handle;

	batch_handle = gem_create(fd, BATCH_SIZE);

	gem_write(fd, batch_handle, 0, buf, sizeof(buf));

	return batch_handle;
}

static int get_num_fences(int fd)
{
	drm_i915_getparam_t gp;
	int ret, val;

	gp.param = I915_PARAM_NUM_FENCES_AVAIL;
	gp.value = &val;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
	assert (ret == 0);

	printf ("total %d fences\n", val);
	assert(val > 4);

	return val - 2;
}

static void fill_reloc(struct drm_i915_gem_relocation_entry *reloc, uint32_t handle)
{
	reloc->offset = 2 * sizeof(uint32_t);
	reloc->target_handle = handle;
	reloc->read_domains = I915_GEM_DOMAIN_RENDER;
	reloc->write_domain = 0;
}

int
main(int argc, char **argv)
{
	struct drm_i915_gem_execbuffer2 execbuf[2];
	struct drm_i915_gem_exec_object2 exec[2][2*MAX_FENCES+1];
	struct drm_i915_gem_relocation_entry reloc[2*MAX_FENCES];

	int fd = drm_open_any();
	int i, n, num_fences;
	int loop = 1000;

	memset(execbuf, 0, sizeof(execbuf));
	memset(exec, 0, sizeof(exec));
	memset(reloc, 0, sizeof(reloc));

	num_fences = get_num_fences(fd) & ~1;
	assert(num_fences <= MAX_FENCES);
	for (n = 0; n < 2*num_fences; n++) {
		uint32_t handle = tiled_bo_create(fd);
		exec[1][2*num_fences - n-1].handle = exec[0][n].handle = handle;
		fill_reloc(&reloc[n], handle);
	}

	for (i = 0; i < 2; i++) {
		for (n = 0; n < num_fences; n++)
			exec[i][n].flags = EXEC_OBJECT_NEEDS_FENCE;

		exec[i][2*num_fences].handle = batch_create(fd);
		exec[i][2*num_fences].relocs_ptr = (uintptr_t)reloc;
		exec[i][2*num_fences].relocation_count = 2*num_fences;

		execbuf[i].buffers_ptr = (uintptr_t)exec[i];
		execbuf[i].buffer_count = 2*num_fences+1;
		execbuf[i].batch_len = 2*sizeof(uint32_t);
	}

	do {
		int ret;

		ret = drmIoctl(fd,
			       DRM_IOCTL_I915_GEM_EXECBUFFER2,
			       &execbuf[0]);
		assert(ret == 0);

		ret = drmIoctl(fd,
			       DRM_IOCTL_I915_GEM_EXECBUFFER2,
			       &execbuf[1]);
		assert(ret == 0);
	} while (--loop);

	close(fd);

	return 0;
}
