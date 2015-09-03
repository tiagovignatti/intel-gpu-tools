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

#include "igt.h"
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

IGT_TEST_DESCRIPTION("Simulates SNA behaviour using negative self-relocations"
		     " for STATE_BASE_ADDRESS command packets.");

#define USE_LUT (1 << 12)

static uint64_t get_page_table_size(int fd)
{
	struct drm_i915_getparam gp;
	int val = 0;

	memset(&gp, 0, sizeof(gp));
	gp.param = 18; /* HAS_ALIASING_PPGTT */
	gp.value = &val;

	if (drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
		return 0;
	errno = 0;

	switch (val) {
	case 0:
	case 1:
		return gem_aperture_size(fd);
	case 2:
		return 1ULL << 32;
	case 3:
		return 1ULL << 48;
	}

	return 0;
}

/* Simulates SNA behaviour using negative self-relocations for
 * STATE_BASE_ADDRESS command packets. If they wrap around (to values greater
 * than the total size of the GTT), the GPU will hang.
 * See https://bugs.freedesktop.org/show_bug.cgi?id=78533
 */
static int negative_reloc(int fd, unsigned flags)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[2];
	struct drm_i915_gem_relocation_entry gem_reloc[1000];
	uint64_t gtt_max = get_page_table_size(fd);
	uint32_t buf[1024] = {MI_BATCH_BUFFER_END};
	int i;

#define BIAS (256*1024)

	igt_require(intel_gen(intel_get_drm_devid(fd)) >= 7);

	memset(gem_exec, 0, sizeof(gem_exec));
	gem_exec[0].handle = gem_create(fd, 4096);
	gem_write(fd, gem_exec[0].handle, 0, buf, 8);

	gem_reloc[0].offset = 1024;
	gem_reloc[0].delta = 0;
	gem_reloc[0].target_handle = gem_exec[0].handle;
	gem_reloc[0].read_domains = I915_GEM_DOMAIN_COMMAND;

	gem_exec[1].handle = gem_create(fd, 4096);
	gem_write(fd, gem_exec[1].handle, 0, buf, 8);
	gem_exec[1].relocation_count = 1;
	gem_exec[1].relocs_ptr = (uintptr_t)gem_reloc;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 2;
	execbuf.batch_len = 8;

	do_or_die(drmIoctl(fd,
			   DRM_IOCTL_I915_GEM_EXECBUFFER2,
			   &execbuf));
	gem_close(fd, gem_exec[1].handle);

	igt_info("Found offset %lld for 4k batch\n", (long long)gem_exec[0].offset);
	/*
	 * Ideally we'd like to be able to control where the kernel is going to
	 * place the buffer. We don't SKIP here because it causes the test
	 * to "randomly" flip-flop between the SKIP and PASS states.
	 */
	if (gem_exec[0].offset < BIAS) {
		igt_info("Offset is below BIAS, not testing anything\n");
		return 0;
	}

	memset(gem_reloc, 0, sizeof(gem_reloc));
	for (i = 0; i < sizeof(gem_reloc)/sizeof(gem_reloc[0]); i++) {
		gem_reloc[i].offset = 8 + 4*i;
		gem_reloc[i].delta = -BIAS*i/1024;
		gem_reloc[i].target_handle = flags & USE_LUT ? 0 : gem_exec[0].handle;
		gem_reloc[i].read_domains = I915_GEM_DOMAIN_COMMAND;
	}

	gem_exec[0].relocation_count = sizeof(gem_reloc)/sizeof(gem_reloc[0]);
	gem_exec[0].relocs_ptr = (uintptr_t)gem_reloc;

	execbuf.buffer_count = 1;
	execbuf.flags = flags & USE_LUT;
	do_or_die(drmIoctl(fd,
			   DRM_IOCTL_I915_GEM_EXECBUFFER2,
			   &execbuf));

	igt_info("Batch is now at offset %lld\n", (long long)gem_exec[0].offset);

	gem_read(fd, gem_exec[0].handle, 0, buf, sizeof(buf));
	gem_close(fd, gem_exec[0].handle);

	for (i = 0; i < sizeof(gem_reloc)/sizeof(gem_reloc[0]); i++)
		igt_assert(buf[2 + i] < gtt_max);

	return 0;
}

