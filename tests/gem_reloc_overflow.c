/*
 * Copyright © 2013 Google
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
 *    Kees Cook <keescook@chromium.org>
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <malloc.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_gpu_tools.h"

/*
 * Testcase: Kernel relocation overflows are caught.
 */

int main(int argc, char *argv[])
{
	int fd, i, entries, num;
	size_t reloc_size;
	size_t total_actual = 0;
	unsigned int total_unsigned = 0;
	int total_signed = 0;
	uint32_t *handles;
	struct drm_i915_gem_relocation_entry *reloc;
	struct drm_i915_gem_exec_object2 *execobjs;
	struct drm_i915_gem_execbuffer2 execbuf = { 0 };

	fd = drm_open_any();

	/* Create giant reloc buffer area. */
	num = 257;
	entries = ((1ULL << 32) / (num - 1));
	reloc_size = entries * sizeof(struct drm_i915_gem_relocation_entry);
	reloc = mmap(NULL, reloc_size, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANON, -1, 0);
	if (reloc == MAP_FAILED) {
		perror("mmap");
		return errno;
	}

	/* Allocate the handles we'll need to wrap. */
	handles = calloc(num, sizeof(*handles));
	for (i = 0; i < num; i++) {
		struct drm_i915_gem_create create_args = { 0 };
		create_args.size = 0x1000;
		if (ioctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create_args)) {
			perror("DRM_IOCTL_I915_GEM_CREATE");
			return errno;
		}
		handles[i] = create_args.handle;
	}

	/* Create relocation objects. */
	execobjs = calloc(num, sizeof(*execobjs));
	execbuf.buffers_ptr = (uintptr_t)execobjs;

	/* Attempt unmapped single entry. */
	execobjs[0].relocation_count = 1;
	execobjs[0].relocs_ptr = 0;
	execbuf.buffer_count = 1;

	errno = 0;
	ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
	if (errno != EFAULT) {
		perror("DRM_IOCTL_I915_GEM_EXECBUFFER2, invalid address");
		abort();
	}

	/* Attempt single overflowed entry. */
	execobjs[0].relocation_count = (1 << 31);
	execobjs[0].relocs_ptr = (uintptr_t)reloc;
	execbuf.buffer_count = 1;

	errno = 0;
	ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
	if (errno != EINVAL) {
		perror("DRM_IOCTL_I915_GEM_EXECBUFFER2, single overflow");
		abort();
	}

	/* Attempt wrapped overflow entries. */
	for (i = 0; i < num; i++) {
		struct drm_i915_gem_exec_object2 *obj = &execobjs[i];
		obj->handle = handles[i];

		if (i == num - 1) {
			/* Wraps to 1 on last count. */
			obj->relocation_count = 1 - total_unsigned;
			obj->relocs_ptr = (uintptr_t)reloc;
		} else {
			obj->relocation_count = entries;
			obj->relocs_ptr = (uintptr_t)reloc;
		}

		total_unsigned += obj->relocation_count;
		total_signed += obj->relocation_count;
		total_actual += obj->relocation_count;
	}
	execbuf.buffer_count = num;

	errno = 0;
	ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
	if (errno != EINVAL) {
		/* ENOENT means we're subject to wrapping overflow since
		 * processing has continued into validating buffer contents.
		 */
		perror("DRM_IOCTL_I915_GEM_EXECBUFFER2, wrap overflow");
		abort();
	}

	if (close(fd)) {
		perror("close");
		return errno;
	}

	return 0;
}
