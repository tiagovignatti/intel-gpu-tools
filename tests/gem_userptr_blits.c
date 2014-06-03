/*
 * Copyright © 2009-2014 Intel Corporation
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
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *    Tvrtko Ursulin <tvrtko.ursulin@intel.com>
 *
 */

/** @file gem_userptr_blits.c
 *
 * This is a test of doing many blits using a mixture of normal system pages
 * and uncached linear buffers, with a working set larger than the
 * aperture size.
 *
 * The goal is to simply ensure the basics work.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <signal.h>

#include "drm.h"
#include "i915_drm.h"

#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_chipset.h"
#include "ioctl_wrappers.h"

#include "eviction_common.c"

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define LOCAL_I915_GEM_USERPTR       0x33
#define LOCAL_IOCTL_I915_GEM_USERPTR DRM_IOWR (DRM_COMMAND_BASE + LOCAL_I915_GEM_USERPTR, struct local_i915_gem_userptr)
struct local_i915_gem_userptr {
	uint64_t user_ptr;
	uint64_t user_size;
	uint32_t flags;
#define LOCAL_I915_USERPTR_READ_ONLY (1<<0)
#define LOCAL_I915_USERPTR_UNSYNCHRONIZED (1<<31)
	uint32_t handle;
};

static uint32_t userptr_flags = LOCAL_I915_USERPTR_UNSYNCHRONIZED;

#define WIDTH 512
#define HEIGHT 512

static uint32_t linear[WIDTH*HEIGHT];

static void gem_userptr_test_unsynchronized(void)
{
	userptr_flags = LOCAL_I915_USERPTR_UNSYNCHRONIZED;
}

static void gem_userptr_test_synchronized(void)
{
	userptr_flags = 0;
}

static int gem_userptr(int fd, void *ptr, int size, int read_only, uint32_t *handle)
{
	struct local_i915_gem_userptr userptr;
	int ret;

	userptr.user_ptr = (uintptr_t)ptr;
	userptr.user_size = size;
	userptr.flags = userptr_flags;
	if (read_only)
		userptr.flags |= LOCAL_I915_USERPTR_READ_ONLY;

	ret = drmIoctl(fd, LOCAL_IOCTL_I915_GEM_USERPTR, &userptr);
	if (ret)
		ret = errno;
	igt_skip_on_f(ret == ENODEV &&
		      (userptr_flags & LOCAL_I915_USERPTR_UNSYNCHRONIZED) == 0 &&
		      !read_only,
		      "Skipping, synchronized mappings with no kernel CONFIG_MMU_NOTIFIER?");
	if (ret == 0)
		*handle = userptr.handle;

	return ret;
}


static void gem_userptr_sync(int fd, uint32_t handle)
{
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
}

static void
copy(int fd, uint32_t dst, uint32_t src, unsigned int error)
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
	if (ret)
		ret = errno;

	if (error == ~0)
		igt_assert(ret != 0);
	else
		igt_assert(ret == error);

	gem_close(fd, handle);
}

static int
blit(int fd, uint32_t dst, uint32_t src, uint32_t *all_bo, int n_bo)
{
	uint32_t batch[12];
	struct drm_i915_gem_relocation_entry reloc[2];
	struct drm_i915_gem_exec_object2 *obj;
	struct drm_i915_gem_execbuffer2 exec;
	uint32_t handle;
	int n, ret, i=0;

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
	reloc[0].read_domains = I915_GEM_DOMAIN_RENDER;
	reloc[0].write_domain = I915_GEM_DOMAIN_RENDER;

	reloc[1].target_handle = src;
	reloc[1].delta = 0;
	reloc[1].offset = 7 * sizeof(batch[0]);
	if (intel_gen(intel_get_drm_devid(fd)) >= 8)
		reloc[1].offset += sizeof(batch[0]);
	reloc[1].presumed_offset = 0;
	reloc[1].read_domains = I915_GEM_DOMAIN_RENDER;
	reloc[1].write_domain = 0;

	obj = calloc(n_bo + 1, sizeof(*obj));
	for (n = 0; n < n_bo; n++)
		obj[n].handle = all_bo[n];
	obj[n].handle = handle;
	obj[n].relocation_count = 2;
	obj[n].relocs_ptr = (uintptr_t)reloc;

	exec.buffers_ptr = (uintptr_t)obj;
	exec.buffer_count = n_bo + 1;
	exec.batch_start_offset = 0;
	exec.batch_len = i * 4;
	exec.DR1 = exec.DR4 = 0;
	exec.num_cliprects = 0;
	exec.cliprects_ptr = 0;
	exec.flags = HAS_BLT_RING(intel_get_drm_devid(fd)) ? I915_EXEC_BLT : 0;
	i915_execbuffer2_set_context_id(exec, 0);
	exec.rsvd2 = 0;

	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &exec);
	if (ret)
		ret = errno;

	gem_close(fd, handle);
	free(obj);

	return ret;
}

static uint32_t
create_userptr(int fd, uint32_t val, uint32_t *ptr)
{
	uint32_t handle;
	int i, ret;

	ret = gem_userptr(fd, ptr, sizeof(linear), 0, &handle);
	igt_assert(ret == 0);
	igt_assert(handle != 0);

	/* Fill the BO with dwords starting at val */
	for (i = 0; i < WIDTH*HEIGHT; i++)
		ptr[i] = val++;

	return handle;
}

