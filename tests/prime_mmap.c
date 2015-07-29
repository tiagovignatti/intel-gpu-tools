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
 *    Rob Bradford <rob at linux.intel.com>
 *
 */

/*
 * Testcase: Check whether mmap()ing dma-buf works
 */
#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <pthread.h>

#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "igt_debugfs.h"
#include "ioctl_wrappers.h"

#define BO_SIZE (16*1024)

static int fd;

char pattern[] = {0xff, 0x00, 0x00, 0x00,
	0x00, 0xff, 0x00, 0x00,
	0x00, 0x00, 0xff, 0x00,
	0x00, 0x00, 0x00, 0xff};

static void
fill_bo(uint32_t handle, size_t size)
{
	off_t i;
	for (i = 0; i < size; i+=sizeof(pattern))
	{
		gem_write(fd, handle, i, pattern, sizeof(pattern));
	}
}

static void
test_correct(void)
{
	int dma_buf_fd;
	char *ptr1, *ptr2;
	uint32_t handle;

	handle = gem_create(fd, BO_SIZE);
	fill_bo(handle, BO_SIZE);

	dma_buf_fd = prime_handle_to_fd(fd, handle);
	igt_assert(errno == 0);

	/* Check correctness vs GEM_MMAP_GTT */
	ptr1 = gem_mmap(fd, handle, BO_SIZE, PROT_READ);
	ptr2 = mmap(NULL, BO_SIZE, PROT_READ, MAP_SHARED, dma_buf_fd, 0);
	igt_assert(ptr1 != MAP_FAILED);
	igt_assert(ptr2 != MAP_FAILED);
	igt_assert(memcmp(ptr1, ptr2, BO_SIZE) == 0);

	/* Check pattern correctness */
	igt_assert(memcmp(ptr2, pattern, sizeof(pattern)) == 0);

	munmap(ptr1, BO_SIZE);
	munmap(ptr2, BO_SIZE);
	close(dma_buf_fd);
	gem_close(fd, handle);
}

static void
test_map_unmap(void)
{
	int dma_buf_fd;
	char *ptr;
	uint32_t handle;

	handle = gem_create(fd, BO_SIZE);
	fill_bo(handle, BO_SIZE);

	dma_buf_fd = prime_handle_to_fd(fd, handle);
	igt_assert(errno == 0);

	ptr = mmap(NULL, BO_SIZE, PROT_READ, MAP_SHARED, dma_buf_fd, 0);
	igt_assert(ptr != MAP_FAILED);
	igt_assert(memcmp(ptr, pattern, sizeof(pattern)) == 0);

	/* Unmap and remap */
	munmap(ptr, BO_SIZE);
	ptr = mmap(NULL, BO_SIZE, PROT_READ, MAP_SHARED, dma_buf_fd, 0);
	igt_assert(ptr != MAP_FAILED);
	igt_assert(memcmp(ptr, pattern, sizeof(pattern)) == 0);

	munmap(ptr, BO_SIZE);
	close(dma_buf_fd);
	gem_close(fd, handle);
}

/* prime and then unprime and then prime again the same handle */
static void
test_reprime(void)
{
	int dma_buf_fd;
	char *ptr;
	uint32_t handle;

	handle = gem_create(fd, BO_SIZE);
	fill_bo(handle, BO_SIZE);

	dma_buf_fd = prime_handle_to_fd(fd, handle);
	igt_assert(errno == 0);

	ptr = mmap(NULL, BO_SIZE, PROT_READ, MAP_SHARED, dma_buf_fd, 0);
	igt_assert(ptr != MAP_FAILED);
	igt_assert(memcmp(ptr, pattern, sizeof(pattern)) == 0);

	close (dma_buf_fd);
	igt_assert(memcmp(ptr, pattern, sizeof(pattern)) == 0);
	munmap(ptr, BO_SIZE);

	dma_buf_fd = prime_handle_to_fd(fd, handle);
	ptr = mmap(NULL, BO_SIZE, PROT_READ, MAP_SHARED, dma_buf_fd, 0);
	igt_assert(ptr != MAP_FAILED);
	igt_assert(memcmp(ptr, pattern, sizeof(pattern)) == 0);

	munmap(ptr, BO_SIZE);
	close(dma_buf_fd);
	gem_close(fd, handle);
}

/* map from another process */
static void
test_forked(void)
{
	int dma_buf_fd;
	char *ptr;
	uint32_t handle;

	handle = gem_create(fd, BO_SIZE);
	fill_bo(handle, BO_SIZE);

	dma_buf_fd = prime_handle_to_fd(fd, handle);
	igt_assert(errno == 0);

	igt_fork(childno, 1) {
		ptr = mmap(NULL, BO_SIZE, PROT_READ, MAP_SHARED, dma_buf_fd, 0);
		igt_assert(ptr != MAP_FAILED);
		igt_assert(memcmp(ptr, pattern, sizeof(pattern)) == 0);
		munmap(ptr, BO_SIZE);
		close(dma_buf_fd);
	}
	close(dma_buf_fd);
	igt_waitchildren();
	gem_close(fd, handle);
}

static void
test_refcounting(void)
{
	int dma_buf_fd;
	char *ptr;
	uint32_t handle;

	handle = gem_create(fd, BO_SIZE);
	fill_bo(handle, BO_SIZE);

	dma_buf_fd = prime_handle_to_fd(fd, handle);
	igt_assert(errno == 0);
	/* Close gem object before mapping */
	gem_close(fd, handle);

	ptr = mmap(NULL, BO_SIZE, PROT_READ, MAP_SHARED, dma_buf_fd, 0);
	igt_assert(ptr != MAP_FAILED);
	igt_assert(memcmp(ptr, pattern, sizeof(pattern)) == 0);
	munmap(ptr, BO_SIZE);
	close (dma_buf_fd);
}

