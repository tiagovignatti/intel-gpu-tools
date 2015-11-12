/*
 * Copyright © 2011 Intel Corporation
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

#define _GNU_SOURCE
#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "drm.h"

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

static int OBJECT_SIZE = 16*1024*1024;

static void
set_domain_gtt(int fd, uint32_t handle)
{
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
}

static void *
mmap_bo(int fd, uint32_t handle)
{
	void *ptr;

	ptr = gem_mmap__gtt(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);

	return ptr;
}

static void *
create_pointer(int fd)
{
	uint32_t handle;
	void *ptr;

	handle = gem_create(fd, OBJECT_SIZE);

	ptr = mmap_bo(fd, handle);

	gem_close(fd, handle);

	return ptr;
}

static void
test_access(int fd)
{
	uint32_t handle, flink, handle2;
	struct drm_i915_gem_mmap_gtt mmap_arg;
	int fd2;

	handle = gem_create(fd, OBJECT_SIZE);
	igt_assert(handle);

	fd2 = drm_open_driver(DRIVER_INTEL);

	/* Check that fd1 can mmap. */
	mmap_arg.handle = handle;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_arg);

	igt_assert(mmap64(0, OBJECT_SIZE, PROT_READ | PROT_WRITE,
			  MAP_SHARED, fd, mmap_arg.offset));

	/* Check that the same offset on the other fd doesn't work. */
	igt_assert(mmap64(0, OBJECT_SIZE, PROT_READ | PROT_WRITE,
			  MAP_SHARED, fd2, mmap_arg.offset) == MAP_FAILED);
	igt_assert(errno == EACCES);

	flink = gem_flink(fd, handle);
	igt_assert(flink);
	handle2 = gem_open(fd2, flink);
	igt_assert(handle2);

	/* Recheck that it works after flink. */
	/* Check that the same offset on the other fd doesn't work. */
	igt_assert(mmap64(0, OBJECT_SIZE, PROT_READ | PROT_WRITE,
			  MAP_SHARED, fd2, mmap_arg.offset));
}

static void
test_short(int fd)
{
	struct drm_i915_gem_mmap_gtt mmap_arg;
	int pages, p;

	mmap_arg.handle = gem_create(fd, OBJECT_SIZE);
	igt_assert(mmap_arg.handle);

	do_ioctl(fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_arg);
	for (pages = 1; pages <= OBJECT_SIZE / PAGE_SIZE; pages <<= 1) {
		uint8_t *r, *w;

		w = mmap64(0, pages * PAGE_SIZE, PROT_READ | PROT_WRITE,
			   MAP_SHARED, fd, mmap_arg.offset);
		igt_assert(w != MAP_FAILED);

		r = mmap64(0, pages * PAGE_SIZE, PROT_READ,
			   MAP_SHARED, fd, mmap_arg.offset);
		igt_assert(r != MAP_FAILED);

		for (p = 0; p < pages; p++) {
			w[p*PAGE_SIZE] = r[p*PAGE_SIZE];
			w[p*PAGE_SIZE+(PAGE_SIZE-1)] =
				r[p*PAGE_SIZE+(PAGE_SIZE-1)];
		}

		munmap(r, pages * PAGE_SIZE);
		munmap(w, pages * PAGE_SIZE);
	}
	gem_close(fd, mmap_arg.handle);
}

static void
test_copy(int fd)
{
	void *src, *dst;

	/* copy from a fresh src to fresh dst to force pagefault on both */
	src = create_pointer(fd);
	dst = create_pointer(fd);

	memcpy(dst, src, OBJECT_SIZE);
	memcpy(src, dst, OBJECT_SIZE);

	munmap(dst, OBJECT_SIZE);
	munmap(src, OBJECT_SIZE);
}

enum test_read_write {
	READ_BEFORE_WRITE,
	READ_AFTER_WRITE,
};

static void
test_read_write(int fd, enum test_read_write order)
{
	uint32_t handle;
	void *ptr;
	volatile uint32_t val = 0;

	handle = gem_create(fd, OBJECT_SIZE);

	ptr = gem_mmap__gtt(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);

	if (order == READ_BEFORE_WRITE) {
		val = *(uint32_t *)ptr;
		*(uint32_t *)ptr = val;
	} else {
		*(uint32_t *)ptr = val;
		val = *(uint32_t *)ptr;
	}

	gem_close(fd, handle);
	munmap(ptr, OBJECT_SIZE);
}

