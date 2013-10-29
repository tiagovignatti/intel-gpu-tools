/*
 * Copyright © 2013 Intel Corporation
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
#include "i915_drm.h"
#include "drmtest.h"

#define OBJECT_SIZE 1024*1024*4

#define COPY_BLT_CMD		(2<<29|0x53<<22|0x6)
#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)

static char device[80];

static void selfcopy(int fd, uint32_t handle)
{
	struct drm_i915_gem_relocation_entry reloc[2];
	struct drm_i915_gem_exec_object2 gem_exec[2];
	struct drm_i915_gem_execbuffer2 execbuf;
	uint32_t buf[10];

	memset(reloc, 0, sizeof(reloc));
	memset(gem_exec, 0, sizeof(gem_exec));
	memset(&execbuf, 0, sizeof(execbuf));

	buf[0] = COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB;
	buf[1] = 0xcc << 16 | 1 << 25 | 1 << 24 | (4*1024);
	buf[2] = 0;
	buf[3] = 1024 << 16 | 1024;
	buf[4] = 0;
	reloc[0].offset = 4 * sizeof(uint32_t);
	reloc[0].target_handle = handle;
	reloc[0].read_domains = I915_GEM_DOMAIN_RENDER;
	reloc[0].write_domain = I915_GEM_DOMAIN_RENDER;

	buf[5] = 0;
	buf[6] = 4*1024;
	buf[7] = 0;
	reloc[1].offset = 7 * sizeof(uint32_t);
	reloc[1].target_handle = handle;
	reloc[1].read_domains = I915_GEM_DOMAIN_RENDER;
	reloc[1].write_domain = 0;

	buf[8] = MI_BATCH_BUFFER_END;
	buf[9] = 0;

	gem_exec[0].handle = handle;

	gem_exec[1].handle = gem_create(fd, 4096);
	gem_exec[1].relocation_count = 2;
	gem_exec[1].relocs_ptr = (uintptr_t)reloc;

	gem_write(fd, gem_exec[1].handle, 0, buf, sizeof(buf));

	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 2;
	execbuf.batch_len = sizeof(buf);
	if (HAS_BLT_RING(intel_get_drm_devid(fd)))
		execbuf.flags |= I915_EXEC_BLT;

	gem_execbuf(fd, &execbuf);
}

static uint32_t load(int fd)
{
	uint32_t handle;

	handle = gem_create(fd, OBJECT_SIZE);
	if (handle == 0)
		return 0;

	selfcopy(fd, handle);
	return handle;
}

static void run(int child)
{
	uint32_t handle;
	int fd;

	fd = open(device, O_RDWR);
	igt_assert(fd != -1);

	handle = load(fd);
	if (child & 1)
		gem_read(fd, handle, 0, &handle, sizeof(handle));
}

int main(int argc, char *argv[])
{
	igt_skip_on_simulation();
	igt_subtest_init(argc, argv);

	sprintf(device, "/dev/dri/card%d", drm_get_card());

	igt_subtest("gem-close-race") {
		igt_fork(child, 100)
			run(child);
		igt_waitchildren();
	}

	igt_exit();
}