static void **handle_ptr_map;
static unsigned int num_handle_ptr_map;

static void add_handle_ptr(uint32_t handle, void *ptr)
{
	if (handle >= num_handle_ptr_map) {
		handle_ptr_map = realloc(handle_ptr_map,
					 (handle + 1000) * sizeof(void*));
		num_handle_ptr_map = handle + 1000;
	}

	handle_ptr_map[handle] = ptr;
}

static void *get_handle_ptr(uint32_t handle)
{
	return handle_ptr_map[handle];
}

static void free_handle_ptr(uint32_t handle)
{
	igt_assert(handle < num_handle_ptr_map);
	igt_assert(handle_ptr_map[handle]);

	free(handle_ptr_map[handle]);
	handle_ptr_map[handle] = NULL;
}

static uint32_t create_userptr_bo(int fd, int size)
{
	void *ptr;
	uint32_t handle;
	int ret;

	ret = posix_memalign(&ptr, PAGE_SIZE, size);
	igt_assert(ret == 0);

	ret = gem_userptr(fd, (uint32_t *)ptr, size, 0, &handle);
	igt_assert(ret == 0);
	add_handle_ptr(handle, ptr);

	return handle;
}

static void clear(int fd, uint32_t handle, int size)
{
	void *ptr = get_handle_ptr(handle);

	igt_assert(ptr != NULL);

	memset(ptr, 0, size);
}