/* dup before mmap */
static void
test_dup(void)
{
	int dma_buf_fd;
	char *ptr;
	uint32_t handle;

	handle = gem_create(fd, BO_SIZE);
	fill_bo(handle, BO_SIZE);

	dma_buf_fd = dup(prime_handle_to_fd(fd, handle));
	igt_assert(errno == 0);

	ptr = mmap(NULL, BO_SIZE, PROT_READ, MAP_SHARED, dma_buf_fd, 0);
	igt_assert(ptr != MAP_FAILED);
	igt_assert(memcmp(ptr, pattern, sizeof(pattern)) == 0);
	munmap(ptr, BO_SIZE);
	gem_close(fd, handle);
	close (dma_buf_fd);
}


/* Used for error case testing to avoid wrapper */
static int prime_handle_to_fd_no_assert(uint32_t handle, int *fd_out)
{
	struct drm_prime_handle args;
	int ret;

	args.handle = handle;
	args.flags = DRM_CLOEXEC;
	args.fd = -1;

	ret = drmIoctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);

	*fd_out = args.fd;

	return ret;
}

static void
test_errors(void)
{
	int dma_buf_fd;
	char *ptr;
	uint32_t handle;

	/* Close gem object before priming */
	handle = gem_create(fd, BO_SIZE);
	fill_bo(handle, BO_SIZE);
	gem_close(fd, handle);
	prime_handle_to_fd_no_assert(handle, &dma_buf_fd);
	igt_assert(dma_buf_fd == -1 && errno == ENOENT);
	errno = 0;

	/* close fd before mapping */
	handle = gem_create(fd, BO_SIZE);
	fill_bo(handle, BO_SIZE);
	dma_buf_fd = prime_handle_to_fd(fd, handle);
	igt_assert(errno == 0);
	close(dma_buf_fd);
	ptr = mmap(NULL, BO_SIZE, PROT_READ, MAP_SHARED, dma_buf_fd, 0);
	igt_assert(ptr == MAP_FAILED && errno == EBADF);
	errno = 0;
	gem_close(fd, handle);

	/* Map too big */
	handle = gem_create(fd, BO_SIZE);
	fill_bo(handle, BO_SIZE);
	dma_buf_fd = prime_handle_to_fd(fd, handle);
	igt_assert(errno == 0);
	ptr = mmap(NULL, BO_SIZE * 2, PROT_READ, MAP_SHARED, dma_buf_fd, 0);
	igt_assert(ptr == MAP_FAILED && errno == EINVAL);
	errno = 0;
	close(dma_buf_fd);
	gem_close(fd, handle);
}

static void
test_aperture_limit(void)
{
	int dma_buf_fd1, dma_buf_fd2;
	char *ptr1, *ptr2;
	uint32_t handle1, handle2;
	/* Two buffers the sum of which > mappable aperture */
	uint64_t size1 = (gem_mappable_aperture_size() * 7) / 8;
	uint64_t size2 = (gem_mappable_aperture_size() * 3) / 8;

	handle1 = gem_create(fd, size1);
	fill_bo(handle1, BO_SIZE);

	dma_buf_fd1 = prime_handle_to_fd(fd, handle1);
	igt_assert(errno == 0);
	ptr1 = mmap(NULL, size1, PROT_READ, MAP_SHARED, dma_buf_fd1, 0);
	igt_assert(ptr1 != MAP_FAILED);
	igt_assert(memcmp(ptr1, pattern, sizeof(pattern)) == 0);

	handle2 = gem_create(fd, size1);
	fill_bo(handle2, BO_SIZE);
	dma_buf_fd2 = prime_handle_to_fd(fd, handle2);
	igt_assert(errno == 0);
	ptr2 = mmap(NULL, size2, PROT_READ, MAP_SHARED, dma_buf_fd2, 0);
	igt_assert(ptr2 != MAP_FAILED);
	igt_assert(memcmp(ptr2, pattern, sizeof(pattern)) == 0);

	igt_assert(memcmp(ptr1, ptr2, BO_SIZE) == 0);

	munmap(ptr1, size1);
	munmap(ptr2, size2);
	close(dma_buf_fd1);
	close(dma_buf_fd2);
	gem_close(fd, handle1);
	gem_close(fd, handle2);
}

static int
check_for_dma_buf_mmap(void)
{
	int dma_buf_fd;
	char *ptr;
	uint32_t handle;
	int ret = 1;

	handle = gem_create(fd, BO_SIZE);
	dma_buf_fd = prime_handle_to_fd(fd, handle);
	ptr = mmap(NULL, BO_SIZE, PROT_READ, MAP_SHARED, dma_buf_fd, 0);
	if (ptr != MAP_FAILED)
		ret = 0;
	munmap(ptr, BO_SIZE);
	gem_close(fd, handle);
	close (dma_buf_fd);
	return ret;
}

igt_main
{
	struct {
		const char *name;
		void (*fn)(void);
	} tests[] = {
		{ "test_correct", test_correct },
		{ "test_map_unmap", test_map_unmap },
		{ "test_reprime", test_reprime },
		{ "test_forked", test_forked },
		{ "test_refcounting", test_refcounting },
		{ "test_dup", test_dup },
		{ "test_errors", test_errors },
		{ "test_aperture_limit", test_aperture_limit },
	};
	int i;

	igt_fixture {
		fd = drm_open_any();
		errno = 0;
	}

	igt_skip_on((check_for_dma_buf_mmap() != 0));

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		igt_subtest(tests[i].name)
			tests[i].fn();
	}

	igt_fixture
		close(fd);
}
