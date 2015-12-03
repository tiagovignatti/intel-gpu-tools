/*
 * Copyright © 2015 Intel Corporation
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
 *    Ankitprasad Sharma <ankitprasad.r.sharma at intel.com>
 *
 */

/** @file gem_create_stolen.c
 *
 * This is a test for the extended gem_create ioctl, that includes allocation
 * of object from stolen memory.
 *
 * The goal is to simply ensure the basics work, and invalid input combinations
 * are rejected.
 */

#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <getopt.h>

#include <drm.h>

#include "ioctl_wrappers.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"
#include "intel_chipset.h"
#include "igt_aux.h"
#include "drmtest.h"
#include "drm.h"
#include "i915_drm.h"

IGT_TEST_DESCRIPTION("This test verifies the exetended gem_create ioctl,"
		     " that includes allocation of obj from stolen region");
#define CLEAR(s) memset(&s, 0, sizeof(s))
#define SIZE 1024*1024
#define DWORD_SIZE 4
#define DATA 0xdead
#define LARGE_SIZE 0xffffffff
#define MAX_OBJECTS 100

static drm_intel_bufmgr *bufmgr;
static struct intel_batchbuffer *batch;

static void verify_copy_op(drm_intel_bo *src, drm_intel_bo *dest)
{
	uint32_t *virt, i, ret;
	/* Fill the src BO with dwords */
	ret = drm_intel_gem_bo_map_gtt(src);
	igt_assert(!ret);

	virt = src->virtual;
	for (i = 0; i < SIZE/DWORD_SIZE; i++)
		virt[i] = i;

	intel_copy_bo(batch, dest, src, SIZE);

	ret = drm_intel_gem_bo_map_gtt(dest);
	igt_assert(!ret);

	virt = dest->virtual;
	/* verify */
	for (i = 0; i < SIZE/DWORD_SIZE; i++)
		igt_assert_eq(virt[i], i);

	drm_intel_bo_unmap(src);
	drm_intel_bo_unmap(dest);
}

static void stolen_pwrite(int fd)
{
	drm_intel_bo *bo;
	uint32_t buf[SIZE/DWORD_SIZE];
	uint32_t handle = 0;
	uint32_t *virt;
	int i, ret = 0;

	for (i = 0; i < SIZE/DWORD_SIZE; i++)
		buf[i] = DATA;

	gem_require_stolen_support(fd);

	handle = gem_create_stolen(fd, SIZE);

	gem_write(fd, handle, 0, buf, SIZE);
	bo = gem_handle_to_libdrm_bo(bufmgr, fd, "bo", handle);

	ret = drm_intel_gem_bo_map_gtt(bo);
	igt_assert(!ret);

	virt = bo->virtual;

	for (i = 0; i < SIZE/DWORD_SIZE; i++)
		igt_assert_eq(virt[i], DATA);

	drm_intel_bo_unmap(bo);
	drm_intel_bo_unreference(bo);
	gem_close(fd, handle);
}

static void stolen_pread(int fd)
{
	drm_intel_bo *bo;
	uint32_t buf[SIZE/DWORD_SIZE];
	uint32_t handle = 0;
	uint32_t *virt;
	int i, ret = 0;

	CLEAR(buf);

	gem_require_stolen_support(fd);

	handle = gem_create_stolen(fd, SIZE);

	bo = gem_handle_to_libdrm_bo(bufmgr, fd, "bo", handle);

	ret = drm_intel_gem_bo_map_gtt(bo);
	igt_assert(!ret);

	virt = bo->virtual;

	for (i = 0; i < SIZE/DWORD_SIZE; i++)
		virt[i] = DATA;

	drm_intel_bo_unmap(bo);
	drm_intel_bo_unreference(bo);

	gem_read(fd, handle, 0, buf, SIZE);

	for (i = 0; i < SIZE/DWORD_SIZE; i++)
		igt_assert_eq(buf[i], DATA);

	gem_close(fd, handle);
}

static void copy_test(int fd)
{
	drm_intel_bo *src, *dest;
	uint32_t src_handle = 0, dest_handle = 0;

	gem_require_stolen_support(fd);

	src_handle = gem_create_stolen(fd, SIZE);
	dest_handle = gem_create_stolen(fd, SIZE);

	src = gem_handle_to_libdrm_bo(bufmgr, fd, "src_bo", src_handle);
	dest = gem_handle_to_libdrm_bo(bufmgr, fd, "dst_bo", dest_handle);

	igt_assert(src != NULL);
	igt_assert(dest != NULL);

	verify_copy_op(src, dest);

	drm_intel_bo_unreference(src);
	drm_intel_bo_unreference(dest);
	gem_close(fd, src_handle);
	gem_close(fd, dest_handle);
}