static void free_userptr_bo(int fd, uint32_t handle)
{
	gem_close(fd, handle);
	free_handle_ptr(handle);
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
check_cpu(uint32_t *ptr, uint32_t val)
{
	int i;

	for (i = 0; i < WIDTH*HEIGHT; i++) {
		igt_assert_f(ptr[i] == val,
			     "Expected 0x%08x, found 0x%08x "
			     "at offset 0x%08x\n",
			     val, ptr[i], i * 4);
		val++;
	}
}

static void
check_gpu(int fd, uint32_t handle, uint32_t val)
{
	gem_read(fd, handle, 0, linear, sizeof(linear));
	check_cpu(linear, val);
}

static int has_userptr(int fd)
{
	uint32_t handle = 0;
	void *ptr;
	uint32_t oldflags;
	int ret;

	assert(posix_memalign(&ptr, PAGE_SIZE, PAGE_SIZE) == 0);
	oldflags = userptr_flags;
	gem_userptr_test_unsynchronized();
	ret = gem_userptr(fd, ptr, PAGE_SIZE, 0, &handle);
	userptr_flags = oldflags;
	if (ret != 0) {
		free(ptr);
		return 0;
	}

	gem_close(fd, handle);
	free(ptr);

	return handle != 0;
}

static int test_input_checking(int fd)
{
	struct local_i915_gem_userptr userptr;
	int ret;

	/* Invalid flags. */
	userptr.user_ptr = 0;
	userptr.user_size = 0;
	userptr.flags = ~0;
	ret = drmIoctl(fd, LOCAL_IOCTL_I915_GEM_USERPTR, &userptr);
	igt_assert(ret != 0);

	/* Too big. */
	userptr.user_ptr = 0;
	userptr.user_size = ~0;
	userptr.flags = 0;
	ret = drmIoctl(fd, LOCAL_IOCTL_I915_GEM_USERPTR, &userptr);
	igt_assert(ret != 0);

	/* Both wrong. */
	userptr.user_ptr = 0;
	userptr.user_size = ~0;
	userptr.flags = ~0;
	ret = drmIoctl(fd, LOCAL_IOCTL_I915_GEM_USERPTR, &userptr);
	igt_assert(ret != 0);

	return 0;
}

static int test_access_control(int fd)
{
	igt_fork(child, 1) {
		void *ptr;
		int ret;
		uint32_t handle;

		igt_drop_root();

		/* CAP_SYS_ADMIN is needed for UNSYNCHRONIZED mappings. */
		gem_userptr_test_unsynchronized();

		igt_assert(posix_memalign(&ptr, PAGE_SIZE, PAGE_SIZE) == 0);

		ret = gem_userptr(fd, ptr, PAGE_SIZE, 0, &handle);
		if (ret == 0)
			gem_close(fd, handle);
		free(ptr);
		igt_assert(ret == EPERM);
	}

	igt_waitchildren();

	return 0;
}

static int test_invalid_mapping(int fd)
{
	int ret;
	uint32_t handle, handle2;
	void *ptr;

	/* NULL pointer. */
	ret = gem_userptr(fd, NULL, PAGE_SIZE, 0, &handle);
	igt_assert(ret == 0);
	copy(fd, handle, handle, ~0); /* QQQ Precise errno? */
	gem_close(fd, handle);

	/* GTT mapping */
	handle = create_bo(fd, 0);
	ptr = gem_mmap__gtt(fd, handle, sizeof(linear), PROT_READ | PROT_WRITE);
	if (ptr == NULL)
		gem_close(fd, handle);
	assert(ptr != NULL);
	assert(((unsigned long)ptr & (PAGE_SIZE - 1)) == 0);
	assert((sizeof(linear) & (PAGE_SIZE - 1)) == 0);
	ret = gem_userptr(fd, ptr, sizeof(linear), 0, &handle2);
	igt_assert(ret == 0);
	copy(fd, handle, handle, ~0); /* QQQ Precise errno? */
	gem_close(fd, handle2);
	munmap(ptr, sizeof(linear));
	gem_close(fd, handle);

	return 0;
}

static int test_forbidden_ops(int fd)
{
	void *ptr;
	int ret;
	uint32_t handle;
	char buf[PAGE_SIZE];
	struct drm_i915_gem_pread gem_pread;
	struct drm_i915_gem_pwrite gem_pwrite;

	assert(posix_memalign(&ptr, PAGE_SIZE, PAGE_SIZE) == 0);

	ret = gem_userptr(fd, ptr, PAGE_SIZE, 0, &handle);
	igt_assert(ret == 0);

	gem_pread.handle = handle;
	gem_pread.offset = 0;
	gem_pread.size = PAGE_SIZE;
	gem_pread.data_ptr = (uintptr_t)buf;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_PREAD, &gem_pread);
	if (ret == 0) {
		gem_close(fd, handle);
		free(ptr);
	}
	igt_assert(ret != 0);

	gem_pwrite.handle = handle;
	gem_pwrite.offset = 0;
	gem_pwrite.size = PAGE_SIZE;
	gem_pwrite.data_ptr = (uintptr_t)buf;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite);
	if (ret == 0) {
		gem_close(fd, handle);
		free(ptr);
	}
	igt_assert(ret != 0);

	gem_close(fd, handle);
	free(ptr);

	return 0;
}

static char counter;

static void (*orig_sigbus)(int sig, siginfo_t *info, void *param);
static unsigned long sigbus_start;
static long sigbus_cnt = -1;

