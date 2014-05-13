/*
 * Copyright (c) 2013 Intel Corporation
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
 */

#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#include "drmtest.h"
#include "ioctl_wrappers.h"
#include "intel_bufmgr.h"
#include "igt_debugfs.h"

/* Testcase: check for flink/open vs. gem close races
 *
 * The gem flink open ioctl had a little race with gem close which could result
 * in the flink name and corresponding reference getting leaked.
 */

/* We want lockless and I'm to lazy to dig out an atomic libarary. On x86 this
 * works, too. */
volatile int pls_die = 0;
int fd;

static int get_object_count(void)
{
	FILE *file;
	int ret, scanned;
	int device = drm_get_card();
	char *path;

	igt_drop_caches_set(DROP_RETIRE);

	ret = asprintf(&path, "/sys/kernel/debug/dri/%d/i915_gem_objects", device);
	igt_assert(ret != -1);

	file = fopen(path, "r");

	scanned = fscanf(file, "%i objects", &ret);
	igt_assert(scanned == 1);

	return ret;
}


static void *thread_fn_flink_name(void *p)
{
	struct drm_gem_open open_struct;
	int ret;

	while (!pls_die) {
		memset(&open_struct, 0, sizeof(open_struct));

		open_struct.name = 1;
		ret = ioctl(fd, DRM_IOCTL_GEM_OPEN, &open_struct);
		if (ret == 0) {
			uint32_t name = gem_flink(fd, open_struct.handle);

			igt_assert(name == 1);

			gem_close(fd, open_struct.handle);
		} else
			igt_assert(errno == ENOENT);
	}

	return (void *)0;
}

static void test_flink_name(void)
{
	pthread_t *threads;
	int r, i, num_threads;
	void *status;

	num_threads = sysconf(_SC_NPROCESSORS_ONLN) - 1;
	if (!num_threads)
		num_threads = 1;

	threads = calloc(num_threads, sizeof(pthread_t));

	fd = drm_open_any();
	igt_assert(fd >= 0);

	for (i = 0; i < num_threads; i++) {
		r = pthread_create(&threads[i], NULL,
				   thread_fn_flink_name, NULL);
		igt_assert(r == 0);
	}

	for (i = 0; i < 1000000; i++) {
		uint32_t handle;

		handle = gem_create(fd, 4096);

		gem_flink(fd, handle);

		gem_close(fd, handle);
	}

	pls_die = 1;

	for (i = 0;  i < num_threads; i++) {
		pthread_join(threads[i], &status);
		igt_assert(status == 0);
	}

	close(fd);
}

static void *thread_fn_flink_close(void *p)
{
	struct drm_gem_flink flink;
	struct drm_gem_close close_bo;
	uint32_t handle;

	while (!pls_die) {
		/* We want to race gem close against flink on handle one.*/
		handle = gem_create(fd, 4096);
		if (handle != 1)
			gem_close(fd, handle);

		/* raw ioctl since we expect this to fail */
		flink.handle = 1;
		ioctl(fd, DRM_IOCTL_GEM_FLINK, &flink);

		close_bo.handle = 1;
		ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_bo);
	}

	return (void *)0;
}

static void test_flink_close(void)
{
	pthread_t *threads;
	int r, i, num_threads;
	int obj_count;
	void *status;
	int fake;

	/* Allocate exit handler fds in here so that we dont screw
	 * up the counts */
	fake = drm_open_any();

	obj_count = get_object_count();

	num_threads = sysconf(_SC_NPROCESSORS_ONLN);

	threads = calloc(num_threads, sizeof(pthread_t));

	fd = drm_open_any();
	igt_assert(fd >= 0);

	for (i = 0; i < num_threads; i++) {
		r = pthread_create(&threads[i], NULL,
				   thread_fn_flink_close, NULL);
		igt_assert(r == 0);
	}

	sleep(5);

	pls_die = 1;

	for (i = 0;  i < num_threads; i++) {
		pthread_join(threads[i], &status);
		igt_assert(status == 0);
	}

	close(fd);

	obj_count = get_object_count() - obj_count;

	igt_info("leaked %i objects\n", obj_count);

	close(fake);

	igt_assert(obj_count == 0);
}

igt_main
{
	igt_skip_on_simulation();

	igt_subtest("flink_name")
		test_flink_name();

	igt_subtest("flink_close")
		test_flink_close();
}
