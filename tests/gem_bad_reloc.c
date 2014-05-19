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
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_io.h"
#include "intel_chipset.h"

#define USE_LUT (1 << 12)

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
	uint64_t gtt_max = gem_aperture_size(fd);
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

	printf("Found offset %ld for 4k batch\n", (long)gem_exec[0].offset);
	igt_require(gem_exec[0].offset < BIAS);

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

	printf("Batch is now at offset %ld\n", (long)gem_exec[0].offset);

	gem_read(fd, gem_exec[0].handle, 0, buf, sizeof(buf));
	gem_close(fd, gem_exec[0].handle);

	for (i = 0; i < sizeof(gem_reloc)/sizeof(gem_reloc[0]); i++)
		igt_assert(buf[2 + i] < gtt_max);

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

	igt_fixture {
		close(fd);
	}
}