static void
check_bo(int fd1, uint32_t handle1, int is_userptr, int fd2, uint32_t handle2)
{
	char *ptr1, *ptr2;
	int i;
	unsigned long size = sizeof(linear);

	if (is_userptr)
		ptr1 = get_handle_ptr(handle1);
	else
		ptr1 = gem_mmap(fd1, handle1, sizeof(linear), PROT_READ | PROT_WRITE);

	ptr2 = gem_mmap(fd2, handle2, sizeof(linear), PROT_READ | PROT_WRITE);

	igt_assert(ptr1);
	igt_assert(ptr2);

	sigbus_start = (unsigned long)ptr2;

	if (sigbus_cnt == 0)
		size = 1;

	/* check whether it's still our old object first. */
	for (i = 0; i < size; i++) {
		igt_assert(ptr1[i] == counter);
		igt_assert(ptr2[i] == counter);
	}

	counter++;

	if (size > 1) {
		memset(ptr1, counter, size);
		igt_assert(memcmp(ptr1, ptr2, size) == 0);
	}

	if (!is_userptr)
		munmap(ptr1, sizeof(linear));
	munmap(ptr2, sizeof(linear));
}

static int export_handle(int fd, uint32_t handle, int *outfd)
{
	struct drm_prime_handle args;
	int ret;

	args.handle = handle;
	args.flags = DRM_CLOEXEC;
	args.fd = -1;

	ret = drmIoctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);
	if (ret)
		ret = errno;
	*outfd = args.fd;

	return ret;
}

static void sigbus(int sig, siginfo_t *info, void *param)
{
	unsigned long ptr = (unsigned long)info->si_addr;
	void *addr;

	if (ptr >= sigbus_start &&
	    ptr <= (sigbus_start + sizeof(linear))) {
		sigbus_cnt++;
		addr = mmap((void *)ptr, sizeof(linear), PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);
		if ((unsigned long)addr == ptr) {
			memset(addr, counter, sizeof(linear));
			return;
		}
	}

	if (orig_sigbus)
		orig_sigbus(sig, info, param);
	assert(0);
}

static int test_dmabuf(void)
{
	int fd1, fd2;
	uint32_t handle, handle_import1, handle_import2, handle_selfimport;
	int dma_buf_fd = -1;
	int ret;
	struct sigaction sigact, orig_sigact;

	fd1 = drm_open_any();
	fd2 = drm_open_any();

	handle = create_userptr_bo(fd1, sizeof(linear));

	ret = export_handle(fd1, handle, &dma_buf_fd);
	if (userptr_flags & LOCAL_I915_USERPTR_UNSYNCHRONIZED && ret) {
		igt_assert(ret == EINVAL || ret == ENODEV);
		free_userptr_bo(fd1, handle);

		return 0;
	} else {
		igt_assert(ret == 0);
		igt_assert(dma_buf_fd >= 0);
	}
	handle_import1 = prime_fd_to_handle(fd2, dma_buf_fd);
	check_bo(fd1, handle, 1, fd2, handle_import1);

	/* reimport should give us the same handle so that userspace can check
	 * whether it has that bo already somewhere. */
	handle_import2 = prime_fd_to_handle(fd2, dma_buf_fd);
	igt_assert(handle_import1 == handle_import2);

	/* Same for re-importing on the exporting fd. */
	handle_selfimport = prime_fd_to_handle(fd1, dma_buf_fd);
	igt_assert(handle == handle_selfimport);

	/* close dma_buf, check whether nothing disappears. */
	close(dma_buf_fd);
	check_bo(fd1, handle, 1, fd2, handle_import1);

	/* destroy userptr object and expect SIGBUS */
	free_userptr_bo(fd1, handle);
	sigact.sa_sigaction = sigbus;
	sigact.sa_flags = SA_SIGINFO;
	ret = sigaction(SIGBUS, &sigact, &orig_sigact);
	assert(ret == 0);
	orig_sigbus = orig_sigact.sa_sigaction;
	sigbus_cnt = 0;
	check_bo(fd2, handle_import1, 0, fd2, handle_import1);
	assert(sigbus_cnt > 0);
	sigact.sa_sigaction = orig_sigbus;
	sigact.sa_flags = SA_SIGINFO;
	ret = sigaction(SIGBUS, &sigact, &orig_sigact);
	assert(ret == 0);

	gem_close(fd2, handle_import1);
	close(fd1);
	close(fd2);

	return 0;
}