static void
test_read_write2(int fd, enum test_read_write order)
{
	uint32_t handle;
	void *r, *w;
	volatile uint32_t val = 0;

	handle = gem_create(fd, OBJECT_SIZE);

	r = gem_mmap__gtt(fd, handle, OBJECT_SIZE, PROT_READ);

	w = gem_mmap__gtt(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);

	if (order == READ_BEFORE_WRITE) {
		val = *(uint32_t *)r;
		*(uint32_t *)w = val;
	} else {
		*(uint32_t *)w = val;
		val = *(uint32_t *)r;
	}

	gem_close(fd, handle);
	munmap(r, OBJECT_SIZE);
	munmap(w, OBJECT_SIZE);
}

static void
test_write(int fd)
{
	void *src;
	uint32_t dst;

	/* copy from a fresh src to fresh dst to force pagefault on both */
	src = create_pointer(fd);
	dst = gem_create(fd, OBJECT_SIZE);

	gem_write(fd, dst, 0, src, OBJECT_SIZE);

	gem_close(fd, dst);
	munmap(src, OBJECT_SIZE);
}

static void
test_write_gtt(int fd)
{
	uint32_t dst;
	char *dst_gtt;
	void *src;

	dst = gem_create(fd, OBJECT_SIZE);

	/* prefault object into gtt */
	dst_gtt = mmap_bo(fd, dst);
	set_domain_gtt(fd, dst);
	memset(dst_gtt, 0, OBJECT_SIZE);
	munmap(dst_gtt, OBJECT_SIZE);

	src = create_pointer(fd);

	gem_write(fd, dst, 0, src, OBJECT_SIZE);

	gem_close(fd, dst);
	munmap(src, OBJECT_SIZE);
}

static void
test_huge_bo(int fd, int huge, int tiling)
{
	uint32_t bo;
	char *ptr;
	char *tiled_pattern;
	char *linear_pattern;
	uint64_t size, last_offset;
	int pitch = tiling == I915_TILING_Y ? 128 : 512;
	int i;

	switch (huge) {
	case -1:
		size = gem_mappable_aperture_size() / 2;
		break;
	case 0:
		size = gem_mappable_aperture_size() + PAGE_SIZE;
		break;
	default:
		size = gem_aperture_size(fd) + PAGE_SIZE;
		break;
	}
	intel_require_memory(1, size, CHECK_RAM);

	last_offset = size - PAGE_SIZE;

	/* Create pattern */
	bo = gem_create(fd, PAGE_SIZE);
	if (tiling)
		igt_require(__gem_set_tiling(fd, bo, tiling, pitch) == 0);
	linear_pattern = gem_mmap__gtt(fd, bo, PAGE_SIZE,
				       PROT_READ | PROT_WRITE);
	for (i = 0; i < PAGE_SIZE; i++)
		linear_pattern[i] = i;
	tiled_pattern = gem_mmap__cpu(fd, bo, 0, PAGE_SIZE, PROT_READ);

	gem_set_domain(fd, bo, I915_GEM_DOMAIN_CPU | I915_GEM_DOMAIN_GTT, 0);
	gem_close(fd, bo);

	bo = gem_create(fd, size);
	if (tiling)
		igt_require(__gem_set_tiling(fd, bo, tiling, pitch) == 0);

	/* Initialise first/last page through CPU mmap */
	ptr = gem_mmap__cpu(fd, bo, 0, size, PROT_READ | PROT_WRITE);
	memcpy(ptr, tiled_pattern, PAGE_SIZE);
	memcpy(ptr + last_offset, tiled_pattern, PAGE_SIZE);
	munmap(ptr, size);

	/* Obtain mapping for the object through GTT. */
	ptr = __gem_mmap__gtt(fd, bo, size, PROT_READ | PROT_WRITE);
	igt_require_f(ptr, "Huge BO GTT mapping not supported.\n");

	set_domain_gtt(fd, bo);

	/* Access through GTT should still provide the CPU written values. */
	igt_assert(memcmp(ptr              , linear_pattern, PAGE_SIZE) == 0);
	igt_assert(memcmp(ptr + last_offset, linear_pattern, PAGE_SIZE) == 0);

	gem_set_tiling(fd, bo, I915_TILING_NONE, 0);

	igt_assert(memcmp(ptr              , tiled_pattern, PAGE_SIZE) == 0);
	igt_assert(memcmp(ptr + last_offset, tiled_pattern, PAGE_SIZE) == 0);

	munmap(ptr, size);

	gem_close(fd, bo);
	munmap(tiled_pattern, PAGE_SIZE);
	munmap(linear_pattern, PAGE_SIZE);
}

