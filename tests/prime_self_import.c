/*
 * Copyright © 2012-2013 Intel Corporation
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
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

/*
 * Testcase: Check whether prime import/export works on the same device
 *
 * ... but with different fds, i.e. the wayland usecase.
 */

#define _GNU_SOURCE
#include "igt.h"
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

IGT_TEST_DESCRIPTION("Check whether prime import/export works on the same"
		     " device... but with different fds.");

#define BO_SIZE (16*1024)

static char counter;
volatile int pls_die = 0;

static void
check_bo(int fd1, uint32_t handle1, int fd2, uint32_t handle2)
{
	char *ptr1, *ptr2;
	int i;

	ptr1 = gem_mmap__gtt(fd1, handle1, BO_SIZE, PROT_READ | PROT_WRITE);
	ptr2 = gem_mmap__gtt(fd2, handle2, BO_SIZE, PROT_READ | PROT_WRITE);

	/* check whether it's still our old object first. */
	for (i = 0; i < BO_SIZE; i++) {
		igt_assert(ptr1[i] == counter);
		igt_assert(ptr2[i] == counter);
	}

	counter++;

	memset(ptr1, counter, BO_SIZE);
	igt_assert(memcmp(ptr1, ptr2, BO_SIZE) == 0);

	munmap(ptr1, BO_SIZE);
	munmap(ptr2, BO_SIZE);
}

static void test_with_fd_dup(void)
{
	int fd1, fd2;
	uint32_t handle, handle_import;
	int dma_buf_fd1, dma_buf_fd2;

	counter = 0;

	fd1 = drm_open_driver(DRIVER_INTEL);
	fd2 = drm_open_driver(DRIVER_INTEL);

	handle = gem_create(fd1, BO_SIZE);

	dma_buf_fd1 = prime_handle_to_fd(fd1, handle);
	gem_close(fd1, handle);

	dma_buf_fd2 = dup(dma_buf_fd1);
	close(dma_buf_fd1);
	handle_import = prime_fd_to_handle(fd2, dma_buf_fd2);
	check_bo(fd2, handle_import, fd2, handle_import);

	close(dma_buf_fd2);
	check_bo(fd2, handle_import, fd2, handle_import);

	close(fd1);
	close(fd2);
}

static void test_with_two_bos(void)
{
	int fd1, fd2;
	uint32_t handle1, handle2, handle_import;
	int dma_buf_fd;

	counter = 0;

	fd1 = drm_open_driver(DRIVER_INTEL);
	fd2 = drm_open_driver(DRIVER_INTEL);

	handle1 = gem_create(fd1, BO_SIZE);
	handle2 = gem_create(fd1, BO_SIZE);

	dma_buf_fd = prime_handle_to_fd(fd1, handle1);
	handle_import = prime_fd_to_handle(fd2, dma_buf_fd);

	close(dma_buf_fd);
	gem_close(fd1, handle1);

	dma_buf_fd = prime_handle_to_fd(fd1, handle2);
	handle_import = prime_fd_to_handle(fd2, dma_buf_fd);
	check_bo(fd1, handle2, fd2, handle_import);

	gem_close(fd1, handle2);
	close(dma_buf_fd);

	check_bo(fd2, handle_import, fd2, handle_import);

	close(fd1);
	close(fd2);
}

static void test_with_one_bo_two_files(void)
{
	int fd1, fd2;
	uint32_t handle_import, handle_open, handle_orig, flink_name;
	int dma_buf_fd1, dma_buf_fd2;

	fd1 = drm_open_driver(DRIVER_INTEL);
	fd2 = drm_open_driver(DRIVER_INTEL);

	handle_orig = gem_create(fd1, BO_SIZE);
	dma_buf_fd1 = prime_handle_to_fd(fd1, handle_orig);

	flink_name = gem_flink(fd1, handle_orig);
	handle_open = gem_open(fd2, flink_name);

	dma_buf_fd2 = prime_handle_to_fd(fd2, handle_open);
	handle_import = prime_fd_to_handle(fd2, dma_buf_fd2);

	/* dma-buf self importing an flink bo should give the same handle */
	igt_assert_eq_u32(handle_import, handle_open);

	close(fd1);
	close(fd2);
	close(dma_buf_fd1);
	close(dma_buf_fd2);
}