static int test_usage_restrictions(int fd)
{
	void *ptr;
	int ret;
	uint32_t handle;

	assert(posix_memalign(&ptr, PAGE_SIZE, PAGE_SIZE * 2) == 0);

	/* Address not aligned. */
	ret = gem_userptr(fd, (char *)ptr + 1, PAGE_SIZE, 0, &handle);
	igt_assert(ret != 0);

	/* Size not rounded to page size. */
	ret = gem_userptr(fd, ptr, PAGE_SIZE - 1, 0, &handle);
	igt_assert(ret != 0);

	/* Both wrong. */
	ret = gem_userptr(fd, (char *)ptr + 1, PAGE_SIZE - 1, 0, &handle);
	igt_assert(ret != 0);

	/* Read-only not supported. */
	ret = gem_userptr(fd, (char *)ptr, PAGE_SIZE, 1, &handle);
	igt_assert(ret != 0);

	free(ptr);

	return 0;
}

static int test_create_destroy(int fd)
{
	void *ptr;
	int ret;
	uint32_t handle;

	igt_assert(posix_memalign(&ptr, PAGE_SIZE, PAGE_SIZE) == 0);

	ret = gem_userptr(fd, ptr, PAGE_SIZE, 0, &handle);
	igt_assert(ret == 0);

	gem_close(fd, handle);
	free(ptr);

	return 0;
}

static int test_coherency(int fd, int count)
{
	uint32_t *memory;
	uint32_t *cpu, *cpu_val;
	uint32_t *gpu, *gpu_val;
	uint32_t start = 0;
	int i, ret;

	igt_info("Using 2x%d 1MiB buffers\n", count);

	ret = posix_memalign((void **)&memory, PAGE_SIZE, count*sizeof(linear));
	igt_assert(ret == 0 && memory);

	gpu = malloc(sizeof(uint32_t)*count*4);
	gpu_val = gpu + count;
	cpu = gpu_val + count;
	cpu_val = cpu + count;

	for (i = 0; i < count; i++) {
		gpu[i] = create_bo(fd, start);
		gpu_val[i] = start;
		start += WIDTH*HEIGHT;
	}

	for (i = 0; i < count; i++) {
		cpu[i] = create_userptr(fd, start, memory+i*WIDTH*HEIGHT);
		cpu_val[i] = start;
		start += WIDTH*HEIGHT;
	}

	igt_info("Verifying initialisation...\n");
	for (i = 0; i < count; i++) {
		check_gpu(fd, gpu[i], gpu_val[i]);
		check_cpu(memory+i*WIDTH*HEIGHT, cpu_val[i]);
	}

	igt_info("Cyclic blits cpu->gpu, forward...\n");
	for (i = 0; i < count * 4; i++) {
		int src = i % count;
		int dst = (i + 1) % count;

		copy(fd, gpu[dst], cpu[src], 0);
		gpu_val[dst] = cpu_val[src];
	}
	for (i = 0; i < count; i++)
		check_gpu(fd, gpu[i], gpu_val[i]);

	igt_info("Cyclic blits gpu->cpu, backward...\n");
	for (i = 0; i < count * 4; i++) {
		int src = (i + 1) % count;
		int dst = i % count;

		copy(fd, cpu[dst], gpu[src], 0);
		cpu_val[dst] = gpu_val[src];
	}
	for (i = 0; i < count; i++) {
		gem_userptr_sync(fd, cpu[i]);
		check_cpu(memory+i*WIDTH*HEIGHT, cpu_val[i]);
	}

	igt_info("Random blits...\n");
	for (i = 0; i < count * 4; i++) {
		int src = random() % count;
		int dst = random() % count;

		if (random() & 1) {
			copy(fd, gpu[dst], cpu[src], 0);
			gpu_val[dst] = cpu_val[src];
		} else {
			copy(fd, cpu[dst], gpu[src], 0);
			cpu_val[dst] = gpu_val[src];
		}
	}
	for (i = 0; i < count; i++) {
		check_gpu(fd, gpu[i], gpu_val[i]);
		gem_close(fd, gpu[i]);

		gem_userptr_sync(fd, cpu[i]);
		check_cpu(memory+i*WIDTH*HEIGHT, cpu_val[i]);
		gem_close(fd, cpu[i]);
	}

	free(gpu);
	free(memory);

	return 0;
}

