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
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

/** @file gem_tiled_pread_pwrite.c
 *
 * This is a test of pread's behavior on tiled objects with respect to the
 * reported swizzling value.
 *
 * The goal is to exercise the slow_bit17_copy path for reading on bit17
 * machines, but will also be useful for catching swizzling value bugs on
 * other systems.
 */

/*
 * Testcase: Exercise swizzle code for swapping
 *
 * The swizzle checks in the swapin path are at a different place than the ones
 * for pread/pwrite, so we need to check them separately.
 *
 * This test obviously needs swap present (and exits if none is detected).
 */

#include "igt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <pthread.h>

#include <drm.h>


IGT_TEST_DESCRIPTION("Exercise swizzle code for swapping.");

#define WIDTH 512
#define HEIGHT 512
#define LINEAR_DWORDS (4 * WIDTH * HEIGHT)
static uint32_t current_tiling_mode;

#define PAGE_SIZE 4096
#define AVAIL_RAM 512

static uint32_t
create_bo(int fd)
{
	uint32_t handle;
	uint32_t *data;

	handle = gem_create(fd, LINEAR_DWORDS);
	gem_set_tiling(fd, handle, current_tiling_mode, WIDTH * sizeof(uint32_t));

	data = __gem_mmap__gtt(fd, handle, LINEAR_DWORDS, PROT_READ | PROT_WRITE);
	if (data == NULL) {
		gem_close(fd, handle);
		return 0;
	}
	munmap(data, LINEAR_DWORDS);

	return handle;
}

static void
fill_bo(int fd, uint32_t handle)
{
	uint32_t *data;
	int i;

	data = gem_mmap__gtt(fd, handle, LINEAR_DWORDS,
			     PROT_READ | PROT_WRITE);

	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
	for (i = 0; i < WIDTH*HEIGHT; i++)
		data[i] = i;
	munmap(data, LINEAR_DWORDS);
}

static void
check_bo(int fd, uint32_t handle)
{
	uint32_t *data;
	int j;

	data = gem_mmap__gtt(fd, handle, LINEAR_DWORDS, PROT_READ);
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, 0);
	j = rand() % (WIDTH * HEIGHT);
	igt_assert_f(data[j] == j, "mismatch at %i: %i\n", j, data[j]);
	munmap(data, LINEAR_DWORDS);
}

uint32_t *bo_handles;

struct thread {
	pthread_t thread;
	int *idx_arr;
	int fd, count;
};

static void *thread_run(void *data)
{
	struct thread *t = data;
	int i;

	for (i = 0; i < t->count; i++)
		check_bo(t->fd, bo_handles[t->idx_arr[i]]);

	return NULL;
}

static void thread_init(struct thread *t, int fd, int count)
{
	int i;

	t->fd = fd;
	t->count = count;
	t->idx_arr = calloc(count, sizeof(int));
	igt_assert(t->idx_arr);

	for (i = 0; i < count; i++)
		t->idx_arr[i] = i;

	igt_permute_array(t->idx_arr, count, igt_exchange_int);
}

static void thread_fini(struct thread *t)
{
	free(t->idx_arr);
}

static void check_memory_layout(void)
{
	igt_skip_on_f(igt_debugfs_search("i915_swizzle_info", "L-shaped"),
		      "L-shaped memory configuration detected\n");

	igt_debug("normal memory configuration detected, continuing\n");
}

igt_main
{
	struct thread *threads;
	int fd, n, count, num_threads;

	igt_fixture {
		size_t lock_size;

		current_tiling_mode = I915_TILING_X;

		intel_purge_vm_caches();

		fd = drm_open_driver(DRIVER_INTEL);

		check_memory_layout();

		/* lock RAM, leaving only 512MB available */
		lock_size = max(0, intel_get_total_ram_mb() - AVAIL_RAM);
		igt_lock_mem(lock_size);

		/* need slightly more than available memory */
		count = min(intel_get_total_ram_mb(), AVAIL_RAM) * 1.25;
		bo_handles = calloc(count, sizeof(uint32_t));
		igt_assert(bo_handles);

		num_threads = gem_available_fences(fd);
		threads = calloc(num_threads, sizeof(struct thread));
		igt_assert(threads);

		igt_info("Using %d 1MiB objects (available RAM: %ld/%ld, swap: %ld)\n",
			 count,
			 (long)intel_get_avail_ram_mb(),
			 (long)intel_get_total_ram_mb(),
			 (long)intel_get_total_swap_mb());
		intel_require_memory(count, 1024*1024, CHECK_RAM | CHECK_SWAP);

		for (n = 0; n < count; n++) {
			bo_handles[n] = create_bo(fd);
			/* Not enough mmap address space possible. */
			igt_require(bo_handles[n]);
		}
	}

	igt_subtest("non-threaded") {
		for (n = 0; n < count; n++)
			fill_bo(fd, bo_handles[n]);

		thread_init(&threads[0], fd, count);
		thread_run(&threads[0]);
		thread_run(&threads[0]);
		thread_run(&threads[0]);
		thread_fini(&threads[0]);
	}

	/* Once more with threads */
	igt_subtest("threaded") {
		for (n = 0; n < count; n++)
			fill_bo(fd, bo_handles[n]);

		for (n = 0; n < num_threads; n++)
			thread_init(&threads[n], fd, count);

		thread_run(&threads[0]);
		for (n = 0; n < num_threads; n++)
			pthread_create(&threads[n].thread, NULL, thread_run, &threads[n]);
		for (n = 0; n < num_threads; n++)
			pthread_join(threads[n].thread, NULL);
		thread_run(&threads[0]);

		for (n = 0; n < num_threads; n++)
			thread_fini(&threads[n]);
	}

	close(fd);
}
