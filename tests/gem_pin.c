/*
 * Copyright © 20013 Intel Corporation
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

/* Exercises pinning of small bo */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <drm.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_chipset.h"
#include "intel_io.h"
#include "igt_aux.h"

#define COPY_BLT_CMD            (2<<29|0x53<<22|0x6)
#define BLT_WRITE_ALPHA         (1<<21)
#define BLT_WRITE_RGB           (1<<20)

static void exec(int fd, uint32_t handle, uint32_t offset)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];
	struct drm_i915_gem_relocation_entry gem_reloc[1];

	gem_reloc[0].offset = 1024;
	gem_reloc[0].delta = 0;
	gem_reloc[0].target_handle = handle;
	gem_reloc[0].read_domains = I915_GEM_DOMAIN_RENDER;
	gem_reloc[0].write_domain = 0;
	gem_reloc[0].presumed_offset = 0;

	gem_exec[0].handle = handle;
	gem_exec[0].relocation_count = 1;
	gem_exec[0].relocs_ptr = (uintptr_t) gem_reloc;
	gem_exec[0].alignment = 0;
	gem_exec[0].offset = 0;
	gem_exec[0].flags = 0;
	gem_exec[0].rsvd1 = 0;
	gem_exec[0].rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = 8;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = 0;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));
	igt_assert(gem_exec[0].offset == offset);
}

static int gem_linear_blt(int fd,
			  uint32_t *batch,
			  uint32_t src,
			  uint32_t dst,
			  uint32_t length,
			  struct drm_i915_gem_relocation_entry *reloc)
{
	uint32_t *b = batch;

	*b++ = COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB;
	*b++ = 0x66 << 16 | 1 << 25 | 1 << 24 | (4*1024);
	*b++ = 0;
	*b++ = (length / (4*1024)) << 16 | 1024;
	*b++ = 0;
	reloc->offset = (b-batch-1) * sizeof(uint32_t);
	reloc->delta = 0;
	reloc->target_handle = dst;
	reloc->read_domains = I915_GEM_DOMAIN_RENDER;
	reloc->write_domain = I915_GEM_DOMAIN_RENDER;
	reloc->presumed_offset = 0;
	reloc++;
	if (intel_gen(intel_get_drm_devid(fd)) >= 8)
		*b++ = 0; /* FIXME */

	*b++ = 0;
	*b++ = 4*1024;
	*b++ = 0;
	reloc->offset = (b-batch-1) * sizeof(uint32_t);
	reloc->delta = 0;
	reloc->target_handle = src;
	reloc->read_domains = I915_GEM_DOMAIN_RENDER;
	reloc->write_domain = 0;
	reloc->presumed_offset = 0;
	reloc++;
	if (intel_gen(intel_get_drm_devid(fd)) >= 8)
		*b++ = 0; /* FIXME */

	*b++ = MI_BATCH_BUFFER_END;
	*b++ = 0;

	return (b - batch) * sizeof(uint32_t);
}

static void make_busy(int fd, uint32_t handle)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj[2];
	struct drm_i915_gem_relocation_entry reloc[2];
	uint32_t batch[20];
	uint32_t tmp;
	int count;

	tmp = gem_create(fd, 1024*1024);

	obj[0].handle = tmp;
	obj[0].relocation_count = 0;
	obj[0].relocs_ptr = 0;
	obj[0].alignment = 0;
	obj[0].offset = 0;
	obj[0].flags = 0;
	obj[0].rsvd1 = 0;
	obj[0].rsvd2 = 0;

	obj[1].handle = handle;
	obj[1].relocation_count = 2;
	obj[1].relocs_ptr = (uintptr_t) reloc;
	obj[1].alignment = 0;
	obj[1].offset = 0;
	obj[1].flags = 0;
	obj[1].rsvd1 = 0;
	obj[1].rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)obj;
	execbuf.buffer_count = 2;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = gem_linear_blt(fd, batch, tmp, tmp, 1024*1024,reloc);
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = 0;
	if (HAS_BLT_RING(intel_get_drm_devid(fd)))
		execbuf.flags |= I915_EXEC_BLT;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	gem_write(fd, handle, 0, batch, execbuf.batch_len);
	for (count = 0; count < 10; count++)
		do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));
	gem_close(fd, tmp);
}

static int test_can_pin(int fd)
{
	struct drm_i915_gem_pin pin;
	int ret;

	pin.handle = gem_create(fd, 4096);;
	pin.alignment = 0;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_PIN, &pin);
	gem_close(fd, pin.handle);

	return ret == 0;;
}

static uint32_t gem_pin(int fd, int handle, int alignment)
{
	struct drm_i915_gem_pin pin;

	pin.handle = handle;
	pin.alignment = alignment;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_PIN, &pin);
	return pin.offset;
}

igt_simple_main
{
	const uint32_t batch[2] = {MI_BATCH_BUFFER_END};
	struct timeval start, now;
	uint32_t *handle, *offset;
	int fd, i;

	igt_skip_on_simulation();

	fd = drm_open_any();

	igt_require(test_can_pin(fd));

	handle = malloc(sizeof(uint32_t)*100);
	offset = malloc(sizeof(uint32_t)*100);

	/* Race creation/use against interrupts */
	igt_fork_signal_helper();
	gettimeofday(&start, NULL);
	do {
		for (i = 0; i < 100; i++) {
			if (i & 1) {
				/* pin anidle bo */
				handle[i] = gem_create(fd, 4096);
				offset[i] = gem_pin(fd, handle[i], 0);
				igt_assert(offset[i]);
				gem_write(fd, handle[i], 0, batch, sizeof(batch));
			} else {
				/* try to pin an anidle bo */
				handle[i] = gem_create(fd, 4096);
				make_busy(fd, handle[i]);
				offset[i] = gem_pin(fd, handle[i], 256*1024);
				igt_assert(offset[i]);
				igt_assert((offset[i] & (256*1024-1)) == 0);
				gem_write(fd, handle[i], 0, batch, sizeof(batch));
			}
		}
		for (i = 0; i < 1000; i++) {
			int j = rand() % 100;
			exec(fd, handle[j], offset[j]);
		}
		for (i = 0; i < 100; i++)
			gem_close(fd, handle[i]);
		gettimeofday(&now, NULL);
	} while ((now.tv_sec - start.tv_sec)*1000 + (now.tv_usec - start.tv_usec) / 1000 < 10000);
	igt_stop_signal_helper();
}