static struct igt_eviction_test_ops fault_ops = {
	.create = create_userptr_bo,
	.close = free_userptr_bo,
	.copy = blit,
	.clear = clear,
};

static int can_swap(void)
{
	unsigned long as, ram;

	/* Cannot swap if not enough address space */

	/* FIXME: Improve check criteria. */
	if (sizeof(void*) < 8)
		as = 3 * 1024;
	else
		as = 256 * 1024; /* Just a big number */

	ram = intel_get_total_ram_mb();

	if ((as - 128) < (ram - 256))
		return 0;

	return 1;
}

#define min(a, b) ((a) < (b) ? (a) : (b))

static void test_forking_evictions(int fd, int size, int count,
			     unsigned flags)
{
	int trash_count;
	int num_threads;

	trash_count = intel_get_total_ram_mb() * 11 / 10;
	/* Use the fact test will spawn a number of child
	 * processes meaning swapping will be triggered system
	 * wide even if one process on it's own can't do it.
	 */
	num_threads = min(sysconf(_SC_NPROCESSORS_ONLN) * 4, 12);
	trash_count /= num_threads;
	if (count > trash_count)
		count = trash_count;

	forking_evictions(fd, &fault_ops, size, count, trash_count, flags);
}

static void test_swapping_evictions(int fd, int size, int count)
{
	int trash_count;

	igt_skip_on_f(!can_swap(),
		"Not enough process address space for swapping tests.\n");

	trash_count = intel_get_total_ram_mb() * 11 / 10;

	swapping_evictions(fd, &fault_ops, size, count, trash_count);
}

static void test_minor_evictions(int fd, int size, int count)
{
	minor_evictions(fd, &fault_ops, size, count);
}

static void test_major_evictions(int fd, int size, int count)
{
	major_evictions(fd, &fault_ops, size, count);
}

static int test_overlap(int fd, int expected)
{
	char *ptr;
	int ret;
	uint32_t handle, handle2;

	igt_assert(posix_memalign((void *)&ptr, PAGE_SIZE, PAGE_SIZE * 3) == 0);

	ret = gem_userptr(fd, ptr + PAGE_SIZE, PAGE_SIZE, 0, &handle);
	igt_assert(ret == 0);

	ret = gem_userptr(fd, ptr, PAGE_SIZE, 0, &handle2);
	igt_assert(ret == 0);
	gem_close(fd, handle2);

	ret = gem_userptr(fd, ptr + PAGE_SIZE * 2, PAGE_SIZE, 0, &handle2);
	igt_assert(ret == 0);
	gem_close(fd, handle2);

	ret = gem_userptr(fd, ptr, PAGE_SIZE * 2, 0, &handle2);
	igt_assert(ret == expected);
	if (ret == 0)
		gem_close(fd, handle2);

	ret = gem_userptr(fd, ptr + PAGE_SIZE, PAGE_SIZE * 2, 0, &handle2);
	igt_assert(ret == expected);
	if (ret == 0)
		gem_close(fd, handle2);

	ret = gem_userptr(fd, ptr, PAGE_SIZE * 3, 0, &handle2);
	igt_assert(ret == expected);
	if (ret == 0)
		gem_close(fd, handle2);

	gem_close(fd, handle);
	free(ptr);

	return 0;
}

static int test_unmap(int fd, int expected)
{
	char *ptr, *bo_ptr;
	const unsigned int num_obj = 3;
	unsigned int i;
	uint32_t bo[num_obj + 1];
	size_t map_size = sizeof(linear) * num_obj + (PAGE_SIZE - 1);
	int ret;

	ptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	assert(ptr != MAP_FAILED);

	bo_ptr = (char *)ALIGN((unsigned long)ptr, PAGE_SIZE);

	for (i = 0; i < num_obj; i++, bo_ptr += sizeof(linear)) {
		ret = gem_userptr(fd, bo_ptr, sizeof(linear), 0, &bo[i]);
		igt_assert(ret == 0);
	}

	bo[num_obj] = create_bo(fd, 0);

	for (i = 0; i < num_obj; i++)
		copy(fd, bo[num_obj], bo[i], 0);

	ret = munmap(ptr, map_size);
	assert(ret == 0);

	for (i = 0; i < num_obj; i++)
		copy(fd, bo[num_obj], bo[i], expected);

	for (i = 0; i < (num_obj + 1); i++)
		gem_close(fd, bo[i]);

	return 0;
}

