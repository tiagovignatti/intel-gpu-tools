/*
 * Copyright © 2007, 2011 Intel Corporation
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
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/mman.h>
#include "drmtest.h"
#include "i915_drm.h"
#include "intel_chipset.h"

/* This file contains a bunch of wrapper functions to directly use gem ioctls.
 * Mostly useful to write kernel tests. */

static int
is_intel(int fd)
{
	struct drm_i915_getparam gp;
	int devid;

	gp.param = I915_PARAM_CHIPSET_ID;
	gp.value = &devid;

	if (ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp)))
		return 0;

	return IS_INTEL(devid);
}

/** Open the first DRM device we can find, searching up to 16 device nodes */
int drm_open_any(void)
{
	char name[20];
	int i, fd;

	for (i = 0; i < 16; i++) {
		sprintf(name, "/dev/dri/card%d", i);
		fd = open(name, O_RDWR);
		if (fd == -1)
			continue;

		if (is_intel(fd))
			return fd;

		close(fd);
	}
	fprintf(stderr, "failed to open any drm device. retry as root?\n");
	abort();
}


/**
 * Open the first DRM device we can find where we end up being the master.
 */
int drm_open_any_master(void)
{
	char name[20];
	int i, fd;

	for (i = 0; i < 16; i++) {
		drm_client_t client;
		int ret;

		sprintf(name, "/dev/dri/card%d", i);
		fd = open(name, O_RDWR);
		if (fd == -1)
			continue;

		if (!is_intel(fd)) {
			close(fd);
			continue;
		}

		/* Check that we're the only opener and authed. */
		client.idx = 0;
		ret = ioctl(fd, DRM_IOCTL_GET_CLIENT, &client);
		assert (ret == 0);
		if (!client.auth) {
			close(fd);
			continue;
		}
		client.idx = 1;
		ret = ioctl(fd, DRM_IOCTL_GET_CLIENT, &client);
		if (ret != -1 || errno != EINVAL) {
			close(fd);
			continue;
		}
		return fd;
	}
	fprintf(stderr, "Couldn't find an un-controlled DRM device\n");
	abort();
}

void gem_set_tiling(int fd, uint32_t handle, int tiling, int stride)
{
	struct drm_i915_gem_set_tiling st;
	int ret;

	memset(&st, 0, sizeof(st));
	do {
		st.handle = handle;
		st.tiling_mode = tiling;
		st.stride = tiling ? stride : 0;

		ret = ioctl(fd, DRM_IOCTL_I915_GEM_SET_TILING, &st);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
	assert(ret == 0);
	assert(st.tiling_mode == tiling);
}

void gem_close(int fd, uint32_t handle)
{
	struct drm_gem_close close_bo;
	int ret;

	close_bo.handle = handle;
	ret = drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &close_bo);
	assert(ret == 0);
}

void gem_write(int fd, uint32_t handle, uint32_t offset, const void *buf, uint32_t size)
{
	struct drm_i915_gem_pwrite gem_pwrite;
	int ret;

	gem_pwrite.handle = handle;
	gem_pwrite.offset = offset;
	gem_pwrite.size = size;
	gem_pwrite.data_ptr = (uintptr_t)buf;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite);
	assert(ret == 0);
}

void gem_read(int fd, uint32_t handle, uint32_t offset, void *buf, uint32_t length)
{
	struct drm_i915_gem_pread gem_pread;
	int ret;

	gem_pread.handle = handle;
	gem_pread.offset = offset;
	gem_pread.size = length;
	gem_pread.data_ptr = (uintptr_t)buf;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_PREAD, &gem_pread);
	assert(ret == 0);
}

void gem_set_domain(int fd, uint32_t handle,
		    uint32_t read_domains, uint32_t write_domain)
{
	struct drm_i915_gem_set_domain set_domain;
	int ret;

	set_domain.handle = handle;
	set_domain.read_domains = read_domains;
	set_domain.write_domain = write_domain;

	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_SET_DOMAIN, &set_domain);
	assert(ret == 0);
}

void gem_sync(int fd, uint32_t handle)
{
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
}

uint32_t gem_create(int fd, int size)
{
	struct drm_i915_gem_create create;
	int ret;

	create.handle = 0;
	create.size = size;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);
	assert(ret == 0);
	assert(create.handle);

	return create.handle;
}

void *gem_mmap(int fd, uint32_t handle, int size, int prot)
{
	struct drm_i915_gem_mmap_gtt mmap_arg;
	void *ptr;

	mmap_arg.handle = handle;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_arg))
		return NULL;

	ptr = mmap(0, size, prot, MAP_SHARED, fd, mmap_arg.offset);
	if (ptr == MAP_FAILED)
		ptr = NULL;

	return ptr;
}