static void test_with_one_bo(void)
{
	int fd1, fd2;
	uint32_t handle, handle_import1, handle_import2, handle_selfimport;
	int dma_buf_fd;

	fd1 = drm_open_driver(DRIVER_INTEL);
	fd2 = drm_open_driver(DRIVER_INTEL);

	handle = gem_create(fd1, BO_SIZE);

	dma_buf_fd = prime_handle_to_fd(fd1, handle);
	handle_import1 = prime_fd_to_handle(fd2, dma_buf_fd);

	check_bo(fd1, handle, fd2, handle_import1);

	/* reimport should give us the same handle so that userspace can check
	 * whether it has that bo already somewhere. */
	handle_import2 = prime_fd_to_handle(fd2, dma_buf_fd);
	igt_assert_eq_u32(handle_import1, handle_import2);

	/* Same for re-importing on the exporting fd. */
	handle_selfimport = prime_fd_to_handle(fd1, dma_buf_fd);
	igt_assert_eq_u32(handle, handle_selfimport);

	/* close dma_buf, check whether nothing disappears. */
	close(dma_buf_fd);
	check_bo(fd1, handle, fd2, handle_import1);

	gem_close(fd1, handle);
	check_bo(fd2, handle_import1, fd2, handle_import1);

	/* re-import into old exporter */
	dma_buf_fd = prime_handle_to_fd(fd2, handle_import1);
	/* but drop all references to the obj in between */
	gem_close(fd2, handle_import1);
	handle = prime_fd_to_handle(fd1, dma_buf_fd);
	handle_import1 = prime_fd_to_handle(fd2, dma_buf_fd);
	check_bo(fd1, handle, fd2, handle_import1);

	/* Completely rip out exporting fd. */
	close(fd1);
	check_bo(fd2, handle_import1, fd2, handle_import1);
}

static void *thread_fn_reimport_vs_close(void *p)
{
	struct drm_gem_close close_bo;
	int *fds = p;
	int fd = fds[0];
	int dma_buf_fd = fds[1];
	uint32_t handle;

	while (!pls_die) {
		handle = prime_fd_to_handle(fd, dma_buf_fd);

		close_bo.handle = handle;
		ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_bo);
	}

	return (void *)0;
}

static void test_reimport_close_race(void)
{
	pthread_t *threads;
	int r, i, num_threads;
	int fds[2];
	int obj_count;
	void *status;
	uint32_t handle;
	int fake;

	/* Allocate exit handler fds in here so that we dont screw
	 * up the counts */
	fake = drm_open_driver(DRIVER_INTEL);

	obj_count = igt_get_stable_obj_count(fake);

	num_threads = sysconf(_SC_NPROCESSORS_ONLN);

	threads = calloc(num_threads, sizeof(pthread_t));

	fds[0] = drm_open_driver(DRIVER_INTEL);

	handle = gem_create(fds[0], BO_SIZE);

	fds[1] = prime_handle_to_fd(fds[0], handle);

	for (i = 0; i < num_threads; i++) {
		r = pthread_create(&threads[i], NULL,
				   thread_fn_reimport_vs_close,
				   (void *)(uintptr_t)fds);
		igt_assert_eq(r, 0);
	}

	sleep(5);

	pls_die = 1;

	for (i = 0;  i < num_threads; i++) {
		pthread_join(threads[i], &status);
		igt_assert(status == 0);
	}

	close(fds[0]);
	close(fds[1]);

	obj_count = igt_get_stable_obj_count(fake) - obj_count;

	igt_info("leaked %i objects\n", obj_count);

	close(fake);

	igt_assert_eq(obj_count, 0);
}

