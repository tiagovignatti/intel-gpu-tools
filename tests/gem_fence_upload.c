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
#include <sys/time.h>
#include <pthread.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "ioctl_wrappers.h"

#define OBJECT_SIZE (1024*1024) /* restricted to 1MiB alignment on i915 fences */

static double elapsed(const struct timeval *start,
		      const struct timeval *end)
{
	return (end->tv_sec - start->tv_sec) + 1e-6*(end->tv_usec - start->tv_usec);
}

static void performance(void)
{
	int n, loop, count;
	int fd, num_fences;
	double linear[2], tiled[2];

	fd = drm_open_any();

	num_fences = gem_available_fences(fd);
	igt_require(num_fences > 0);

	for (count = 2; count < 4*num_fences; count *= 2) {
		struct timeval start, end;
		uint32_t handle[count];
		void *ptr[count];

		for (n = 0; n < count; n++) {
			handle[n] = gem_create(fd, OBJECT_SIZE);
			ptr[n] = gem_mmap(fd, handle[n], OBJECT_SIZE, PROT_READ | PROT_WRITE);
			igt_assert(ptr[n]);
		}

		gettimeofday(&start, NULL);
		for (loop = 0; loop < 1024; loop++) {
			for (n = 0; n < count; n++)
				memset(ptr[n], 0, OBJECT_SIZE);
		}
		gettimeofday(&end, NULL);

		linear[count != 2] = count * loop / elapsed(&start, &end);
		igt_info("Upload rate for %d linear surfaces:	%7.3fMiB/s\n", count, linear[count != 2]);

		for (n = 0; n < count; n++)
			gem_set_tiling(fd, handle[n], I915_TILING_X, 1024);

		gettimeofday(&start, NULL);
		for (loop = 0; loop < 1024; loop++) {
			for (n = 0; n < count; n++)
				memset(ptr[n], 0, OBJECT_SIZE);
		}
		gettimeofday(&end, NULL);

		tiled[count != 2] = count * loop / elapsed(&start, &end);
		igt_info("Upload rate for %d tiled surfaces:	%7.3fMiB/s\n", count, tiled[count != 2]);

		for (n = 0; n < count; n++) {
			munmap(ptr[n], OBJECT_SIZE);
			gem_close(fd, handle[n]);
		}

	}

	errno = 0;
	igt_assert(linear[1] > 0.75 * linear[0]);
	igt_assert(tiled[1] > 0.75 * tiled[0]);
}

struct thread_performance {
	pthread_t thread;
	int id, count, direction, loops;
	void **ptr;
};

static void *read_thread_performance(void *closure)
{
	struct thread_performance *t = closure;
	uint32_t x = 0;
	int n, m;

	for (n = 0; n < t->loops; n++) {
		uint32_t *src = t->ptr[rand() % t->count];
		src += (rand() % 256) * 4096 / 4;
		for (m = 0; m < 4096/4; m++)
			x += src[m];
	}

	return (void *)(uintptr_t)x;
}

static void *write_thread_performance(void *closure)
{
	struct thread_performance *t = closure;
	int n;

	for (n = 0; n < t->loops; n++) {
		uint32_t *dst = t->ptr[rand() % t->count];
		dst += (rand() % 256) * 4096 / 4;
		memset(dst, 0, 4096);
	}

	return NULL;
}