static void
test_huge_copy(int fd, int huge, int tiling_a, int tiling_b)
{
	uint64_t huge_object_size, i;
	uint32_t bo, *pattern_a, *pattern_b;
	char *a, *b;

	switch (huge) {
	case -2:
		huge_object_size = gem_mappable_aperture_size() / 4;
		break;
	case -1:
		huge_object_size = gem_mappable_aperture_size() / 2;
		break;
	case 0:
		huge_object_size = gem_mappable_aperture_size() + PAGE_SIZE;
		break;
	default:
		huge_object_size = gem_aperture_size(fd) + PAGE_SIZE;
		break;
	}
	intel_require_memory(2, huge_object_size, CHECK_RAM);

	pattern_a = malloc(PAGE_SIZE);
	for (i = 0; i < PAGE_SIZE/4; i++)
		pattern_a[i] = i;

	pattern_b = malloc(PAGE_SIZE);
	for (i = 0; i < PAGE_SIZE/4; i++)
		pattern_b[i] = ~i;

	bo = gem_create(fd, huge_object_size);
	if (tiling_a)
		igt_require(__gem_set_tiling(fd, bo, tiling_a, tiling_a == I915_TILING_Y ? 128 : 512) == 0);
	a = __gem_mmap__gtt(fd, bo, huge_object_size, PROT_READ | PROT_WRITE);
	igt_require(a);
	gem_close(fd, bo);

	for (i = 0; i < huge_object_size / PAGE_SIZE; i++)
		memcpy(a + PAGE_SIZE*i, pattern_a, PAGE_SIZE);

	bo = gem_create(fd, huge_object_size);
	if (tiling_b)
		igt_require(__gem_set_tiling(fd, bo, tiling_b, tiling_b == I915_TILING_Y ? 128 : 512) == 0);
	b = __gem_mmap__gtt(fd, bo, huge_object_size, PROT_READ | PROT_WRITE);
	igt_require(b);
	gem_close(fd, bo);

	for (i = 0; i < huge_object_size / PAGE_SIZE; i++)
		memcpy(b + PAGE_SIZE*i, pattern_b, PAGE_SIZE);

	for (i = 0; i < huge_object_size / PAGE_SIZE; i++) {
		if (i & 1)
			memcpy(a + i *PAGE_SIZE, b + i*PAGE_SIZE, PAGE_SIZE);
		else
			memcpy(b + i *PAGE_SIZE, a + i*PAGE_SIZE, PAGE_SIZE);
	}

	for (i = 0; i < huge_object_size / PAGE_SIZE; i++) {
		if (i & 1)
			igt_assert(memcmp(pattern_b, a + PAGE_SIZE*i, PAGE_SIZE) == 0);
		else
			igt_assert(memcmp(pattern_a, a + PAGE_SIZE*i, PAGE_SIZE) == 0);
	}
	munmap(a, huge_object_size);

	for (i = 0; i < huge_object_size / PAGE_SIZE; i++) {
		if (i & 1)
			igt_assert(memcmp(pattern_b, b + PAGE_SIZE*i, PAGE_SIZE) == 0);
		else
			igt_assert(memcmp(pattern_a, b + PAGE_SIZE*i, PAGE_SIZE) == 0);
	}
	munmap(b, huge_object_size);

	free(pattern_a);
	free(pattern_b);
}

static void
test_read(int fd)
{
	void *dst;
	uint32_t src;

	/* copy from a fresh src to fresh dst to force pagefault on both */
	dst = create_pointer(fd);
	src = gem_create(fd, OBJECT_SIZE);

	gem_read(fd, src, 0, dst, OBJECT_SIZE);

	gem_close(fd, src);
	munmap(dst, OBJECT_SIZE);
}

static void
test_write_cpu_read_gtt(int fd)
{
	uint32_t handle;
	uint32_t *src, *dst;

	igt_require(gem_has_llc(fd));

	handle = gem_create(fd, OBJECT_SIZE);

	dst = gem_mmap__gtt(fd, handle, OBJECT_SIZE, PROT_READ);

	src = gem_mmap__cpu(fd, handle, 0, OBJECT_SIZE, PROT_WRITE);

	gem_close(fd, handle);

	memset(src, 0xaa, OBJECT_SIZE);
	igt_assert(memcmp(dst, src, OBJECT_SIZE) == 0);

	munmap(src, OBJECT_SIZE);
	munmap(dst, OBJECT_SIZE);
}

struct thread_fault_concurrent {
	pthread_t thread;
	int id;
	uint32_t **ptr;
};

static void *
thread_fault_concurrent(void *closure)
{
	struct thread_fault_concurrent *t = closure;
	uint32_t val = 0;
	int n;

	for (n = 0; n < 32; n++) {
		if (n & 1)
			*t->ptr[(n + t->id) % 32] = val;
		else
			val = *t->ptr[(n + t->id) % 32];
	}

	return NULL;
}

