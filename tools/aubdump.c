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
static char *filename;
static FILE *file;
static int gen = 0;
static int verbose = 0;
static const uint32_t gtt_size = 0x10000;
static bool device_override;
static uint32_t device;

#define MAX_BO_COUNT 64 * 1024

struct bo {
	uint32_t size;
	uint64_t offset;
	void *map;
};

static struct bo *bos;

#define DRM_MAJOR 226

#ifndef DRM_I915_GEM_USERPTR

#define DRM_I915_GEM_USERPTR		0x33
#define DRM_IOCTL_I915_GEM_USERPTR	DRM_IOWR (DRM_COMMAND_BASE + DRM_I915_GEM_USERPTR, struct drm_i915_gem_userptr)

struct drm_i915_gem_userptr {
	__u64 user_ptr;
	__u64 user_size;
	__u32 flags;
#define I915_USERPTR_READ_ONLY 0x1
#define I915_USERPTR_UNSYNCHRONIZED 0x80000000
	/**
	 * Returned handle for the object.
	 *
	 * Object handles are nonzero.
	 */
	__u32 handle;
};

#endif

/* We set bit 0 in the map pointer for userptr BOs so we know not to
 * munmap them on DRM_IOCTL_GEM_CLOSE.
 */
#define USERPTR_FLAG 1
#define IS_USERPTR(p) ((uintptr_t) (p) & USERPTR_FLAG)
#define GET_PTR(p) ( (void *) ((uintptr_t) p & ~(uintptr_t) 1) )

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

static inline uint32_t
align_u32(uint32_t v, uint32_t a)
{
	return (v + a - 1) & ~(a - 1);
}

static inline uint64_t
align_u64(uint64_t v, uint64_t a)
{
	return (v + a - 1) & ~(a - 1);
}

static void
dword_out(uint32_t data)
{
	fwrite(&data, 1, 4, file);
}

static void
data_out(const void *data, size_t size)
{
	fwrite(data, 1, size, file);
}

static void
write_header(void)
{
	uint32_t entry = 0x200003;

	/* Start with a (required) version packet. */
	dword_out(CMD_AUB_HEADER | (13 - 2));
	dword_out((4 << AUB_HEADER_MAJOR_SHIFT) |
		  (0 << AUB_HEADER_MINOR_SHIFT));
	for (int i = 0; i < 8; i++)
		dword_out(0); /* app name */

	dword_out(0); /* timestamp */
	dword_out(0); /* timestamp */
	dword_out(0); /* comment len */

	/* Set up the GTT. The max we can handle is 256M */
	dword_out(CMD_AUB_TRACE_HEADER_BLOCK | ((gen >= 8 ? 6 : 5) - 2));
	dword_out(AUB_TRACE_MEMTYPE_GTT_ENTRY |
		  AUB_TRACE_TYPE_NOTYPE | AUB_TRACE_OP_DATA_WRITE);
	dword_out(0); /* subtype */
	dword_out(0); /* offset */
	dword_out(gtt_size); /* size */
	if (gen >= 8)
		dword_out(0);
	for (uint32_t i = 0; i < gtt_size; i += 4, entry += 0x1000)
		dword_out(entry);
}

/**
 * Break up large objects into multiple writes.  Otherwise a 128kb VBO
 * would overflow the 16 bits of size field in the packet header and
 * everything goes badly after that.
 */
static void
aub_write_trace_block(uint32_t type, void *virtual, uint32_t size, uint64_t gtt_offset)
{
	uint32_t block_size;
	uint32_t subtype = 0;
	static const char null_block[8 * 4096];

	for (uint32_t offset = 0; offset < size; offset += block_size) {
		block_size = size - offset;

		if (block_size > 8 * 4096)
			block_size = 8 * 4096;

		dword_out(CMD_AUB_TRACE_HEADER_BLOCK |
			  ((gen >= 8 ? 6 : 5) - 2));
		dword_out(AUB_TRACE_MEMTYPE_GTT |
			  type | AUB_TRACE_OP_DATA_WRITE);
		dword_out(subtype);
		dword_out(gtt_offset + offset);
		dword_out(align_u32(block_size, 4));
		if (gen >= 8)
			dword_out((gtt_offset + offset) >> 32);

		if (virtual)
			data_out(GET_PTR(virtual) + offset, block_size);
		else
			data_out(null_block, block_size);

		/* Pad to a multiple of 4 bytes. */
		data_out(null_block, -block_size & 3);
	}
}

