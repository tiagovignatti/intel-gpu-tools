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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_gpu_tools.h"

#define WIDTH 512
#define HEIGHT 512
static uint32_t linear[WIDTH * HEIGHT];
static uint32_t current_tiling_mode;

#define PAGE_SIZE 4096

static uint32_t
create_bo_and_fill(int fd)
{
	uint32_t handle;
	uint32_t *data;
	int i;

	handle = gem_create(fd, sizeof(linear));
	gem_set_tiling(fd, handle, current_tiling_mode, WIDTH * sizeof(uint32_t));

	/* Fill the BO with dwords starting at start_val */
	data = gem_mmap(fd, handle, sizeof(linear), PROT_READ | PROT_WRITE);
	for (i = 0; i < WIDTH*HEIGHT; i++)
		data[i] = i;
	munmap(data, sizeof(linear));

	return handle;
}

uint32_t *bo_handles;
int *idx_arr;

int
main(int argc, char **argv)
{
	int fd;
	uint32_t *data;
	int i, j;
	int count;
	current_tiling_mode = I915_TILING_X;

	fd = drm_open_any();
	/* need slightly more than total ram */
	count = intel_get_total_ram_mb() * 11 / 10;
	bo_handles = calloc(count, sizeof(uint32_t));
	assert(bo_handles);

	idx_arr = calloc(count, sizeof(int));
	assert(idx_arr);

	if (intel_get_total_swap_mb() == 0) {
		printf("no swap detected\n");
		return 77;
	}

	if (intel_get_total_ram_mb() / 4 > intel_get_total_swap_mb()) {
		printf("not enough swap detected\n");
		return 77;
	}

	for (i = 0; i < count; i++)
		bo_handles[i] = create_bo_and_fill(fd);

	for (i = 0; i < count; i++)
		idx_arr[i] = i;

	drmtest_permute_array(idx_arr, count,
			      drmtest_exchange_int);

	for (i = 0; i < count/2; i++) {
		/* Check the target bo's contents. */
		data = gem_mmap(fd, bo_handles[idx_arr[i]],
				sizeof(linear), PROT_READ | PROT_WRITE);
		for (j = 0; j < WIDTH*HEIGHT; j++)
			if (data[j] != j) {
				fprintf(stderr, "mismatch at %i: %i\n",
						j, data[j]);
				exit(1);
			}
		munmap(data, sizeof(linear));
	}

	close(fd);

	return 0;
}
