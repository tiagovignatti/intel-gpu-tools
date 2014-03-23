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
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <drm.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_io.h"
#include "igt_aux.h"

#define WIDTH 512
#define HEIGHT 512
#define LINEAR_DWORDS (4 * WIDTH * HEIGHT)
static uint32_t current_tiling_mode;

#define PAGE_SIZE 4096

static uint32_t
create_bo_and_fill(int fd)
{
	uint32_t handle;
	uint32_t *data;
	int i;

	handle = gem_create(fd, LINEAR_DWORDS);
	gem_set_tiling(fd, handle, current_tiling_mode, WIDTH * sizeof(uint32_t));

	/* Fill the BO with dwords starting at start_val */
	data = gem_mmap(fd, handle, LINEAR_DWORDS, PROT_READ | PROT_WRITE);
	if (data == NULL && errno == ENOSPC)
		return 0;

	for (i = 0; i < WIDTH*HEIGHT; i++)
		data[i] = i;
	munmap(data, LINEAR_DWORDS);

	return handle;
}

uint32_t *bo_handles;
int *idx_arr;

igt_simple_main
{
	int fd;
	uint32_t *data;
	int i, j;
	int count;
	current_tiling_mode = I915_TILING_X;

	igt_skip_on_simulation();

	fd = drm_open_any();
	/* need slightly more than total ram */
	count = intel_get_total_ram_mb() * 11 / 10;
	bo_handles = calloc(count, sizeof(uint32_t));
	igt_assert(bo_handles);

	idx_arr = calloc(count, sizeof(int));
	igt_assert(idx_arr);

	igt_require(intel_get_total_ram_mb() / 4 < intel_get_total_swap_mb());

	for (i = 0; i < count; i++) {
		bo_handles[i] = create_bo_and_fill(fd);
		/* Not enough mmap address space possible. */
		igt_require(bo_handles[i]);
	}

	for (i = 0; i < count; i++)
		idx_arr[i] = i;

	igt_permute_array(idx_arr, count,
			      igt_exchange_int);

	for (i = 0; i < count/2; i++) {
		/* Check the target bo's contents. */
		data = gem_mmap(fd, bo_handles[idx_arr[i]],
				LINEAR_DWORDS, PROT_READ | PROT_WRITE);
		for (j = 0; j < WIDTH*HEIGHT; j++)
			igt_assert_f(data[j] == j,
				     "mismatch at %i: %i\n",
				     j, data[j]);
		munmap(data, LINEAR_DWORDS);
	}

	close(fd);
}
