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
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>

#include "drmtest.h"
#include "i915_drm.h"
#include "intel_bufmgr.h"

/* Testcase: check for flink/open vs. gem close races
 *
 * The gem flink open ioctl had a little race with gem close which could result
 * in the flink name and corresponding reference getting leaked.
 */

/* We want lockless and I'm to lazy to dig out an atomic libarary. On x86 this
 * works, too. */
volatile int pls_die = 0;
int fd;

static void *thread_fn(void *p)
{
	struct drm_gem_open open_struct;
	int ret;

	while (!pls_die) {
		memset(&open_struct, 0, sizeof(open_struct));

		open_struct.name = 1;
		ret = ioctl(fd, DRM_IOCTL_GEM_OPEN, &open_struct);
		if (ret == 0)
			gem_close(fd, open_struct.handle);
	}

	return (void *)0;
}

static int get_object_count(const char *path)
{
	FILE *file;
	int ret, scanned;
	file = fopen(path, "r");

	scanned = fscanf(file, "%i objects,", &ret);
	assert(scanned == 1);

	return ret;
}

int main(int argc, char **argv)
{
	int num_threads;
	pthread_t *threads;
	int r, i;
	void *status;
	int device = drm_get_card(0);
	int old_object_count, new_object_count;
	char *path;

	drmtest_skip_on_simulation();

	r = asprintf(&path, "/sys/kernel/debug/dri/%d/i915_gem_objects", device);
	assert(r != -1);

	old_object_count = get_object_count(path);

	num_threads = sysconf(_SC_NPROCESSORS_ONLN) - 1;
	if (!num_threads)
		num_threads = 1;

	threads = calloc(num_threads, sizeof(pthread_t));

	fd = drm_open_any();
	assert(fd >= 0);

	for (i = 0; i < num_threads; i++) {
		r = pthread_create(&threads[i], NULL, thread_fn, NULL);
		assert(r == 0);
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
		assert(status == 0);
	}

	fd = drm_open_any();
	assert(fd >= 0);

	close(fd);

	new_object_count = get_object_count(path);

	printf("leaked %d objects\n", new_object_count - old_object_count);

	return new_object_count - old_object_count > 0 ? 1 : 0;
}