static void *thread_fn_export_vs_close(void *p)
{
	struct drm_prime_handle prime_h2f;
	struct drm_gem_close close_bo;
	int fd = (uintptr_t)p;
	uint32_t handle;

	while (!pls_die) {
		/* We want to race gem close against prime export on handle one.*/
		handle = gem_create(fd, 4096);
		if (handle != 1)
			gem_close(fd, handle);

		/* raw ioctl since we expect this to fail */

		/* WTF: for gem_flink_race I've unconditionally used handle == 1
		 * here, but with prime it seems to help a _lot_ to use
		 * something more random. */
		prime_h2f.handle = 1;
		prime_h2f.flags = DRM_CLOEXEC;
		prime_h2f.fd = -1;

		ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime_h2f);

		close_bo.handle = 1;
		ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_bo);

		close(prime_h2f.fd);
	}

	return (void *)0;
}

static void test_export_close_race(void)
{
	pthread_t *threads;
	int r, i, num_threads;
	int fd;
	int obj_count;
	void *status;
	int fake;

	num_threads = sysconf(_SC_NPROCESSORS_ONLN);

	threads = calloc(num_threads, sizeof(pthread_t));

	/* Allocate exit handler fds in here so that we dont screw
	 * up the counts */
	fake = drm_open_driver(DRIVER_INTEL);

	obj_count = igt_get_stable_obj_count(fake);

	fd = drm_open_driver(DRIVER_INTEL);

	for (i = 0; i < num_threads; i++) {
		r = pthread_create(&threads[i], NULL,
				   thread_fn_export_vs_close,
				   (void *)(uintptr_t)fd);
		igt_assert_eq(r, 0);
	}

	sleep(5);

	pls_die = 1;

	for (i = 0;  i < num_threads; i++) {
		pthread_join(threads[i], &status);
		igt_assert(status == 0);
	}

	close(fd);

	obj_count = igt_get_stable_obj_count(fake) - obj_count;

	igt_info("leaked %i objects\n", obj_count);

	close(fake);

	igt_assert_eq(obj_count, 0);
}

static void test_llseek_size(void)
{
	int fd, i;
	uint32_t handle;
	int dma_buf_fd;

	counter = 0;

	fd = drm_open_driver(DRIVER_INTEL);


	for (i = 0; i < 10; i++) {
		int bufsz = 4096 << i;

		handle = gem_create(fd, bufsz);
		dma_buf_fd = prime_handle_to_fd(fd, handle);

		gem_close(fd, handle);

		igt_assert(prime_get_size(dma_buf_fd) == bufsz);

		close(dma_buf_fd);
	}

	close(fd);
}

static void test_llseek_bad(void)
{
	int fd;
	uint32_t handle;
	int dma_buf_fd;

	counter = 0;

	fd = drm_open_driver(DRIVER_INTEL);


	handle = gem_create(fd, BO_SIZE);
	dma_buf_fd = prime_handle_to_fd(fd, handle);

	gem_close(fd, handle);

	igt_require(lseek(dma_buf_fd, 0, SEEK_END) >= 0);

	igt_assert(lseek(dma_buf_fd, -1, SEEK_END) == -1 && errno == EINVAL);
	igt_assert(lseek(dma_buf_fd, 1, SEEK_SET) == -1 && errno == EINVAL);
	igt_assert(lseek(dma_buf_fd, BO_SIZE, SEEK_SET) == -1 && errno == EINVAL);
	igt_assert(lseek(dma_buf_fd, BO_SIZE + 1, SEEK_SET) == -1 && errno == EINVAL);
	igt_assert(lseek(dma_buf_fd, BO_SIZE - 1, SEEK_SET) == -1 && errno == EINVAL);

	close(dma_buf_fd);

	close(fd);
}

igt_main
{
	struct {
		const char *name;
		void (*fn)(void);
	} tests[] = {
		{ "basic-with_one_bo", test_with_one_bo },
		{ "basic-with_one_bo_two_files", test_with_one_bo_two_files },
		{ "basic-with_two_bos", test_with_two_bos },
		{ "basic-with_fd_dup", test_with_fd_dup },
		{ "export-vs-gem_close-race", test_export_close_race },
		{ "reimport-vs-gem_close-race", test_reimport_close_race },
		{ "basic-llseek-size", test_llseek_size },
		{ "basic-llseek-bad", test_llseek_bad },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		igt_subtest(tests[i].name)
			tests[i].fn();
	}
}
