/*
 * Copyright © 2008-9 Intel Corporation
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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
#include "ioctl_wrappers.h"
#include "drmtest.h"

#define OBJECT_SIZE (128*1024) /* restricted to 1MiB alignment on i915 fences */

/* Before introduction of the LRU list for fences, allocation of a fence for a page
 * fault would use the first inactive fence (i.e. in preference one with no outstanding
 * GPU activity, or it would wait on the first to finish). Given the choice, it would simply
 * reuse the fence that had just been allocated for the previous page-fault - the worst choice
 * when copying between two buffers and thus constantly swapping fences.
 */

struct test {
	int fd;
	int tiling;
	int num_surfaces;
};

static void *
bo_create (int fd, int tiling)
{
	void *ptr;
	int handle;

	handle = gem_create(fd, OBJECT_SIZE);

	/* dirty cpu caches a bit ... */
	ptr = gem_mmap__cpu(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);
	igt_assert(ptr);
	memset(ptr, 0, OBJECT_SIZE);
	munmap(ptr, OBJECT_SIZE);

	gem_set_tiling(fd, handle, tiling, 1024);

	ptr = gem_mmap(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);
	igt_assert(ptr);

	/* XXX: mmap_gtt pulls the bo into the GTT read domain. */
	gem_sync(fd, handle);

	return ptr;
}

static void *
bo_copy (void *_arg)
{
	struct test *t = (struct test *)_arg;
	int fd = t->fd;
	int n;
	char *a, *b;

	a = bo_create (fd, t->tiling);
	b = bo_create (fd, t->tiling);

	for (n = 0; n < 1000; n++) {
		memcpy (a, b, OBJECT_SIZE);
		sched_yield ();
	}

	return NULL;
}

static void
_bo_write_verify(struct test *t)
{
	int fd = t->fd;
	int i, k;
	uint32_t **s;
	uint32_t v;
	unsigned int dwords = OBJECT_SIZE >> 2;
	const char *tile_str[] = { "none", "x", "y" };

	igt_assert(t->tiling >= 0 && t->tiling <= I915_TILING_Y);
	igt_assert(t->num_surfaces > 0);

	s = calloc(sizeof(*s), t->num_surfaces);
	igt_assert(s);

	for (k = 0; k < t->num_surfaces; k++)
		s[k] = bo_create(fd, t->tiling);

	for (k = 0; k < t->num_surfaces; k++) {
		volatile uint32_t *a = s[k];

		for (i = 0; i < dwords; i++) {
			a[i] = i;
			v = a[i];
			igt_assert_f(v == i,
				     "tiling %s: write failed at %d (%x)\n",
				     tile_str[t->tiling], i, v);
		}

		for (i = 0; i < dwords; i++) {
			v = a[i];
			igt_assert_f(v == i,
				     "tiling %s: verify failed at %d (%x)\n",
				     tile_str[t->tiling], i, v);
		}
	}

	for (k = 0; k < t->num_surfaces; k++)
		munmap(s[k], OBJECT_SIZE);

	free(s);
}

static void *
bo_write_verify(void *_arg)
{
	struct test *t = (struct test *)_arg;
	int i;

	for (i = 0; i < 10; i++)
		_bo_write_verify(t);

	return 0;
}

static int run_test(int threads_per_fence, void *f, int tiling,
		    int surfaces_per_thread)
{
	struct test t;
	pthread_t *threads;
	int n, num_fences, num_threads;

	t.fd = drm_open_any();
	t.tiling = tiling;
	t.num_surfaces = surfaces_per_thread;

	num_fences = gem_available_fences(t.fd);
	igt_assert(num_fences > 0);

	num_threads = threads_per_fence * num_fences;

	igt_info("%s: threads %d, fences %d, tiling %d, surfaces per thread %d\n",
		 f == bo_copy ? "copy" : "write-verify", num_threads,
		 num_fences, tiling, surfaces_per_thread);

	if (threads_per_fence) {
		threads = calloc(sizeof(*threads), num_threads);
		igt_assert(threads != NULL);

		for (n = 0; n < num_threads; n++)
			pthread_create (&threads[n], NULL, f, &t);

		for (n = 0; n < num_threads; n++)
			pthread_join (threads[n], NULL);
	} else {
		void *(*func)(void *) = f;
		igt_assert(func(&t) == (void *)0);
	}

	close(t.fd);

	return 0;
}

igt_main
{
	igt_skip_on_simulation();

	igt_subtest("bo-write-verify-none")
		igt_assert(run_test(0, bo_write_verify, I915_TILING_NONE, 80) == 0);

	igt_subtest("bo-write-verify-x")
		igt_assert(run_test(0, bo_write_verify, I915_TILING_X, 80) == 0);

	igt_subtest("bo-write-verify-y")
		igt_assert(run_test(0, bo_write_verify, I915_TILING_Y, 80) == 0);

	igt_subtest("bo-write-verify-threaded-none")
		igt_assert(run_test(5, bo_write_verify, I915_TILING_NONE, 2) == 0);

	igt_subtest("bo-write-verify-threaded-x") {
		igt_assert(run_test(2, bo_write_verify, I915_TILING_X, 2) == 0);
		igt_assert(run_test(5, bo_write_verify, I915_TILING_X, 2) == 0);
		igt_assert(run_test(10, bo_write_verify, I915_TILING_X, 2) == 0);
		igt_assert(run_test(20, bo_write_verify, I915_TILING_X, 2) == 0);
	}

	igt_subtest("bo-write-verify-threaded-y") {
		igt_assert(run_test(2, bo_write_verify, I915_TILING_Y, 2) == 0);
		igt_assert(run_test(5, bo_write_verify, I915_TILING_Y, 2) == 0);
		igt_assert(run_test(10, bo_write_verify, I915_TILING_Y, 2) == 0);
		igt_assert(run_test(20, bo_write_verify, I915_TILING_Y, 2) == 0);
	}

	igt_subtest("bo-copy")
		igt_assert(run_test(1, bo_copy, I915_TILING_X, 1) == 0);
}