static void
test_fault_concurrent(int fd)
{
	uint32_t *ptr[32];
	struct thread_fault_concurrent thread[64];
	int n;

	for (n = 0; n < 32; n++) {
		ptr[n] = create_pointer(fd);
	}

	for (n = 0; n < 64; n++) {
		thread[n].ptr = ptr;
		thread[n].id = n;
		pthread_create(&thread[n].thread, NULL, thread_fault_concurrent, &thread[n]);
	}

	for (n = 0; n < 64; n++)
		pthread_join(thread[n].thread, NULL);

	for (n = 0; n < 32; n++) {
		munmap(ptr[n], OBJECT_SIZE);
	}
}

static void
run_without_prefault(int fd,
			void (*func)(int fd))
{
	igt_disable_prefault();
	func(fd);
	igt_enable_prefault();
}

int fd;

igt_main
{
	if (igt_run_in_simulation())
		OBJECT_SIZE = 1 * 1024 * 1024;

	igt_fixture
		fd = drm_open_driver(DRIVER_INTEL);

	igt_subtest("basic")
		test_access(fd);
	igt_subtest("basic-short")
		test_short(fd);
	igt_subtest("basic-copy")
		test_copy(fd);
	igt_subtest("basic-read")
		test_read(fd);
	igt_subtest("basic-write")
		test_write(fd);
	igt_subtest("basic-write-gtt")
		test_write_gtt(fd);
	igt_subtest("basic-read-write")
		test_read_write(fd, READ_BEFORE_WRITE);
	igt_subtest("basic-write-read")
		test_read_write(fd, READ_AFTER_WRITE);
	igt_subtest("basic-read-write-distinct")
		test_read_write2(fd, READ_BEFORE_WRITE);
	igt_subtest("basic-write-read-distinct")
		test_read_write2(fd, READ_AFTER_WRITE);
	igt_subtest("fault-concurrent")
		test_fault_concurrent(fd);
	igt_subtest("basic-read-no-prefault")
		run_without_prefault(fd, test_read);
	igt_subtest("basic-write-no-prefault")
		run_without_prefault(fd, test_write);
	igt_subtest("basic-write-gtt-no-prefault")
		run_without_prefault(fd, test_write_gtt);
	igt_subtest("basic-write-cpu-read-gtt")
		test_write_cpu_read_gtt(fd);

	igt_subtest("basic-small-bo")
		test_huge_bo(fd, -1, I915_TILING_NONE);
	igt_subtest("basic-small-bo-tiledX")
		test_huge_bo(fd, -1, I915_TILING_X);
	igt_subtest("basic-small-bo-tiledY")
		test_huge_bo(fd, -1, I915_TILING_Y);

	igt_subtest("big-bo")
		test_huge_bo(fd, 0, I915_TILING_NONE);
	igt_subtest("big-bo-tiledX")
		test_huge_bo(fd, 0, I915_TILING_X);
	igt_subtest("big-bo-tiledY")
		test_huge_bo(fd, 0, I915_TILING_Y);

	igt_subtest("huge-bo")
		test_huge_bo(fd, 1, I915_TILING_NONE);
	igt_subtest("huge-bo-tiledX")
		test_huge_bo(fd, 1, I915_TILING_X);
	igt_subtest("huge-bo-tiledY")
		test_huge_bo(fd, 1, I915_TILING_Y);

	igt_subtest("basic-small-copy")
		test_huge_copy(fd, -2, I915_TILING_NONE, I915_TILING_NONE);
	igt_subtest("basic-small-copy-XY")
		test_huge_copy(fd, -2, I915_TILING_X, I915_TILING_Y);
	igt_subtest("medium-copy")
		test_huge_copy(fd, -1, I915_TILING_NONE, I915_TILING_NONE);
	igt_subtest("medium-copy-XY")
		test_huge_copy(fd, -1, I915_TILING_X, I915_TILING_Y);
	igt_subtest("big-copy")
		test_huge_copy(fd, 0, I915_TILING_NONE, I915_TILING_NONE);
	igt_subtest("big-copy-XY")
		test_huge_copy(fd, 0, I915_TILING_X, I915_TILING_Y);
	igt_subtest("huge-copy")
		test_huge_copy(fd, 1, I915_TILING_NONE, I915_TILING_NONE);
	igt_subtest("huge-copy-XY")
		test_huge_copy(fd, 1, I915_TILING_X, I915_TILING_Y);

	igt_fixture
		close(fd);
}
