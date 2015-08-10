/*
 * Copyright © 2015 Intel Corporation
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
 */

#define _GNU_SOURCE /* for RTLD_NEXT */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <i915_drm.h>

#include "intel_aub.h"
#include "intel_chipset.h"

static int (*libc_close)(int fd);
static int (*libc_ioctl)(int fd, unsigned long request, void *argp);

static int drm_fd = -1;
static FILE *file;

#define DRM_MAJOR 226

enum {
	ADD_BO = 0,
	DEL_BO,
	EXEC,
};

struct trace_add_bo {
	uint8_t cmd;
	uint32_t handle;
	uint64_t size;
} __attribute__((packed));
struct trace_del_bo {
	uint8_t cmd;
	uint32_t handle;
}__attribute__((packed));

struct trace_exec {
	uint8_t cmd;
	uint32_t object_count;
	uint64_t flags;
}__attribute__((packed));

struct trace_exec_object {
	uint32_t handle;
	uint32_t relocation_count;
	uint64_t alignment;
	uint64_t flags;
	uint64_t rsvd1;
	uint64_t rsvd2;
}__attribute__((packed));

struct trace_exec_relocation {
	uint32_t target_handle;
	uint32_t delta;
	uint64_t offset;
	uint32_t read_domains;
	uint32_t write_domain;
}__attribute__((packed));

static void __attribute__ ((format(__printf__, 2, 3)))
fail_if(int cond, const char *format, ...)
{
	va_list args;

	if (!cond)
		return;

	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);

	exit(1);
}

static void
trace_exec(int fd, const struct drm_i915_gem_execbuffer2 *execbuffer2)
{
	const struct drm_i915_gem_exec_object2 *exec_objects =
		(struct drm_i915_gem_exec_object2 *)(uintptr_t)execbuffer2->buffers_ptr;

	{
		struct trace_exec t = {
			EXEC, execbuffer2->buffer_count, execbuffer2->flags
		};
		fwrite(&t, sizeof(t), 1, file);
	}

	for (uint32_t i = 0; i < execbuffer2->buffer_count; i++) {
		const struct drm_i915_gem_exec_object2 *obj = &exec_objects[i];
		const struct drm_i915_gem_relocation_entry *relocs =
			(struct drm_i915_gem_relocation_entry *)(uintptr_t)obj->relocs_ptr;
		{
			struct trace_exec_object t = {
				obj->handle,
				obj->relocation_count,
				obj->alignment,
				obj->flags,
				obj->rsvd1,
				obj->rsvd2
			};
			fwrite(&t, sizeof(t), 1, file);
		}
		for (uint32_t j = 0; j < obj->relocation_count; j++) {
			struct trace_exec_relocation t = {
				relocs[j].target_handle,
				relocs[j].delta,
				relocs[j].offset,
				relocs[j].read_domains,
				relocs[j].write_domain,
			};
			fwrite(&t, sizeof(t), 1, file);
		}
	}

	fflush(file);
}

static void
trace_add(uint32_t handle, uint64_t size)
{
	struct trace_add_bo t = { ADD_BO, handle, size };
	fwrite(&t, sizeof(t), 1, file);
}

static void
trace_del(uint32_t handle)
{
	struct trace_del_bo t = { DEL_BO, handle };
	fwrite(&t, sizeof(t), 1, file);
}

int
close(int fd)
{
	if (fd == drm_fd)
		drm_fd = -1;

	return libc_close(fd);
}

int
ioctl(int fd, unsigned long request, ...)
{
	va_list args;
	void *argp;
	int ret;

	va_start(args, request);
	argp = va_arg(args, void *);
	va_end(args);

	if (_IOC_TYPE(request) == DRM_IOCTL_BASE && drm_fd != fd) {
		char filename[80];
		if (file)
			fclose(file);
		sprintf(filename, "/tmp/trace.%d", fd);
		file = fopen(filename, "w+");
		drm_fd = fd;
	}

	if (fd == drm_fd) {
		switch (request) {
		case DRM_IOCTL_I915_GEM_EXECBUFFER: {
			return libc_ioctl(fd, request, argp);
		}

		case DRM_IOCTL_I915_GEM_EXECBUFFER2: {
			trace_exec(fd, argp);
			return libc_ioctl(fd, request, argp);
		}

		case DRM_IOCTL_I915_GEM_CREATE: {
			struct drm_i915_gem_create *create = argp;

			ret = libc_ioctl(fd, request, argp);
			if (ret == 0)
				trace_add(create->handle, create->size);

			return ret;
		}

		case DRM_IOCTL_I915_GEM_USERPTR: {
			struct drm_i915_gem_userptr *userptr = argp;

			ret = libc_ioctl(fd, request, argp);
			if (ret == 0)
				trace_add(userptr->handle, userptr->user_size);
			return ret;
		}

		case DRM_IOCTL_GEM_CLOSE: {
			struct drm_gem_close *close = argp;

			trace_del(close->handle);

			return libc_ioctl(fd, request, argp);
		}

		case DRM_IOCTL_GEM_OPEN: {
			struct drm_gem_open *open = argp;

			ret = libc_ioctl(fd, request, argp);
			if (ret == 0)
				trace_add(open->handle, open->size);

			return ret;
		}

		case DRM_IOCTL_PRIME_FD_TO_HANDLE: {
			struct drm_prime_handle *prime = argp;

			ret = libc_ioctl(fd, request, argp);
			if (ret == 0) {
				off_t size;

				size = lseek(prime->fd, 0, SEEK_END);
				fail_if(size == -1, "failed to get prime bo size\n");
				trace_add(prime->handle, size);
			}

			return ret;
		}

		default:
			return libc_ioctl(fd, request, argp);
		}
	} else {
		return libc_ioctl(fd, request, argp);
	}
}

static void __attribute__ ((constructor))
init(void)
{
	libc_close = dlsym(RTLD_NEXT, "close");
	libc_ioctl = dlsym(RTLD_NEXT, "ioctl");
	fail_if(libc_close == NULL || libc_ioctl == NULL,
		"failed to get libc ioctl or close\n");
}
