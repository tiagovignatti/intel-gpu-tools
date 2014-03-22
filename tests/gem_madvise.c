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
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>

#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"

#define OBJECT_SIZE (1024*1024)

/* Testcase: checks that the kernel reports EFAULT when trying to use purged bo
 *
 */

static jmp_buf jmp;

static void sigtrap(int sig)
{
	longjmp(jmp, sig);
}

static void
dontneed_before_mmap(void)
{
	int fd = drm_open_any();
	uint32_t handle;
	char *ptr;

	handle = gem_create(fd, OBJECT_SIZE);
	gem_madvise(fd, handle, I915_MADV_DONTNEED);
	ptr = gem_mmap(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);
	igt_assert(ptr == NULL);
	igt_assert(errno == EFAULT);
	close(fd);
}

static void
dontneed_after_mmap(void)
{
	int fd = drm_open_any();
	uint32_t handle;
	char *ptr;

	handle = gem_create(fd, OBJECT_SIZE);
	ptr = gem_mmap(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);
	igt_assert(ptr != NULL);
	gem_madvise(fd, handle, I915_MADV_DONTNEED);
	close(fd);

	signal(SIGBUS, sigtrap);
	switch (setjmp(jmp)) {
	case SIGBUS:
		break;
	case 0:
		*ptr = 0;
	default:
		igt_assert(!"reached");
		break;
	}
	munmap(ptr, OBJECT_SIZE);
	signal(SIGBUS, SIG_DFL);
}

static void
dontneed_before_pwrite(void)
{
	int fd = drm_open_any();
	uint32_t buf[] = { MI_BATCH_BUFFER_END, 0 };
	struct drm_i915_gem_pwrite gem_pwrite;

	gem_pwrite.handle = gem_create(fd, OBJECT_SIZE);
	gem_pwrite.offset = 0;
	gem_pwrite.size = sizeof(buf);
	gem_pwrite.data_ptr = (uintptr_t)buf;
	gem_madvise(fd, gem_pwrite.handle, I915_MADV_DONTNEED);

	igt_assert(drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite));
	igt_assert(errno == EFAULT);

	gem_close(fd, gem_pwrite.handle);
	close(fd);
}

static void
dontneed_before_exec(void)
{
	int fd = drm_open_any();
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec;
	uint32_t buf[] = { MI_BATCH_BUFFER_END, 0 };

	memset(&execbuf, 0, sizeof(execbuf));
	memset(&exec, 0, sizeof(exec));

	exec.handle = gem_create(fd, OBJECT_SIZE);
	gem_write(fd, exec.handle, 0, buf, sizeof(buf));
	gem_madvise(fd, exec.handle, I915_MADV_DONTNEED);

	execbuf.buffers_ptr = (uintptr_t)&exec;
	execbuf.buffer_count = 1;
	gem_execbuf(fd, &execbuf);

	gem_close(fd, exec.handle);
	close(fd);
}

igt_main
{
	igt_skip_on_simulation();

	igt_subtest("dontneed-before-mmap")
		dontneed_before_mmap();

	igt_subtest("dontneed-after-mmap")
		dontneed_after_mmap();

	igt_subtest("dontneed-before-pwrite")
		dontneed_before_pwrite();

	igt_subtest("dontneed-before-exec")
		dontneed_before_exec();
}
