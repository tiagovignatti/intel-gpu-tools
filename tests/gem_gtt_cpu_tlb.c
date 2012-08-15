/*
 * Copyright © 2009 Intel Corporation
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

/** @file gem_gtt_cpu_tlb.c
 *
 * This test checks whether gtt tlbs for cpu access are correctly invalidated.
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

#define OBJ_SIZE (1024*1024)

#define PAGE_SIZE 4096

static uint32_t
create_bo(int fd)
{
	uint32_t handle;
	uint32_t *data;
	int i;

	handle = gem_create(fd, OBJ_SIZE);

	/* Fill the BO with dwords starting at start_val */
	data = gem_mmap(fd, handle, OBJ_SIZE, PROT_READ | PROT_WRITE);
	for (i = 0; i < OBJ_SIZE/4; i++)
		data[i] = i;
	munmap(data, OBJ_SIZE);

	return handle;
}

int
main(int argc, char **argv)
{
	int fd;
	int i;
	uint32_t handle;

	uint32_t *ptr;

	fd = drm_open_any();

	handle = gem_create(fd, OBJ_SIZE);

	/* touch one page */
	ptr = gem_mmap(fd, handle, OBJ_SIZE, PROT_READ | PROT_WRITE);
	*ptr = 0xdeadbeef;
	munmap(ptr, OBJ_SIZE);

	gem_close(fd, handle);

	/* stirr up the page allocator a bit. */
	ptr = malloc(OBJ_SIZE);
	assert(ptr);
	memset(ptr, 0x1, OBJ_SIZE);

	handle = create_bo(fd);

	/* Read a bunch of random subsets of the data and check that they come
	 * out right.
	 */
	gem_read(fd, handle, 0, ptr, OBJ_SIZE);
	for (i = 0; i < OBJ_SIZE/4; i++)
		assert(ptr[i] == i);

	close(fd);

	return 0;
}