#define READ (1<<0)
#define WRITE (1<<1)
static const char *direction_string(unsigned mask)
{
	switch (mask) {
	case READ: return "Download";
	case WRITE: return "Upload";
	case READ | WRITE: return "Combined";
	default: return "Unknown";
	}
}
static void thread_performance(unsigned mask)
{
	const int loops = 4096;
	int n, count;
	int fd, num_fences;
	double linear[2], tiled[2];

	fd = drm_open_any();

	num_fences = gem_available_fences(fd);
	igt_require(num_fences > 0);

	for (count = 2; count < 4*num_fences; count *= 2) {
		const int nthreads = (mask & READ ? count : 0) + (mask & WRITE ? count : 0);
		struct timeval start, end;
		struct thread_performance readers[count];
		struct thread_performance writers[count];
		uint32_t handle[count];
		void *ptr[count];

		for (n = 0; n < count; n++) {
			handle[n] = gem_create(fd, OBJECT_SIZE);
			ptr[n] = gem_mmap(fd, handle[n], OBJECT_SIZE, PROT_READ | PROT_WRITE);
			igt_assert(ptr[n]);

			if (mask & READ) {
				readers[n].id = n;
				readers[n].direction = READ;
				readers[n].ptr = ptr;
				readers[n].count = count;
				readers[n].loops = loops;
			}

			if (mask & WRITE) {
				writers[n].id = count - n - 1;
				writers[n].direction = WRITE;
				writers[n].ptr = ptr;
				writers[n].count = count;
				writers[n].loops = loops;
			}
		}

		gettimeofday(&start, NULL);
		for (n = 0; n < count; n++) {
			if (mask & READ)
				pthread_create(&readers[n].thread, NULL, read_thread_performance, &readers[n]);
			if (mask & WRITE)
				pthread_create(&writers[n].thread, NULL, write_thread_performance, &writers[n]);
		}
		for (n = 0; n < count; n++) {
			if (mask & READ)
				pthread_join(readers[n].thread, NULL);
			if (mask & WRITE)
				pthread_join(writers[n].thread, NULL);
		}
		gettimeofday(&end, NULL);

		linear[count != 2] = nthreads * loops / elapsed(&start, &end) / (OBJECT_SIZE / 4096);
		igt_info("%s rate for %d linear surfaces, %d threads:	%7.3fMiB/s\n", direction_string(mask), count, nthreads, linear[count != 2]);

		for (n = 0; n < count; n++)
			gem_set_tiling(fd, handle[n], I915_TILING_X, 1024);

		gettimeofday(&start, NULL);
		for (n = 0; n < count; n++) {
			if (mask & READ)
				pthread_create(&readers[n].thread, NULL, read_thread_performance, &readers[n]);
			if (mask & WRITE)
				pthread_create(&writers[n].thread, NULL, write_thread_performance, &writers[n]);
		}
		for (n = 0; n < count; n++) {
			if (mask & READ)
				pthread_join(readers[n].thread, NULL);
			if (mask & WRITE)
				pthread_join(writers[n].thread, NULL);
		}
		gettimeofday(&end, NULL);

		tiled[count != 2] = nthreads * loops / elapsed(&start, &end) / (OBJECT_SIZE / 4096);
		igt_info("%s rate for %d tiled surfaces, %d threads:	%7.3fMiB/s\n", direction_string(mask), count, nthreads, tiled[count != 2]);

		for (n = 0; n < count; n++) {
			munmap(ptr[n], OBJECT_SIZE);
			gem_close(fd, handle[n]);
		}
	}

	errno = 0;
	igt_assert(linear[1] > 0.75 * linear[0]);
	igt_assert(tiled[1] > 0.75 * tiled[0]);
}

struct thread_contention {
	pthread_t thread;
	uint32_t handle;
	int loops, fd;
};
static void *no_contention(void *closure)
{
	struct thread_contention *t = closure;
	int n;

	for (n = 0; n < t->loops; n++) {
		uint32_t *ptr = gem_mmap(t->fd, t->handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);
		memset(ptr + (rand() % 256) * 4096 / 4, 0, 4096);
		munmap(ptr, OBJECT_SIZE);
	}

	return NULL;
}

static void thread_contention(void)
{
	const int loops = 4096;
	int n, count;
	int fd, num_fences;
	double linear[2], tiled[2];

	fd = drm_open_any();

	num_fences = gem_available_fences(fd);
	igt_require(num_fences > 0);

	for (count = 1; count < 4*num_fences; count *= 2) {
		struct timeval start, end;
		struct thread_contention threads[count];

		for (n = 0; n < count; n++) {
			threads[n].handle = gem_create(fd, OBJECT_SIZE);
			threads[n].loops = loops;
			threads[n].fd = fd;
		}

		gettimeofday(&start, NULL);
		for (n = 0; n < count; n++)
			pthread_create(&threads[n].thread, NULL, no_contention, &threads[n]);
		for (n = 0; n < count; n++)
			pthread_join(threads[n].thread, NULL);
		gettimeofday(&end, NULL);

		linear[count != 2] = count * loops / elapsed(&start, &end) / (OBJECT_SIZE / 4096);
		igt_info("Contended upload rate for %d threads:	%7.3fMiB/s\n", count, linear[count != 2]);

		for (n = 0; n < count; n++)
			gem_set_tiling(fd, threads[n].handle, I915_TILING_X, 1024);

		gettimeofday(&start, NULL);
		for (n = 0; n < count; n++)
			pthread_create(&threads[n].thread, NULL, no_contention, &threads[n]);
		for (n = 0; n < count; n++)
			pthread_join(threads[n].thread, NULL);
		gettimeofday(&end, NULL);

		tiled[count != 2] = count * loops / elapsed(&start, &end) / (OBJECT_SIZE / 4096);
		igt_info("Contended upload rate for %d threads:	%7.3fMiB/s\n", count, tiled[count != 2]);

		for (n = 0; n < count; n++) {
			gem_close(fd, threads[n].handle);
		}
	}

	errno = 0;
	igt_assert(linear[1] > 0.75 * linear[0]);
	igt_assert(tiled[1] > 0.75 * tiled[0]);
}

igt_main
{
	igt_skip_on_simulation();

	igt_subtest("performance")
		performance();
	igt_subtest("thread-contention")
		thread_contention();
	igt_subtest("thread-performance-read")
		thread_performance(READ);
	igt_subtest("thread-performance-write")
		thread_performance(WRITE);
	igt_subtest("thread-performance-both")
		thread_performance(READ | WRITE);
}
