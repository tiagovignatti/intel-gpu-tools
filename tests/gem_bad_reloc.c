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
#define BIAS (256*1024)

/* Simulates SNA behaviour using negative self-relocations for
 * STATE_BASE_ADDRESS command packets. If they wrap around (to values greater
 * than the total size of the GTT), the GPU will hang.
 * See https://bugs.freedesktop.org/show_bug.cgi?id=78533
 */
static void negative_reloc(int fd, unsigned engine, unsigned flags)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	struct drm_i915_gem_relocation_entry reloc[1000];
	uint64_t gtt_max = gem_aperture_size(fd);
	uint32_t bbe = MI_BATCH_BUFFER_END;
	uint64_t *offsets;
	int i;

	gem_require_ring(fd, engine);
	igt_require(intel_gen(intel_get_drm_devid(fd)) >= 7);

	memset(&obj, 0, sizeof(obj));
	obj.handle = gem_create(fd, 8192);
	gem_write(fd, obj.handle, 0, &bbe, sizeof(bbe));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&obj;
	execbuf.buffer_count = 1;
	execbuf.flags = engine | (flags & USE_LUT);
	igt_require(__gem_execbuf(fd, &execbuf) == 0);

	igt_info("Found offset %lld for 4k batch\n", (long long)obj.offset);
	/*
	 * Ideally we'd like to be able to control where the kernel is going to
	 * place the buffer. We don't SKIP here because it causes the test
	 * to "randomly" flip-flop between the SKIP and PASS states.
	 */
	if (obj.offset < BIAS) {
		igt_info("Offset is below BIAS, not testing anything\n");
		return;
	}

	memset(reloc, 0, sizeof(reloc));
	for (i = 0; i < ARRAY_SIZE(reloc); i++) {
		reloc[i].offset = 8 + 8*i;
		reloc[i].delta = -BIAS*i/1024;
		reloc[i].presumed_offset = -1;
		reloc[i].target_handle = flags & USE_LUT ? 0 : obj.handle;
		reloc[i].read_domains = I915_GEM_DOMAIN_COMMAND;
	}
	obj.relocation_count = i;
	obj.relocs_ptr = (uintptr_t)reloc;
	gem_execbuf(fd, &execbuf);

	igt_info("Batch is now at offset %#llx, max GTT %#llx\n",
		 (long long)obj.offset, (long long)gtt_max);

	offsets = gem_mmap__cpu(fd, obj.handle, 0, 8192, PROT_READ);
	gem_set_domain(fd, obj.handle, I915_GEM_DOMAIN_CPU, 0);
	gem_close(fd, obj.handle);

	for (i = 0; i < ARRAY_SIZE(reloc); i++)
		igt_assert_f(offsets[1 + i] < gtt_max,
			     "Offset[%d]=%#llx, expected less than %#llx\n",
			     i, (long long)offsets[i+i], (long long)gtt_max);
	munmap(offsets, 8192);
}

static void negative_reloc_blt(int fd)
{
	const int gen = intel_gen(intel_get_drm_devid(fd));
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj[1024][2];
	struct drm_i915_gem_relocation_entry reloc;
	uint32_t buf[1024], *b;
	int i;

	memset(&reloc, 0, sizeof(reloc));
	reloc.offset = 4 * sizeof(uint32_t);
	reloc.presumed_offset = ~0ULL;
	reloc.delta = -4096;
	reloc.target_handle = 0;
	reloc.read_domains = I915_GEM_DOMAIN_RENDER;
	reloc.write_domain = I915_GEM_DOMAIN_RENDER;

	for (i = 0; i < 1024; i++) {
		memset(obj[i], 0, sizeof(obj[i]));

		obj[i][0].handle = gem_create(fd, 4096);
		obj[i][0].flags = EXEC_OBJECT_NEEDS_FENCE;

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

		obj[i][1].handle = gem_create(fd, 4096);
		gem_write(fd, obj[i][1].handle, 0, buf, (b - buf) * sizeof(uint32_t));
		obj[i][1].relocation_count = 1;
		obj[i][1].relocs_ptr = (uintptr_t)&reloc;
	}

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffer_count = 2;
	execbuf.batch_len = (b - buf) * sizeof(uint32_t);
	execbuf.flags = USE_LUT;
	if (gen >= 6)
		execbuf.flags |= I915_EXEC_BLT;

	for (i = 0; i < 1024; i++) {
		execbuf.buffers_ptr = (uintptr_t)obj[i];
		gem_execbuf(fd, &execbuf);
	}

	for (i = 1024; i--;) {
		gem_read(fd, obj[i][0].handle,
			 i*sizeof(uint32_t), buf + i, sizeof(uint32_t));
		gem_close(fd, obj[i][0].handle);
		gem_close(fd, obj[i][1].handle);
	}

	if (0) {
		for (i = 0; i < 1024; i += 8)
			igt_info("%08x %08x %08x %08x %08x %08x %08x %08x\n",
				 buf[i + 0], buf[i + 1], buf[i + 2], buf[i + 3],
				 buf[i + 4], buf[i + 5], buf[i + 6], buf[i + 7]);
	}
	for (i = 0; i < 1024; i++)
		igt_assert_eq(buf[i], 0xc0ffee ^ i);
}

igt_main
{
	const struct intel_execution_engine *e;
	int fd = -1;

	igt_fixture
		fd = drm_open_driver(DRIVER_INTEL);

	for (e = intel_execution_engines; e->name; e++) {
		igt_subtest_f("negative-reloc-%s", e->name)
			negative_reloc(fd, e->exec_id | e->flags, 0);

		igt_subtest_f("negative-reloc-lut-%s", e->name)
			negative_reloc(fd, e->exec_id | e->flags, USE_LUT);
	}

	igt_subtest("negative-reloc-bltcopy")
		negative_reloc_blt(fd);

	igt_fixture
		close(fd);
}
