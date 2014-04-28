/*
 * Copyright (c) 2014 Intel Corporation
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
 *    Daniel Vetter
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
#include "igt_aux.h"

#define LOCAL_I915_EXEC_VEBOX (4<<0)

struct drm_i915_gem_execbuffer2 execbuf;
struct drm_i915_gem_exec_object2 gem_exec[1];
uint32_t batch[2] = {MI_BATCH_BUFFER_END};
uint32_t handle, devid;
int fd;

igt_main
{
	igt_fixture {
		fd = drm_open_any();

		devid = intel_get_drm_devid(fd);

		handle = gem_create(fd, 4096);
		gem_write(fd, handle, 0, batch, sizeof(batch));

		gem_exec[0].handle = handle;
		gem_exec[0].relocation_count = 0;
		gem_exec[0].relocs_ptr = 0;
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
	}

	igt_subtest("control") {
		igt_assert(drmIoctl(fd,
				    DRM_IOCTL_I915_GEM_EXECBUFFER2,
				    &execbuf) == 0);
		execbuf.flags = I915_EXEC_RENDER;
		igt_assert(drmIoctl(fd,
				    DRM_IOCTL_I915_GEM_EXECBUFFER2,
				    &execbuf) == 0);
	}

#define RUN_FAIL(expected_errno) do { \
		igt_assert(drmIoctl(fd, \
				    DRM_IOCTL_I915_GEM_EXECBUFFER2, \
				    &execbuf) == -1); \
		igt_assert_cmpint(errno, ==, expected_errno); \
	} while(0)

	igt_subtest("no-bsd") {
		igt_require(!gem_has_bsd(fd));
		execbuf.flags = I915_EXEC_BSD;
		RUN_FAIL(EINVAL);
	}
	igt_subtest("no-blt") {
		igt_require(!gem_has_blt(fd));
		execbuf.flags = I915_EXEC_BLT;
		RUN_FAIL(EINVAL);
	}
	igt_subtest("no-vebox") {
		igt_require(!gem_has_vebox(fd));
		execbuf.flags = LOCAL_I915_EXEC_VEBOX;
		RUN_FAIL(EINVAL);
	}
	igt_subtest("invalid-ring") {
		execbuf.flags = I915_EXEC_RING_MASK;
		RUN_FAIL(EINVAL);
	}

	igt_subtest("invalid-ring2") {
		execbuf.flags = LOCAL_I915_EXEC_VEBOX+1;
		RUN_FAIL(EINVAL);
	}

	igt_subtest("rel-constants-invalid-ring") {
		igt_require(gem_has_bsd(fd));
		execbuf.flags = I915_EXEC_BSD | I915_EXEC_CONSTANTS_ABSOLUTE;
		RUN_FAIL(EINVAL);
	}

	igt_subtest("rel-constants-invalid-rel-gen5") {
		igt_require(intel_gen(devid) > 5);
		execbuf.flags = I915_EXEC_RENDER | I915_EXEC_CONSTANTS_REL_SURFACE;
		RUN_FAIL(EINVAL);
	}

	igt_subtest("rel-constants-invalid") {
		execbuf.flags = I915_EXEC_RENDER | (I915_EXEC_CONSTANTS_REL_SURFACE+(1<<6));
		RUN_FAIL(EINVAL);
	}

	igt_subtest("sol-reset-invalid") {
		igt_require(gem_has_bsd(fd));
		execbuf.flags = I915_EXEC_BSD | I915_EXEC_GEN7_SOL_RESET;
		RUN_FAIL(EINVAL);
	}

	igt_subtest("sol-reset-not-gen7") {
		igt_require(intel_gen(devid) != 7);
		execbuf.flags = I915_EXEC_RENDER | I915_EXEC_GEN7_SOL_RESET;
		RUN_FAIL(EINVAL);
	}

	igt_subtest("secure-non-root") {
		igt_fork(child, 1) {
			igt_drop_root();

			execbuf.flags = I915_EXEC_RENDER | I915_EXEC_SECURE;
			RUN_FAIL(EPERM);
		}

		igt_waitchildren();
	}

	igt_subtest("secure-non-master") {
		do_or_die(drmDropMaster(fd));
		execbuf.flags = I915_EXEC_RENDER | I915_EXEC_SECURE;
		RUN_FAIL(EPERM);
		do_or_die(drmSetMaster(fd));
		igt_assert(drmIoctl(fd,
				    DRM_IOCTL_I915_GEM_EXECBUFFER2,
				    &execbuf) == 0);
	}

	/* HANDLE_LUT and NO_RELOC are already exercised by gem_exec_lut_handle */

	igt_subtest("invalid-flag") {
		execbuf.flags = I915_EXEC_RENDER | (I915_EXEC_HANDLE_LUT << 1);
		RUN_FAIL(EINVAL);
	}

	/* rsvd1 aka context id is already exercised  by gem_ctx_bad_exec */

	igt_subtest("cliprects-invalid") {
		igt_require(intel_gen(devid) >= 5);
		execbuf.flags = 0;
		execbuf.num_cliprects = 1;
		RUN_FAIL(EINVAL);
		execbuf.num_cliprects = 0;
	}

#define DIRT(name) \
	igt_subtest(#name "-dirt") { \
		execbuf.flags = 0; \
		execbuf.name = 1; \
		RUN_FAIL(EINVAL); \
		execbuf.name = 0; \
	}

	DIRT(rsvd2);
	DIRT(cliprects_ptr);
	DIRT(DR1);
	DIRT(DR4);
#undef DIRT

#undef RUN_FAIL

	igt_fixture {
		gem_close(fd, handle);

		close(fd);
	}
}
