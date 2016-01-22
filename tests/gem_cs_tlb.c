/*
 * Copyright © 2011,2012 Intel Corporation
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
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

/*
 * Testcase: Check whether we correctly invalidate the cs tlb
 *
 * Motivated by a strange bug on launchpad where *acth != ipehr, on snb notably
 * where everything should be coherent by default.
 *
 * https://bugs.launchpad.net/ubuntu/+source/xserver-xorg-video-intel/+bug/1063252
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

#include <drm.h>

IGT_TEST_DESCRIPTION("Check whether we correctly invalidate the cs tlb.");

#define LOCAL_I915_EXEC_VEBOX	(4<<0)
#define EXEC_OBJECT_PINNED	(1<<4)
#define BATCH_SIZE (1024*1024)

static bool has_softpin(int fd)
{
	struct drm_i915_getparam gp;
	int val = 0;

	memset(&gp, 0, sizeof(gp));
	gp.param = 37; /* I915_PARAM_HAS_EXEC_SOFTPIN */
	gp.value = &val;

	if (drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
		return 0;

	errno = 0;
	return (val == 1);
}

static void *
mmap_coherent(int fd, uint32_t handle, int size)
{
	int domain;
	void *ptr;

	if (gem_has_llc(fd) || !gem_mmap__has_wc(fd)) {
		domain = I915_GEM_DOMAIN_CPU;
		ptr = gem_mmap__cpu(fd, handle, 0, size, PROT_WRITE);
	} else {
		domain = I915_GEM_DOMAIN_GTT;
		ptr = gem_mmap__wc(fd, handle, 0, size, PROT_WRITE);
	}

	gem_set_domain(fd, handle, domain, domain);
	return ptr;
}

static void run_on_ring(int fd, unsigned ring_id, const char *ring_name)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 execobj;
	struct {
		uint32_t handle;
		uint32_t *batch;
	} obj[2];
	unsigned i;
	char buf[100];

	gem_require_ring(fd, ring_id);
	igt_require(has_softpin(fd));

	for (i = 0; i < 2; i++) {
		obj[i].handle = gem_create(fd, BATCH_SIZE);
		obj[i].batch = mmap_coherent(fd, obj[i].handle, BATCH_SIZE);
		memset(obj[i].batch, 0xff, BATCH_SIZE);
	}

	memset(&execobj, 0, sizeof(execobj));
	execobj.handle = obj[0].handle;
	obj[0].batch[0] = MI_BATCH_BUFFER_END;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&execobj;
	execbuf.buffer_count = 1;
	execbuf.flags = ring_id;

	/* Execute once to allocate a gtt-offset */
	gem_execbuf(fd, &execbuf);
	execobj.flags = EXEC_OBJECT_PINNED;

	sprintf(buf, "Testing %s cs tlb coherency: ", ring_name);
	for (i = 0; i < BATCH_SIZE/8; i++) {
		igt_progress(buf, i, BATCH_SIZE/8);

		execobj.handle = obj[i&1].handle;
		obj[i&1].batch[i*2] = MI_BATCH_BUFFER_END;
		execbuf.batch_start_offset = i*8;

		gem_execbuf(fd, &execbuf);
	}

	for (i = 0; i < 2; i++) {
		gem_close(fd, obj[i].handle);
		munmap(obj[i].batch, BATCH_SIZE);
	}
}

igt_main
{
	int fd = -1;

	igt_skip_on_simulation();

	igt_fixture
		fd = drm_open_driver(DRIVER_INTEL);

	igt_subtest("render")
		run_on_ring(fd, I915_EXEC_RENDER, "render");

	igt_subtest("bsd")
		run_on_ring(fd, I915_EXEC_BSD, "bsd");

	igt_subtest("blt")
		run_on_ring(fd, I915_EXEC_BLT, "blt");

	igt_subtest("vebox")
		run_on_ring(fd, LOCAL_I915_EXEC_VEBOX, "vebox");

	igt_fixture
		close(fd);
}
