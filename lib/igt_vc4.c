/*
 * Copyright © 2016 Broadcom
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
 */

#include <assert.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "drmtest.h"
#include "igt_aux.h"
#include "igt_core.h"
#include "igt_vc4.h"
#include "ioctl_wrappers.h"
#include "intel_reg.h"
#include "intel_chipset.h"
#include "vc4_drm.h"
#include "vc4_packet.h"

#if NEW_CONTEXT_PARAM_NO_ERROR_CAPTURE_API
#define LOCAL_CONTEXT_PARAM_NO_ERROR_CAPTURE 0x4
#endif

/**
 * SECTION:igt_vc4
 * @short_description: VC4 support library
 * @title: VC4
 * @include: igt.h
 *
 * This library provides various auxiliary helper functions for writing VC4
 * tests.
 */

/**
 * igt_vc4_get_cleared_bo:
 * @size: size of the BO in bytes
 * @clearval: u32 value that the buffer should be completely cleared with
 *
 * This helper returns a new BO with the given size, which has just been
 * cleared using the render engine.
 */
uint32_t igt_vc4_get_cleared_bo(int fd, size_t size, uint32_t clearval)
{
	/* A single row will be a page. */
	uint32_t width = 1024;
	uint32_t height = size / (width * 4);
	uint32_t handle = igt_vc4_create_bo(fd, size);
	struct drm_vc4_submit_cl submit = {
		.color_write = {
			.hindex = 0,
			.bits = VC4_SET_FIELD(VC4_RENDER_CONFIG_FORMAT_RGBA8888,
					      VC4_RENDER_CONFIG_FORMAT),
		},

		.color_read = { .hindex = ~0 },
		.zs_read = { .hindex = ~0 },
		.zs_write = { .hindex = ~0 },
		.msaa_color_write = { .hindex = ~0 },
		.msaa_zs_write = { .hindex = ~0 },

		.bo_handles = (uint64_t)(uintptr_t)&handle,
		.bo_handle_count = 1,
		.width = width,
		.height = height,
		.max_x_tile = ALIGN(width, 64) / 64 - 1,
		.max_y_tile = ALIGN(height, 64) / 64 - 1,
		.clear_color = { clearval, clearval },
		.flags = VC4_SUBMIT_CL_USE_CLEAR_COLOR,
	};

	igt_assert_eq_u32(width * height * 4, size);

	do_ioctl(fd, DRM_IOCTL_VC4_SUBMIT_CL, &submit);

	return handle;
}

int
igt_vc4_create_bo(int fd, size_t size)
{
	struct drm_vc4_create_bo create = {
		.size = size,
	};

	do_ioctl(fd, DRM_IOCTL_VC4_CREATE_BO, &create);

	return create.handle;
}

void *
igt_vc4_mmap_bo(int fd, uint32_t handle, uint32_t size, unsigned prot)
{
	struct drm_vc4_mmap_bo mmap_bo = {
		.handle = handle,
	};
	void *ptr;

	do_ioctl(fd, DRM_IOCTL_VC4_MMAP_BO, &mmap_bo);

	ptr = mmap(0, size, prot, MAP_SHARED, fd, mmap_bo.offset);
	if (ptr == MAP_FAILED)
		return NULL;
	else
		return ptr;
}