static int negative_reloc_blt(int fd)
{
	const int gen = intel_gen(intel_get_drm_devid(fd));
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1024][2];
	struct drm_i915_gem_relocation_entry gem_reloc;
	uint32_t buf[1024], *b;
	int i;

	memset(&gem_reloc, 0, sizeof(gem_reloc));
	gem_reloc.offset = 4 * sizeof(uint32_t);
	gem_reloc.presumed_offset = ~0ULL;
	gem_reloc.delta = -4096;
	gem_reloc.target_handle = 0;
	gem_reloc.read_domains = I915_GEM_DOMAIN_RENDER;
	gem_reloc.write_domain = I915_GEM_DOMAIN_RENDER;

	for (i = 0; i < 1024; i++) {
		memset(gem_exec[i], 0, sizeof(gem_exec[i]));

		gem_exec[i][0].handle = gem_create(fd, 4096);
		gem_exec[i][0].flags = EXEC_OBJECT_NEEDS_FENCE;

		b = buf;
		*b++ = XY_COLOR_BLT_CMD_NOLEN |
			((gen >= 8) ? 5 : 4) |
			COLOR_BLT_WRITE_ALPHA | XY_COLOR_BLT_WRITE_RGB;
		*b++ = 0xf0 << 16 | 1 << 25 | 1 << 24 | 4096;
		*b++ = 1 << 16 | 0;
		*b++ = 2 << 16 | 1024;
		*b++ = ~0;
		if (gen >= 8)
			*b++ = ~0;
		*b++ = 0xc0ffee ^ i;
		*b++ = MI_BATCH_BUFFER_END;
		if ((b - buf) & 1)
			*b++ = 0;

		gem_exec[i][1].handle = gem_create(fd, 4096);
		gem_write(fd, gem_exec[i][1].handle, 0, buf, (b - buf) * sizeof(uint32_t));
		gem_exec[i][1].relocation_count = 1;
		gem_exec[i][1].relocs_ptr = (uintptr_t)&gem_reloc;
	}

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffer_count = 2;
	execbuf.batch_len = (b - buf) * sizeof(uint32_t);
	execbuf.flags = USE_LUT;
	if (gen >= 6)
		execbuf.flags |= I915_EXEC_BLT;

	for (i = 0; i < 1024; i++) {
		execbuf.buffers_ptr = (uintptr_t)gem_exec[i];
		gem_execbuf(fd, &execbuf);
	}

	for (i = 1024; i--;) {
		gem_read(fd, gem_exec[i][0].handle,
			 i*sizeof(uint32_t), buf + i, sizeof(uint32_t));
		gem_close(fd, gem_exec[i][0].handle);
		gem_close(fd, gem_exec[i][1].handle);
	}

	if (0) {
		for (i = 0; i < 1024; i += 8)
			igt_info("%08x %08x %08x %08x %08x %08x %08x %08x\n",
				 buf[i + 0], buf[i + 1], buf[i + 2], buf[i + 3],
				 buf[i + 4], buf[i + 5], buf[i + 6], buf[i + 7]);
	}
	for (i = 0; i < 1024; i++)
		igt_assert_eq(buf[i], 0xc0ffee ^ i);

	return 0;
}

int fd;

igt_main
{
	igt_fixture {
		fd = drm_open_any();
	}

	igt_subtest("negative-reloc")
		negative_reloc(fd, 0);

	igt_subtest("negative-reloc-lut")
		negative_reloc(fd, USE_LUT);

	igt_subtest("negative-reloc-blt")
		negative_reloc_blt(fd);

	igt_fixture {
		close(fd);
	}
}
