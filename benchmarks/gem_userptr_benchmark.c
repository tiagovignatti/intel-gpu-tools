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
 *    Tvrtko Ursulin <tvrtko.ursulin@intel.com>
 *
 */

/** @file gem_userptr_benchmark.c
 *
 * Benchmark the userptr code and impact of having userptr surfaces
 * in process address space on some normal operations.
 *
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
#include "igt_aux.h"

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

#define BO_SIZE (65536)

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

static void free_userptr_bo(int fd, uint32_t handle)
{
	gem_close(fd, handle);
	free_handle_ptr(handle);
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

static const unsigned int nr_bos[] = {0, 1, 10, 100, 1000};
static const unsigned int test_duration_sec = 3;

static volatile unsigned int run_test;

static void alarm_handler(int sig)
{
	assert(run_test == 1);
	run_test = 0;
}

static void start_test(unsigned int duration)
{
	run_test = 1;
	if (duration == 0)
		duration = test_duration_sec;
	signal(SIGALRM, alarm_handler);
	alarm(duration);
}

static void exchange_ptr(void *array, unsigned i, unsigned j)
{
	void **arr, *tmp;
	arr = (void **)array;

	tmp = arr[i];
	arr[i] = arr[j];
	arr[j] = tmp;
}

static void test_malloc_free(int random)
{
	unsigned long iter = 0;
	unsigned int i, tot = 1000;
	void *ptr[tot];

	start_test(test_duration_sec);

	while (run_test) {
		for (i = 0; i < tot; i++) {
			ptr[i] = malloc(1000);
			assert(ptr[i]);
		}
		if (random)
			igt_permute_array(ptr, tot, exchange_ptr);
		for (i = 0; i < tot; i++)
			free(ptr[i]);
		iter++;
	}

	printf("%8lu iter/s\n", iter / test_duration_sec);
}

static void test_malloc_realloc_free(int random)
{
	unsigned long iter = 0;
	unsigned int i, tot = 1000;
	void *ptr[tot];

	start_test(test_duration_sec);

	while (run_test) {
		for (i = 0; i < tot; i++) {
			ptr[i] = malloc(1000);
			assert(ptr[i]);
		}
		if (random)
			igt_permute_array(ptr, tot, exchange_ptr);
		for (i = 0; i < tot; i++) {
			ptr[i] = realloc(ptr[i], 2000);
			assert(ptr[i]);
		}
		if (random)
			igt_permute_array(ptr, tot, exchange_ptr);
		for (i = 0; i < tot; i++)
			free(ptr[i]);
		iter++;
	}

	printf("%8lu iter/s\n", iter / test_duration_sec);
}

static void test_mmap_unmap(int random)
{
	unsigned long iter = 0;
	unsigned int i, tot = 1000;
	void *ptr[tot];

	start_test(test_duration_sec);

	while (run_test) {
		for (i = 0; i < tot; i++) {
			ptr[i] = mmap(NULL, 1000, PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			assert(ptr[i] != MAP_FAILED);
		}
		if (random)
			igt_permute_array(ptr, tot, exchange_ptr);
		for (i = 0; i < tot; i++)
			munmap(ptr[i], 1000);
		iter++;
	}

	printf("%8lu iter/s\n", iter / test_duration_sec);
}

static void test_ptr_read(void *ptr)
{
	unsigned long iter = 0;
	volatile unsigned long *p;
	unsigned long i, loops;

	loops = BO_SIZE / sizeof(unsigned long) / 4;

	start_test(test_duration_sec);

	while (run_test) {
		p = (unsigned long *)ptr;
		for (i = 0; i < loops; i++) {
			(void)*p++;
			(void)*p++;
			(void)*p++;
			(void)*p++;
		}
		iter++;
	}

	printf("%8lu MB/s\n", iter / test_duration_sec * BO_SIZE / 1000000);
}

static void test_ptr_write(void *ptr)
{
	unsigned long iter = 0;
	volatile unsigned long *p;
	register unsigned long i, loops;

	loops = BO_SIZE / sizeof(unsigned long) / 4;

	start_test(test_duration_sec);

	while (run_test) {
		p = (unsigned long *)ptr;
		for (i = 0; i < loops; i++) {
			*p++ = i;
			*p++ = i;
			*p++ = i;
			*p++ = i;
		}
		iter++;
	}

	printf("%8lu MB/s\n", iter / test_duration_sec * BO_SIZE / 1000000);
}

static void test_impact(int fd)
{
	unsigned int total = sizeof(nr_bos) / sizeof(nr_bos[0]);
	unsigned int subtest, i;
	uint32_t handles[nr_bos[total-1]];
	void *ptr;
	char buffer[BO_SIZE];

	for (subtest = 0; subtest < total; subtest++) {
		for (i = 0; i < nr_bos[subtest]; i++)
			handles[i] = create_userptr_bo(fd, BO_SIZE);

		if (nr_bos[subtest] > 0)
			ptr = get_handle_ptr(handles[0]);
		else
			ptr = buffer;

		printf("ptr-read,                   %5u bos = ", nr_bos[subtest]);
		test_ptr_read(ptr);

		printf("ptr-write                   %5u bos = ", nr_bos[subtest]);
		test_ptr_write(ptr);

		printf("malloc-free,                %5u bos = ", nr_bos[subtest]);
		test_malloc_free(0);
		printf("malloc-free-random          %5u bos = ", nr_bos[subtest]);
		test_malloc_free(1);

		printf("malloc-realloc-free,        %5u bos = ", nr_bos[subtest]);
		test_malloc_realloc_free(0);
		printf("malloc-realloc-free-random, %5u bos = ", nr_bos[subtest]);
		test_malloc_realloc_free(1);

		printf("mmap-unmap,                 %5u bos = ", nr_bos[subtest]);
		test_mmap_unmap(0);
		printf("mmap-unmap-random,          %5u bos = ", nr_bos[subtest]);
		test_mmap_unmap(1);

		for (i = 0; i < nr_bos[subtest]; i++)
			free_userptr_bo(fd, handles[i]);
	}
}

static void test_single(int fd)
{
	char *ptr, *bo_ptr;
	uint32_t handle = 0;
	unsigned long iter = 0;
	int ret;
	unsigned long map_size = BO_SIZE + PAGE_SIZE - 1;

	ptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	assert(ptr != MAP_FAILED);

	bo_ptr = (char *)ALIGN((unsigned long)ptr, PAGE_SIZE);

	start_test(test_duration_sec);

	while (run_test) {
		ret = gem_userptr(fd, bo_ptr, BO_SIZE, 0, &handle);
		assert(ret == 0);
		gem_close(fd, handle);
		iter++;
	}

	munmap(ptr, map_size);

	printf("%8lu iter/s\n", iter / test_duration_sec);
}

static void test_multiple(int fd, unsigned int batch, int random)
{
	char *ptr, *bo_ptr;
	uint32_t handles[10000];
	int map[10000];
	unsigned long iter = 0;
	int ret;
	int i;
	unsigned long map_size = batch * BO_SIZE + PAGE_SIZE - 1;

	assert(batch < (sizeof(handles) / sizeof(handles[0])));
	assert(batch < (sizeof(map) / sizeof(map[0])));

	ptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	assert(ptr != MAP_FAILED);

	bo_ptr = (char *)ALIGN((unsigned long)ptr, PAGE_SIZE);

	for (i = 0; i < batch; i++)
		map[i] = i;

	start_test(test_duration_sec);

	while (run_test) {
		if (random)
			igt_permute_array(map, batch, igt_exchange_int);
		for (i = 0; i < batch; i++) {
			ret = gem_userptr(fd, bo_ptr + map[i] * BO_SIZE,
						BO_SIZE,
						0, &handles[i]);
			assert(ret == 0);
		}
		if (random)
			igt_permute_array(map, batch, igt_exchange_int);
		for (i = 0; i < batch; i++)
			gem_close(fd, handles[map[i]]);
		iter++;
	}

	munmap(ptr, map_size);

	printf("%8lu iter/s\n", iter * batch / test_duration_sec);
}

static void test_userptr(int fd)
{
	printf("create-destroy                = ");
	test_single(fd);

	printf("multi-create-destroy          = ");
	test_multiple(fd, 100, 0);

	printf("multi-create-destroy-random   = ");
	test_multiple(fd, 100, 1);
}

int main(int argc, char **argv)
{
	int fd = -1, ret;

	igt_skip_on_simulation();

	igt_subtest_init(argc, argv);

	fd = drm_open_any();
	igt_assert(fd >= 0);

	ret = has_userptr(fd);
	igt_skip_on_f(ret == 0, "No userptr support - %s (%d)\n",
			strerror(errno), ret);


	gem_userptr_test_unsynchronized();

	igt_subtest("userptr-unsync")
		test_userptr(fd);

	igt_subtest("userptr-impact-unsync")
		test_impact(fd);

	gem_userptr_test_synchronized();

	igt_subtest("userptr-sync")
		test_userptr(fd);

	igt_subtest("userptr-impact-sync")
		test_impact(fd);

	igt_exit();

	return 0;
}