static void
aub_dump_ringbuffer(uint64_t batch_offset, uint64_t offset, int ring_flag)
{
	uint32_t ringbuffer[4096];
	int ring = AUB_TRACE_TYPE_RING_PRB0; /* The default ring */
	int ring_count = 0;

	if (ring_flag == I915_EXEC_BSD)
		ring = AUB_TRACE_TYPE_RING_PRB1;
	else if (ring_flag == I915_EXEC_BLT)
		ring = AUB_TRACE_TYPE_RING_PRB2;

	/* Make a ring buffer to execute our batchbuffer. */
	memset(ringbuffer, 0, sizeof(ringbuffer));
	if (gen >= 8) {
		ringbuffer[ring_count++] = AUB_MI_BATCH_BUFFER_START | (3 - 2);
		ringbuffer[ring_count++] = batch_offset;
		ringbuffer[ring_count++] = batch_offset >> 32;
	} else {
		ringbuffer[ring_count++] = AUB_MI_BATCH_BUFFER_START;
		ringbuffer[ring_count++] = batch_offset;
	}

	/* Write out the ring.  This appears to trigger execution of
	 * the ring in the simulator.
	 */
	dword_out(CMD_AUB_TRACE_HEADER_BLOCK |
		  ((gen >= 8 ? 6 : 5) - 2));
	dword_out(AUB_TRACE_MEMTYPE_GTT | ring | AUB_TRACE_OP_COMMAND_WRITE);
	dword_out(0); /* general/surface subtype */
	dword_out(offset);
	dword_out(ring_count * 4);
	if (gen >= 8)
		dword_out(offset >> 32);

	data_out(ringbuffer, ring_count * 4);
}

static void *
relocate_bo(struct bo *bo, const struct drm_i915_gem_execbuffer2 *execbuffer2,
	    const struct drm_i915_gem_exec_object2 *obj)
{
	const struct drm_i915_gem_exec_object2 *exec_objects =
		(struct drm_i915_gem_exec_object2 *) (uintptr_t) execbuffer2->buffers_ptr;
	const struct drm_i915_gem_relocation_entry *relocs =
		(const struct drm_i915_gem_relocation_entry *) (uintptr_t) obj->relocs_ptr;
	void *relocated;
	uint32_t *dw;
	int handle;

	relocated = malloc(bo->size);
	fail_if(relocated == NULL, "intel_aubdump: out of memory\n");
	memcpy(relocated, GET_PTR(bo->map), bo->size);
	for (size_t i = 0; i < obj->relocation_count; i++) {
		fail_if(relocs[i].offset >= bo->size, "intel_aubdump: reloc outside bo\n");

		if (execbuffer2->flags & I915_EXEC_HANDLE_LUT)
			handle = exec_objects[relocs[i].target_handle].handle;
		else
			handle = relocs[i].target_handle;

		dw = relocated + relocs[i].offset;
		*dw = bos[handle].offset + relocs[i].delta;
	}

	return relocated;
}

static int
gem_ioctl(int fd, unsigned long request, void *argp)
{
	int ret;

	do {
		ret = libc_ioctl(fd, request, argp);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));

	return ret;
}

static void *
gem_mmap(int fd, uint32_t handle, uint64_t offset, uint64_t size)
{
	struct drm_i915_gem_mmap mmap = {
		.handle = handle,
		.offset = offset,
		.size = size
	};

	if (gem_ioctl(fd, DRM_IOCTL_I915_GEM_MMAP, &mmap) == -1)
		return MAP_FAILED;

	return (void *)(uintptr_t) mmap.addr_ptr;
}

static int
gem_get_param(int fd, uint32_t param)
{
	int value;
	drm_i915_getparam_t gp = {
		.param = param,
		.value = &value
	};

	if (gem_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp) == -1)
		return 0;

	return value;
}

static void
dump_execbuffer2(int fd, struct drm_i915_gem_execbuffer2 *execbuffer2)
{
	struct drm_i915_gem_exec_object2 *exec_objects =
		(struct drm_i915_gem_exec_object2 *) (uintptr_t) execbuffer2->buffers_ptr;
	uint32_t ring_flag = 0;
	uint32_t offset = gtt_size;
	struct drm_i915_gem_exec_object2 *obj;
	struct bo *bo, *batch_bo;
	void *data;

	/* We can't do this at open time as we're not yet authenticated. */
	if (device == 0) {
		device = gem_get_param(fd, I915_PARAM_CHIPSET_ID);
		fail_if(device == 0 || gen == -1, "failed to identify chipset\n");
	}
	if (gen == 0) {
		gen = intel_gen(device);

		if (verbose)
			printf("[intel_aubdump: running, "
			       "output file %s, chipset id 0x%04x, gen %d]\n",
			       filename, device, gen);
	}

	for (uint32_t i = 0; i < execbuffer2->buffer_count; i++) {
		obj = &exec_objects[i];
		bo = &bos[obj->handle];

		bo->offset = offset;
		offset = align_u32(offset + bo->size + 4095, 4096);

		if (bo->map == NULL)
			bo->map = gem_mmap(fd, obj->handle, 0, bo->size);
		fail_if(bo->map == MAP_FAILED, "intel_aubdump: bo mmap failed\n");
	}

	batch_bo = &bos[exec_objects[execbuffer2->buffer_count - 1].handle];
	for (uint32_t i = 0; i < execbuffer2->buffer_count; i++) {
		obj = &exec_objects[i];
		bo = &bos[obj->handle];

		if (obj->relocation_count > 0)
			data = relocate_bo(bo, execbuffer2, obj);
		else
			data = bo->map;

		if (bo == batch_bo) {
			aub_write_trace_block(AUB_TRACE_TYPE_BATCH,
					      data, bo->size, bo->offset);
		} else {
			aub_write_trace_block(AUB_TRACE_TYPE_NOTYPE,
					      data, bo->size, bo->offset);
		}
		if (data != bo->map)
			free(data);
	}

	/* Dump ring buffer */
	aub_dump_ringbuffer(batch_bo->offset + execbuffer2->batch_start_offset,
			    offset, ring_flag);

	fflush(file);
}

