/*
 * Copyright © 2009 Intel Corporation
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
 *    Eric Anholt <eric@anholt.net>
 *
 */

/** @file gem_linear_blits.c
 *
 * This is a test of doing many blits, with a working set
 * larger than the aperture size.
 *
 * The goal is to simply ensure the basics work.
 */

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
#include "intel_chipset.h"
#include "drmtest.h"
#include "intel_io.h"
#include "igt_aux.h"

#define WIDTH 512
#define HEIGHT 512

static uint32_t linear[WIDTH*HEIGHT];

static void
copy(int fd, uint32_t dst, uint32_t src)
{
	uint32_t batch[12];
	struct drm_i915_gem_relocation_entry reloc[2];
	struct drm_i915_gem_exec_object2 obj[3];
	struct drm_i915_gem_execbuffer2 exec;
	uint32_t handle;
	int ret, i=0;

	batch[i++] = XY_SRC_COPY_BLT_CMD |
		  XY_SRC_COPY_BLT_WRITE_ALPHA |
		  XY_SRC_COPY_BLT_WRITE_RGB;
	if (intel_gen(intel_get_drm_devid(fd)) >= 8)
		batch[i - 1] |= 8;
	else
		batch[i - 1] |= 6;

	batch[i++] = (3 << 24) | /* 32 bits */
		  (0xcc << 16) | /* copy ROP */
		  WIDTH*4;
	batch[i++] = 0; /* dst x1,y1 */
	batch[i++] = (HEIGHT << 16) | WIDTH; /* dst x2,y2 */
	batch[i++] = 0; /* dst reloc */
	if (intel_gen(intel_get_drm_devid(fd)) >= 8)
		batch[i++] = 0;
	batch[i++] = 0; /* src x1,y1 */
	batch[i++] = WIDTH*4;
	batch[i++] = 0; /* src reloc */
	if (intel_gen(intel_get_drm_devid(fd)) >= 8)
		batch[i++] = 0;
	batch[i++] = MI_BATCH_BUFFER_END;
	batch[i++] = MI_NOOP;

	handle = gem_create(fd, 4096);
	gem_write(fd, handle, 0, batch, sizeof(batch));

	reloc[0].target_handle = dst;
	reloc[0].delta = 0;
	reloc[0].offset = 4 * sizeof(batch[0]);
	reloc[0].presumed_offset = 0;
	reloc[0].read_domains = I915_GEM_DOMAIN_RENDER;;
	reloc[0].write_domain = I915_GEM_DOMAIN_RENDER;

	reloc[1].target_handle = src;
	reloc[1].delta = 0;
	reloc[1].offset = 7 * sizeof(batch[0]);
	if (intel_gen(intel_get_drm_devid(fd)) >= 8)
		reloc[1].offset += sizeof(batch[0]);
	reloc[1].presumed_offset = 0;
	reloc[1].read_domains = I915_GEM_DOMAIN_RENDER;;
	reloc[1].write_domain = 0;

	obj[0].handle = dst;
	obj[0].relocation_count = 0;
	obj[0].relocs_ptr = 0;
	obj[0].alignment = 0;
	obj[0].offset = 0;
	obj[0].flags = 0;
	obj[0].rsvd1 = 0;
	obj[0].rsvd2 = 0;

	obj[1].handle = src;
	obj[1].relocation_count = 0;
	obj[1].relocs_ptr = 0;
	obj[1].alignment = 0;
	obj[1].offset = 0;
	obj[1].flags = 0;
	obj[1].rsvd1 = 0;
	obj[1].rsvd2 = 0;

	obj[2].handle = handle;
	obj[2].relocation_count = 2;
	obj[2].relocs_ptr = (uintptr_t)reloc;
	obj[2].alignment = 0;
	obj[2].offset = 0;
	obj[2].flags = 0;
	obj[2].rsvd1 = obj[2].rsvd2 = 0;

	exec.buffers_ptr = (uintptr_t)obj;
	exec.buffer_count = 3;
	exec.batch_start_offset = 0;
	exec.batch_len = i * 4;
	exec.DR1 = exec.DR4 = 0;
	exec.num_cliprects = 0;
	exec.cliprects_ptr = 0;
	exec.flags = HAS_BLT_RING(intel_get_drm_devid(fd)) ? I915_EXEC_BLT : 0;
	i915_execbuffer2_set_context_id(exec, 0);
	exec.rsvd2 = 0;

	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &exec);
	while (ret && errno == EBUSY) {
		drmCommandNone(fd, DRM_I915_GEM_THROTTLE);
		ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &exec);
	}
	igt_assert(ret == 0);

	gem_close(fd, handle);
}