static int test_unmap_after_close(int fd)
{
	char *ptr, *bo_ptr;
	const unsigned int num_obj = 3;
	unsigned int i;
	uint32_t bo[num_obj + 1];
	size_t map_size = sizeof(linear) * num_obj + (PAGE_SIZE - 1);
	int ret;

	ptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	assert(ptr != MAP_FAILED);

	bo_ptr = (char *)ALIGN((unsigned long)ptr, PAGE_SIZE);

	for (i = 0; i < num_obj; i++, bo_ptr += sizeof(linear)) {
		ret = gem_userptr(fd, bo_ptr, sizeof(linear), 0, &bo[i]);
		igt_assert(ret == 0);
	}

	bo[num_obj] = create_bo(fd, 0);

	for (i = 0; i < num_obj; i++)
		copy(fd, bo[num_obj], bo[i], 0);

	for (i = 0; i < (num_obj + 1); i++)
		gem_close(fd, bo[i]);

	ret = munmap(ptr, map_size);
	assert(ret == 0);

	return 0;
}

static int test_unmap_cycles(int fd, int expected)
{
	int i;

	for (i = 0; i < 1000; i++)
		test_unmap(fd, expected);

	return 0;
}

unsigned int total_ram;
uint64_t aperture_size;
int fd, count;