static void verify_object_clear(int fd)
{
	drm_intel_bo *bo;
	uint32_t handle = 0;
	uint32_t *virt;
	int i, ret;

	gem_require_stolen_support(fd);

	handle = gem_create_stolen(fd, SIZE);

	bo = gem_handle_to_libdrm_bo(bufmgr, fd, "verify_bo", handle);
	igt_assert(bo != NULL);

	ret = drm_intel_gem_bo_map_gtt(bo);
	igt_assert(!ret);

	/* Verify if the BO is zeroed */
	virt = bo->virtual;
	for (i = 0; i < SIZE / DWORD_SIZE; i++)
		igt_assert(!virt[i]);

	drm_intel_bo_unmap(bo);
	drm_intel_bo_unreference(bo);
	gem_close(fd, handle);
}

static void stolen_large_obj_alloc(int fd)
{
	uint32_t handle = 0;

	gem_require_stolen_support(fd);
	handle = __gem_create_stolen(fd, (unsigned long long) LARGE_SIZE + 4096);
	igt_assert(!handle);
}

static void stolen_fill_purge_test(int fd)
{
	drm_intel_bo *bo;
	int obj_count = 0, i = 0;
	int _ret = 0, j = 0;
	uint32_t handle[MAX_OBJECTS];
	uint32_t new_handle;
	uint32_t *virt;
	int retained;

	gem_require_stolen_support(fd);

	/* Exhaust Stolen space */
	do {
		handle[i] = __gem_create_stolen(fd, SIZE);
		if (handle[i] != 0) {
			bo = gem_handle_to_libdrm_bo(bufmgr, fd,
						     "verify_bo", handle[i]);
			igt_assert(bo != NULL);

			_ret = drm_intel_gem_bo_map_gtt(bo);
			igt_assert(!_ret);

			virt = bo->virtual;
			for (j = 0; j < SIZE/DWORD_SIZE; j++)
				virt[j] = DATA;

			drm_intel_bo_unmap(bo);
			drm_intel_bo_unreference(bo);

			obj_count++;
		}

		i++;
	} while (handle[i-1] && i < MAX_OBJECTS);

	igt_assert(obj_count > 0);

	/* Mark all stolen objects purgeable */
	for (i = 0; i < obj_count; i++)
		retained = gem_madvise(fd, handle[i], I915_MADV_DONTNEED);

	/* Try to allocate one more object */
	new_handle = gem_create_stolen(fd, SIZE);

	/* Check if the retained object's memory contents are intact */
	for (i = 0; i < obj_count; i++) {
		retained = gem_madvise(fd, handle[i], I915_MADV_WILLNEED);
		if (retained) {
			bo = gem_handle_to_libdrm_bo(bufmgr, fd,
						     "verify_bo", handle[i]);
			igt_assert(bo != NULL);

			_ret = drm_intel_gem_bo_map_gtt(bo);
			igt_assert(!_ret);

			virt = bo->virtual;
			for (j = 0; j < SIZE/DWORD_SIZE; j++)
				igt_assert_eq(virt[j], DATA);

			drm_intel_bo_unmap(bo);
			drm_intel_bo_unreference(bo);
		}
	}

	gem_close(fd, new_handle);
	for (i = 0; i < obj_count; i++)
		gem_close(fd, handle[i]);
}

static void
stolen_no_mmap(int fd)
{
	void *addr;
	uint32_t handle = 0;

	gem_require_stolen_support(fd);

	handle = gem_create_stolen(fd, SIZE);

	addr = gem_mmap__cpu(fd, handle, 0, SIZE, PROT_READ | PROT_WRITE);
	igt_assert(addr == NULL);

	gem_close(fd, handle);
}

igt_main
{
	int fd;
	uint32_t devid;

	igt_skip_on_simulation();

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);
		devid = intel_get_drm_devid(fd);

		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		batch = intel_batchbuffer_alloc(bufmgr, devid);
	}

	igt_subtest("stolen-clear")
		verify_object_clear(fd);

	/*
	 * stolen mem special cases - checking for non cpu mappable
	 */
	igt_subtest("stolen-no-mmap")
		stolen_no_mmap(fd);

	/* checking for pread/pwrite interfaces */
	igt_subtest("stolen-pwrite")
		stolen_pwrite(fd);

	igt_subtest("stolen-pread")
		stolen_pread(fd);

	/* Functional Test - blt copy */
	igt_subtest("stolen-copy")
		copy_test(fd);

	igt_subtest("large-object-alloc")
		stolen_large_obj_alloc(fd);

	/* Filling stolen completely and marking all the objects
	 * purgeable. Then trying to add one more object, to verify
	 * the purging logic.
	 * Again marking all objects WILLNEED and verifying the
	 * contents of the retained objects.
	 */
	igt_subtest("stolen-fill-purge")
		stolen_fill_purge_test(fd);

	igt_fixture {
		intel_batchbuffer_free(batch);
		drm_intel_bufmgr_destroy(bufmgr);
	}
}
