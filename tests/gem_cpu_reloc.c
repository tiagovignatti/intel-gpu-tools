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
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

/*
 * Testcase: Test the relocations through the CPU domain
 *
 * Attempt to stress test performing relocations whilst the batch is in the
 * CPU domain.
 *
 * A freshly allocated buffer starts in the CPU domain, and the pwrite
 * should also be performed whilst in the CPU domain and so we should
 * execute the relocations within the CPU domain. If for any reason one of
 * those steps should land it in the GTT domain, we take the secondary
 * precaution of filling the mappable portion of the GATT.
 *
 * In order to detect whether a relocation fails, we first fill a target
 * buffer with a sequence of invalid commands that would cause the GPU to
 * immediate hang, and then attempt to overwrite them with a legal, if
 * short, batchbuffer using a BLT. Then we come to execute the bo, if the
 * relocation fail and we either copy across all zeros or garbage, then the
 * GPU will hang.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_gpu_tools.h"

static uint32_t use_blt;

static void copy(int fd, uint32_t batch, uint32_t src, uint32_t dst)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_relocation_entry gem_reloc[2];
	struct drm_i915_gem_exec_object2 gem_exec[3];

	gem_reloc[0].offset = 4 * sizeof(uint32_t);
	gem_reloc[0].delta = 0;
	gem_reloc[0].target_handle = dst;
	gem_reloc[0].read_domains = I915_GEM_DOMAIN_RENDER;
	gem_reloc[0].write_domain = I915_GEM_DOMAIN_RENDER;
	gem_reloc[0].presumed_offset = 0;

	gem_reloc[1].offset = 7 * sizeof(uint32_t);
	gem_reloc[1].delta = 0;
	gem_reloc[1].target_handle = src;
	gem_reloc[1].read_domains = I915_GEM_DOMAIN_RENDER;
	gem_reloc[1].write_domain = 0;
	gem_reloc[1].presumed_offset = 0;

	memset(gem_exec, 0, sizeof(gem_exec));
	gem_exec[0].handle = src;
	gem_exec[1].handle = dst;
	gem_exec[2].handle = batch;
	gem_exec[2].relocation_count = 2;
	gem_exec[2].relocs_ptr = (uintptr_t)gem_reloc;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 3;
	execbuf.batch_len = 4096;
	execbuf.flags = use_blt;

	do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));
}

static void exec(int fd, uint32_t handle)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec;

	memset(&gem_exec, 0, sizeof(gem_exec));
	gem_exec.handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&gem_exec;
	execbuf.buffer_count = 1;
	execbuf.batch_len = 4096;

	do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));
}

int main(int argc, char **argv)
{
	const uint32_t batch[] = {
		(XY_SRC_COPY_BLT_CMD |
		 XY_SRC_COPY_BLT_WRITE_ALPHA |
		 XY_SRC_COPY_BLT_WRITE_RGB),
		(3 << 24 | /* 32 bits */
		 0xcc << 16 | /* copy ROP */
		 4096),
		0 << 16 | 0, /* dst x1, y1 */
		1 << 16 | 2,
		0, /* dst relocation */
		0 << 16 | 0, /* src x1, y1 */
		4096,
		0, /* src relocation */
		MI_BATCH_BUFFER_END,
	};
	const uint32_t hang[] = {-1, -1, -1, -1};
	const uint32_t end[] = {MI_BATCH_BUFFER_END, 0};
	uint64_t aper_size;
	uint32_t noop;
	uint32_t *handles;
	int fd, i, count;

	fd = drm_open_any();
	noop = intel_get_drm_devid(fd);

	use_blt = 0;
	if (intel_gen(noop) >= 6)
		use_blt = I915_EXEC_BLT;

	aper_size = gem_mappable_aperture_size();
	if (intel_get_total_ram_mb() < aper_size / (1024*1024) * 2) {
		fprintf(stderr, "not enough mem to run test\n");
		return 77;
	}

	count = aper_size / 4096 * 2;
	if (drmtest_run_in_simulation())
		count = 10;

	handles = malloc (count * sizeof(uint32_t));
	assert(handles);

	noop = gem_create(fd, 4096);
	gem_write(fd, noop, 0, end, sizeof(end));

	/* fill the entire gart with batches and run them */
	for (i = 0; i < count; i++) {
		uint32_t bad;

		handles[i] = gem_create(fd, 4096);
		gem_write(fd, handles[i], 0, batch, sizeof(batch));

		bad = gem_create(fd, 4096);
		gem_write(fd, bad, 0, hang, sizeof(hang));

		/* launch the newly created batch */
		copy(fd, handles[i], noop, bad);
		exec(fd, bad);
		gem_close(fd, bad);

		drmtest_progress("gem_cpu_reloc: ", i, 2*count);
	}

	/* And again in reverse to try and catch the relocation code out */
	for (i = 0; i < count; i++) {
		uint32_t bad;

		bad = gem_create(fd, 4096);
		gem_write(fd, bad, 0, hang, sizeof(hang));

		/* launch the newly created batch */
		copy(fd, handles[count-i-1], noop, bad);
		exec(fd, bad);
		gem_close(fd, bad);

		drmtest_progress("gem_cpu_reloc: ", count+i, 3*count);
	}

	/* Third time lucky? */
	for (i = 0; i < count; i++) {
		uint32_t bad;

		bad = gem_create(fd, 4096);
		gem_write(fd, bad, 0, hang, sizeof(hang));

		/* launch the newly created batch */
		gem_set_domain(fd, handles[i],
			       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
		copy(fd, handles[i], noop, bad);
		exec(fd, bad);
		gem_close(fd, bad);

		drmtest_progress("gem_cpu_reloc: ", 2*count+i, 3*count);
	}

	printf("Test suceeded, cleanup up - this might take a while.\n");
	close(fd);

	return 0;
}