static uint32_t
create_bo(int fd, uint32_t val)
{
	uint32_t handle;
	int i;

	handle = gem_create(fd, sizeof(linear));

	/* Fill the BO with dwords starting at val */
	for (i = 0; i < WIDTH*HEIGHT; i++)
		linear[i] = val++;
	gem_write(fd, handle, 0, linear, sizeof(linear));

	return handle;
}

static void
check_bo(int fd, uint32_t handle, uint32_t val)
{
	int i;

	gem_read(fd, handle, 0, linear, sizeof(linear));
	for (i = 0; i < WIDTH*HEIGHT; i++) {
		igt_assert_f(linear[i] == val,
			     "Expected 0x%08x, found 0x%08x "
			     "at offset 0x%08x\n",
			     val, linear[i], i * 4);
		val++;
	}
}

static void run_test(int fd, int count)
{
	uint32_t *handle, *start_val;
	uint32_t start = 0;
	int i;

	handle = malloc(sizeof(uint32_t)*count*2);
	start_val = handle + count;

	for (i = 0; i < count; i++) {
		handle[i] = create_bo(fd, start);
		start_val[i] = start;
		start += 1024 * 1024 / 4;
	}

	igt_info("Verifying initialisation...\n");
	for (i = 0; i < count; i++)
		check_bo(fd, handle[i], start_val[i]);

	igt_info("Cyclic blits, forward...\n");
	for (i = 0; i < count * 4; i++) {
		int src = i % count;
		int dst = (i + 1) % count;

		copy(fd, handle[dst], handle[src]);
		start_val[dst] = start_val[src];
	}
	for (i = 0; i < count; i++)
		check_bo(fd, handle[i], start_val[i]);

	igt_info("Cyclic blits, backward...\n");
	for (i = 0; i < count * 4; i++) {
		int src = (i + 1) % count;
		int dst = i % count;

		copy(fd, handle[dst], handle[src]);
		start_val[dst] = start_val[src];
	}
	for (i = 0; i < count; i++)
		check_bo(fd, handle[i], start_val[i]);

	igt_info("Random blits...\n");
	for (i = 0; i < count * 4; i++) {
		int src = random() % count;
		int dst = random() % count;

		if (src == dst)
			continue;

		copy(fd, handle[dst], handle[src]);
		start_val[dst] = start_val[src];
	}
	for (i = 0; i < count; i++) {
		check_bo(fd, handle[i], start_val[i]);
		gem_close(fd, handle[i]);
	}

	free(handle);
}

int main(int argc, char **argv)
{
	int fd = 0, count = 0;

	igt_skip_on_simulation();

	igt_subtest_init(argc, argv);

	igt_fixture {
		fd = drm_open_any();

		if (argc > 1)
			count = atoi(argv[1]);
		if (count == 0)
			count = 3 * gem_aperture_size(fd) / (1024*1024) / 2;
		else if (count < 2) {
			fprintf(stderr, "count must be >= 2\n");
			return 1;
		}

		if (count > intel_get_total_ram_mb() * 9 / 10) {
			count = intel_get_total_ram_mb() * 9 / 10;
			igt_info("not enough RAM to run test, reducing buffer count\n");
		}

		igt_info("Using %d 1MiB buffers\n", count);
	}

	igt_subtest("normal")
		run_test(fd, count);

	igt_subtest("interruptible") {
		igt_fork_signal_helper();
		run_test(fd, count);
		igt_stop_signal_helper();
	}

	igt_exit();
}
