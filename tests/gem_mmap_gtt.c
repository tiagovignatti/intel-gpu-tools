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
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "igt_debugfs.h"

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

static int OBJECT_SIZE = 16*1024*1024;

static void
set_domain_gtt(int fd, uint32_t handle)
{
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
}

static void
set_domain_cpu(int fd, uint32_t handle)
{
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
}

static void
pread_bo(int fd, int handle, void *buf, int offset, int size)
{
	struct drm_i915_gem_pread gem_pread;

	memset(&gem_pread, 0, sizeof(gem_pread));
	gem_pread.handle = handle;
	gem_pread.data_ptr = (uintptr_t)buf;
	gem_pread.size = size;
	gem_pread.offset = offset;

	igt_assert(ioctl(fd, DRM_IOCTL_I915_GEM_PREAD, &gem_pread) == 0);
}

static void *
mmap_bo(int fd, uint32_t handle)
{
	void *ptr;

	ptr = gem_mmap(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);
	igt_assert(ptr != MAP_FAILED);

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

	fd2 = drm_open_any();

	/* Check that fd1 can mmap. */
	mmap_arg.handle = handle;
	igt_assert(drmIoctl(fd,
			    DRM_IOCTL_I915_GEM_MMAP_GTT,
			    &mmap_arg) == 0);

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

	igt_assert(drmIoctl(fd,
			    DRM_IOCTL_I915_GEM_MMAP_GTT,
			    &mmap_arg) == 0);
	for (pages = 1; pages <= OBJECT_SIZE / 4096; pages <<= 1) {
		uint8_t *r, *w;

		w = mmap64(0, pages * 4096, PROT_READ | PROT_WRITE,
			   MAP_SHARED, fd, mmap_arg.offset);
		igt_assert(w != MAP_FAILED);

		r = mmap64(0, pages * 4096, PROT_READ,
			   MAP_SHARED, fd, mmap_arg.offset);
		igt_assert(r != MAP_FAILED);

		for (p = 0; p < pages; p++) {
			w[4096*p] = r[4096*p];
			w[4096*p+4095] = r[4096*p+4095];
		}

		munmap(r, pages * 4096);
		munmap(w, pages * 4096);
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

	ptr = gem_mmap(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);
	igt_assert(ptr != MAP_FAILED);

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

	r = gem_mmap(fd, handle, OBJECT_SIZE, PROT_READ);
	igt_assert(r != MAP_FAILED);

	w = gem_mmap(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);
	igt_assert(w != MAP_FAILED);

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
test_huge_bo(int fd)
{
	uint32_t bo;
	char *ptr_cpu;
	char *ptr_gtt;
	char *cpu_pattern;
	char *gtt_pattern;
	uint64_t mappable_aperture_pages = gem_mappable_aperture_size() /
					   PAGE_SIZE;
	uint64_t huge_object_size = (mappable_aperture_pages + 1) * PAGE_SIZE;
	uint64_t last_offset = huge_object_size - PAGE_SIZE;

	cpu_pattern = malloc(PAGE_SIZE);
	gtt_pattern = malloc(PAGE_SIZE);
	igt_assert(cpu_pattern && gtt_pattern);
	memset(cpu_pattern,  0xaa, PAGE_SIZE);
	memset(gtt_pattern, ~0xaa, PAGE_SIZE);

	bo = gem_create(fd, huge_object_size);

	/* Obtain CPU mapping for the object. */
	ptr_cpu = gem_mmap__cpu(fd, bo, 0, huge_object_size,
				PROT_READ | PROT_WRITE);
	igt_assert(ptr_cpu);

	set_domain_cpu(fd, bo);

	/* Write first page through the mapping and assert reading it back
	 * works. */
	memcpy(ptr_cpu, cpu_pattern, PAGE_SIZE);
	igt_assert(memcmp(ptr_cpu, cpu_pattern, PAGE_SIZE) == 0);

	/* Write last page through the mapping and assert reading it back
	 * works. */
	memcpy(ptr_cpu + last_offset, cpu_pattern, PAGE_SIZE);
	igt_assert(memcmp(ptr_cpu + last_offset, cpu_pattern, PAGE_SIZE) == 0);

	/* Cross check that accessing two simultaneous pages works. */
	igt_assert(memcmp(ptr_cpu, ptr_cpu + last_offset, PAGE_SIZE) == 0);

	munmap(ptr_cpu, huge_object_size);
	ptr_cpu = NULL;

	/* Obtain mapping for the object through GTT. */
	ptr_gtt = gem_mmap__gtt(fd, bo, huge_object_size,
			        PROT_READ | PROT_WRITE);
	igt_require_f(ptr_gtt, "Huge BO GTT mapping not supported.\n");

	set_domain_gtt(fd, bo);

	/* Access through GTT should still provide the CPU written values. */
	igt_assert(memcmp(ptr_gtt              , cpu_pattern, PAGE_SIZE) == 0);
	igt_assert(memcmp(ptr_gtt + last_offset, cpu_pattern, PAGE_SIZE) == 0);

	/* Try replacing first page through GTT mapping and make sure other page
	 * stays intact. */
	memcpy(ptr_gtt, gtt_pattern, PAGE_SIZE);
	igt_assert(memcmp(ptr_gtt              , gtt_pattern, PAGE_SIZE) == 0);
	igt_assert(memcmp(ptr_gtt + last_offset, cpu_pattern, PAGE_SIZE) == 0);

	/* And make sure that after writing, both pages contain what they
	 * should.*/
	memcpy(ptr_gtt + last_offset, gtt_pattern, PAGE_SIZE);
	igt_assert(memcmp(ptr_gtt              , gtt_pattern, PAGE_SIZE) == 0);
	igt_assert(memcmp(ptr_gtt + last_offset, gtt_pattern, PAGE_SIZE) == 0);

	munmap(ptr_gtt, huge_object_size);
	ptr_gtt = NULL;

	/* Verify the page contents after GTT writes by reading without mapping.
	 * Mapping to CPU domain is avoided not to cause a huge flush.
	 */
	pread_bo(fd, bo, cpu_pattern, 0, PAGE_SIZE);
	igt_assert(memcmp(cpu_pattern, gtt_pattern, PAGE_SIZE) == 0);

	memset(cpu_pattern, 0x00, PAGE_SIZE);

	pread_bo(fd, bo, cpu_pattern, last_offset, PAGE_SIZE);
	igt_assert(memcmp(cpu_pattern, gtt_pattern, PAGE_SIZE) == 0);

	gem_close(fd, bo);
	free(cpu_pattern);
	free(gtt_pattern);
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

	dst = gem_mmap(fd, handle, OBJECT_SIZE, PROT_READ);
	igt_assert(dst != (uint32_t *)MAP_FAILED);

	src = gem_mmap__cpu(fd, handle, 0, OBJECT_SIZE, PROT_WRITE);
	igt_assert(src != (uint32_t *)MAP_FAILED);

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
		fd = drm_open_any();

	igt_subtest("access")
		test_access(fd);
	igt_subtest("short")
		test_short(fd);
	igt_subtest("copy")
		test_copy(fd);
	igt_subtest("read")
		test_read(fd);
	igt_subtest("write")
		test_write(fd);
	igt_subtest("write-gtt")
		test_write_gtt(fd);
	igt_subtest("read-write")
		test_read_write(fd, READ_BEFORE_WRITE);
	igt_subtest("write-read")
		test_read_write(fd, READ_AFTER_WRITE);
	igt_subtest("read-write-distinct")
		test_read_write2(fd, READ_BEFORE_WRITE);
	igt_subtest("write-read-distinct")
		test_read_write2(fd, READ_AFTER_WRITE);
	igt_subtest("fault-concurrent")
		test_fault_concurrent(fd);
	igt_subtest("read-no-prefault")
		run_without_prefault(fd, test_read);
	igt_subtest("write-no-prefault")
		run_without_prefault(fd, test_write);
	igt_subtest("write-gtt-no-prefault")
		run_without_prefault(fd, test_write_gtt);
	igt_subtest("write-cpu-read-gtt")
		test_write_cpu_read_gtt(fd);
	igt_subtest("huge-bo")
		test_huge_bo(fd);

	igt_fixture
		close(fd);
}