static void
add_new_bo(int handle, uint64_t size, void *map)
{
	struct bo *bo = &bos[handle];

	fail_if(handle >= MAX_BO_COUNT, "intel_aubdump: bo handle out of range\n");

	bo->size = size;
	bo->map = map;
}

static void
remove_bo(int handle)
{
	struct bo *bo = &bos[handle];

	if (bo->map && !IS_USERPTR(bo->map))
		munmap(bo->map, bo->size);
	bo->map = NULL;
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
	struct stat buf;

	va_start(args, request);
	argp = va_arg(args, void *);
	va_end(args);

	if (_IOC_TYPE(request) == DRM_IOCTL_BASE &&
	    drm_fd != fd && fstat(fd, &buf) == 0 &&
	    (buf.st_mode & S_IFMT) == S_IFCHR && major(buf.st_rdev) == DRM_MAJOR) {
		drm_fd = fd;
		if (verbose)
			printf("[intel_aubdump: intercept drm ioctl on fd %d]\n", fd);
	}

	if (fd == drm_fd) {
		switch (request) {
		case DRM_IOCTL_I915_GETPARAM: {
			struct drm_i915_getparam *getparam = argp;

			if (device_override && getparam->param == I915_PARAM_CHIPSET_ID) {
				*getparam->value = device;
				return 0;
			}

			ret = libc_ioctl(fd, request, argp);

			/* If the application looks up chipset_id
			 * (they typically do), we'll piggy-back on
			 * their ioctl and store the id for later
			 * use. */
			if (getparam->param == I915_PARAM_CHIPSET_ID)
				device = *getparam->value;

			return ret;
		}

		case DRM_IOCTL_I915_GEM_EXECBUFFER: {
			static bool once;
			if (!once) {
				fprintf(stderr, "intel_aubdump: "
					"application uses DRM_IOCTL_I915_GEM_EXECBUFFER, not handled\n");
				once = true;
			}
			return libc_ioctl(fd, request, argp);
		}

		case DRM_IOCTL_I915_GEM_EXECBUFFER2: {
			dump_execbuffer2(fd, argp);
			if (device_override)
				return 0;

			return libc_ioctl(fd, request, argp);
		}

		case DRM_IOCTL_I915_GEM_CREATE: {
			struct drm_i915_gem_create *create = argp;

			ret = libc_ioctl(fd, request, argp);
			if (ret == 0)
				add_new_bo(create->handle, create->size, NULL);

			return ret;
		}

		case DRM_IOCTL_I915_GEM_USERPTR: {
			struct drm_i915_gem_userptr *userptr = argp;

			ret = libc_ioctl(fd, request, argp);
			if (ret == 0)
				add_new_bo(userptr->handle, userptr->user_size,
					   (void *) (uintptr_t) (userptr->user_ptr | USERPTR_FLAG));
			return ret;
		}

		case DRM_IOCTL_GEM_CLOSE: {
			struct drm_gem_close *close = argp;

			remove_bo(close->handle);

			return libc_ioctl(fd, request, argp);
		}

		case DRM_IOCTL_GEM_OPEN: {
			struct drm_gem_open *open = argp;

			ret = libc_ioctl(fd, request, argp);
			if (ret == 0)
				add_new_bo(open->handle, open->size, NULL);

			return ret;
		}

		case DRM_IOCTL_PRIME_FD_TO_HANDLE: {
			struct drm_prime_handle *prime = argp;

			ret = libc_ioctl(fd, request, argp);
			if (ret == 0) {
				off_t size;

				size = lseek(prime->fd, 0, SEEK_END);
				fail_if(size == -1, "intel_aubdump: failed to get prime bo size\n");
				add_new_bo(prime->handle, size, NULL);
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
	const char *args = getenv("INTEL_AUBDUMP_ARGS");

	libc_close = dlsym(RTLD_NEXT, "close");
	libc_ioctl = dlsym(RTLD_NEXT, "ioctl");
	fail_if(libc_close == NULL || libc_ioctl == NULL,
		"intel_aubdump: failed to get libc ioctl or close\n");

	if (sscanf(args, "verbose=%d;file=%m[^;];device=%i",
		   &verbose, &filename, &device) != 3)
		filename = strdup("intel.aub");
	fail_if(filename == NULL, "intel_aubdump: out of memory\n");

	if (device)
		device_override = true;

	bos = malloc(MAX_BO_COUNT * sizeof(bos[0]));
	fail_if(bos == NULL, "intel_aubdump: out of memory\n");

	file = fopen(filename, "w+");
	fail_if(file == NULL, "intel_aubdump: failed to open file '%s'\n", filename);

	write_header();
}

static void __attribute__ ((destructor))
fini(void)
{
	free(filename);
	fclose(file);
	free(bos);
}