int main(int argc, char **argv)
{
	int size = sizeof(linear);

	igt_skip_on_simulation();

	igt_subtest_init(argc, argv);

	igt_fixture {
		int ret;

		fd = drm_open_any();
		igt_assert(fd >= 0);

		ret = has_userptr(fd);
		igt_skip_on_f(ret == 0, "No userptr support - %s (%d)\n",
			      strerror(errno), ret);

		size = sizeof(linear);

		aperture_size = gem_aperture_size(fd);
		igt_info("Aperture size is %lu MiB\n", (long)(aperture_size / (1024*1024)));

		if (argc > 1)
			count = atoi(argv[1]);
		if (count == 0)
			count = 2 * aperture_size / (1024*1024) / 3;

		total_ram = intel_get_total_ram_mb();
		igt_info("Total RAM is %u MiB\n", total_ram);

		if (count > total_ram * 3 / 4) {
			count = intel_get_total_ram_mb() * 3 / 4;
			igt_info("Not enough RAM to run test, reducing buffer count.\n");
		}
	}

	igt_subtest("input-checking")
		test_input_checking(fd);

	igt_subtest("usage-restrictions")
		test_usage_restrictions(fd);

	igt_subtest("invalid-mapping")
		test_invalid_mapping(fd);

	igt_subtest("forbidden-operations")
		test_forbidden_ops(fd);

	igt_info("Testing unsynchronized mappings...\n");
	gem_userptr_test_unsynchronized();

	igt_subtest("create-destroy-unsync")
		test_create_destroy(fd);

	igt_subtest("unsync-overlap")
		test_overlap(fd, 0);

	igt_subtest("unsync-unmap")
		test_unmap(fd, 0);

	igt_subtest("unsync-unmap-cycles")
		test_unmap_cycles(fd, 0);

	igt_subtest("unsync-unmap-after-close")
		test_unmap_after_close(fd);

	igt_subtest("coherency-unsync")
		test_coherency(fd, count);

	igt_subtest("dmabuf-unsync")
		test_dmabuf();

	for (unsigned flags = 0; flags < ALL_FORKING_EVICTIONS + 1; flags++) {
		igt_subtest_f("forked-unsync%s%s%s-%s",
		    flags & FORKING_EVICTIONS_SWAPPING ? "-swapping" : "",
		    flags & FORKING_EVICTIONS_DUP_DRMFD ? "-multifd" : "",
		    flags & FORKING_EVICTIONS_MEMORY_PRESSURE ?
				"-mempressure" : "",
		    flags & FORKING_EVICTIONS_INTERRUPTIBLE ?
				"interruptible" : "normal") {
			test_forking_evictions(fd, size, count, flags);
		}
	}

	igt_subtest("swapping-unsync-normal")
		test_swapping_evictions(fd, size, count);

	igt_subtest("minor-unsync-normal")
		test_minor_evictions(fd, size, count);

	igt_subtest("major-unsync-normal") {
		size = 200 * 1024 * 1024;
		count = (gem_aperture_size(fd) / size) + 2;
		test_major_evictions(fd, size, count);
	}

	igt_fixture {
		size = sizeof(linear);
		count = 2 * gem_aperture_size(fd) / (1024*1024) / 3;
		if (count > total_ram * 3 / 4)
			count = intel_get_total_ram_mb() * 3 / 4;
	}

	igt_fork_signal_helper();

	igt_subtest("swapping-unsync-interruptible")
		test_swapping_evictions(fd, size, count);

	igt_subtest("minor-unsync-interruptible")
		test_minor_evictions(fd, size, count);

	igt_subtest("major-unsync-interruptible") {
		size = 200 * 1024 * 1024;
		count = (gem_aperture_size(fd) / size) + 2;
		test_major_evictions(fd, size, count);
	}

	igt_stop_signal_helper();

	igt_info("Testing synchronized mappings...\n");

	igt_fixture {
		size = sizeof(linear);
		count = 2 * gem_aperture_size(fd) / (1024*1024) / 3;
		if (count > total_ram * 3 / 4)
			count = intel_get_total_ram_mb() * 3 / 4;
	}

	gem_userptr_test_synchronized();

	igt_subtest("create-destroy-sync")
		test_create_destroy(fd);

	igt_subtest("sync-overlap")
		test_overlap(fd, EINVAL);

	igt_subtest("sync-unmap")
		test_unmap(fd, EFAULT);

	igt_subtest("sync-unmap-cycles")
		test_unmap_cycles(fd, EFAULT);

	igt_subtest("sync-unmap-after-close")
		test_unmap_after_close(fd);

	igt_subtest("coherency-sync")
		test_coherency(fd, count);

	igt_subtest("dmabuf-sync")
		test_dmabuf();

	for (unsigned flags = 0; flags < ALL_FORKING_EVICTIONS + 1; flags++) {
		igt_subtest_f("forked-sync%s%s%s-%s",
		    flags & FORKING_EVICTIONS_SWAPPING ? "-swapping" : "",
		    flags & FORKING_EVICTIONS_DUP_DRMFD ? "-multifd" : "",
		    flags & FORKING_EVICTIONS_MEMORY_PRESSURE ?
				"-mempressure" : "",
		    flags & FORKING_EVICTIONS_INTERRUPTIBLE ?
				"interruptible" : "normal") {
			test_forking_evictions(fd, size, count, flags);
		}
	}

	igt_subtest("swapping-normal-sync")
		test_swapping_evictions(fd, size, count);

	igt_subtest("minor-normal-sync")
		test_minor_evictions(fd, size, count);

	igt_subtest("major-normal-sync") {
		size = 200 * 1024 * 1024;
		count = (gem_aperture_size(fd) / size) + 2;
		test_major_evictions(fd, size, count);
	}

	igt_fixture {
		size = 1024 * 1024;
		count = 2 * gem_aperture_size(fd) / (1024*1024) / 3;
		if (count > total_ram * 3 / 4)
			count = intel_get_total_ram_mb() * 3 / 4;
	}

	igt_fork_signal_helper();

	igt_subtest("swapping-sync-interruptible")
		test_swapping_evictions(fd, size, count);

	igt_subtest("minor-sync-interruptible")
		test_minor_evictions(fd, size, count);

	igt_subtest("major-sync-interruptible") {
		size = 200 * 1024 * 1024;
		count = (gem_aperture_size(fd) / size) + 2;
		test_major_evictions(fd, size, count);
	}

	igt_stop_signal_helper();

	igt_subtest("access-control")
	test_access_control(fd);

	igt_exit();

	return 0;
}
