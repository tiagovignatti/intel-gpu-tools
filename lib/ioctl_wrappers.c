/*
 * Copyright © 2007, 2011, 2013, 2014 Intel Corporation
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

#ifndef ANDROID
#define _GNU_SOURCE
#else
#include <libgen.h>
#endif
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <pciaccess.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <termios.h>

#include "drmtest.h"
#include "i915_drm.h"
#include "intel_chipset.h"
#include "intel_gpu_tools.h"
#include "igt_debugfs.h"
#include "../version.h"
#include "config.h"

#include "ioctl_wrappers.h"

int __gem_set_tiling(int fd, uint32_t handle, uint32_t tiling, uint32_t stride)
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
	if (ret != 0)
		return -errno;

	igt_assert(st.tiling_mode == tiling);
	return 0;
}

void gem_set_tiling(int fd, uint32_t handle, uint32_t tiling, uint32_t stride)
{
	igt_assert(__gem_set_tiling(fd, handle, tiling, stride) == 0);
}

int gem_get_num_rings(int fd)
{
	int num_rings = 1;	/* render ring is always available */

	if (gem_has_bsd(fd))
		num_rings++;
	else
		goto skip;

	if (gem_has_blt(fd))
		num_rings++;
	else
		goto skip;

	if (gem_has_vebox(fd))
		num_rings++;
	else
		goto skip;


skip:
	return num_rings;
}

struct local_drm_i915_gem_caching {
	uint32_t handle;
	uint32_t caching;
};

#define LOCAL_DRM_I915_GEM_SET_CACHEING    0x2f
#define LOCAL_DRM_I915_GEM_GET_CACHEING    0x30
#define LOCAL_DRM_IOCTL_I915_GEM_SET_CACHEING \
	DRM_IOW(DRM_COMMAND_BASE + LOCAL_DRM_I915_GEM_SET_CACHEING, struct local_drm_i915_gem_caching)
#define LOCAL_DRM_IOCTL_I915_GEM_GET_CACHEING \
	DRM_IOWR(DRM_COMMAND_BASE + LOCAL_DRM_I915_GEM_GET_CACHEING, struct local_drm_i915_gem_caching)

void gem_set_caching(int fd, uint32_t handle, int caching)
{
	struct local_drm_i915_gem_caching arg;
	int ret;

	arg.handle = handle;
	arg.caching = caching;
	ret = ioctl(fd, LOCAL_DRM_IOCTL_I915_GEM_SET_CACHEING, &arg);

	igt_assert(ret == 0 || (errno == ENOTTY || errno == EINVAL));
	igt_require(ret == 0);
}

uint32_t gem_get_caching(int fd, uint32_t handle)
{
	struct local_drm_i915_gem_caching arg;
	int ret;

	arg.handle = handle;
	arg.caching = 0;
	ret = ioctl(fd, LOCAL_DRM_IOCTL_I915_GEM_GET_CACHEING, &arg);
	igt_assert(ret == 0);

	return arg.caching;
}

uint32_t gem_open(int fd, uint32_t name)
{
	struct drm_gem_open open_struct;
	int ret;

	open_struct.name = name;
	ret = ioctl(fd, DRM_IOCTL_GEM_OPEN, &open_struct);
	igt_assert(ret == 0);
	igt_assert(open_struct.handle != 0);

	return open_struct.handle;
}

uint32_t gem_flink(int fd, uint32_t handle)
{
	struct drm_gem_flink flink;
	int ret;

	flink.handle = handle;
	ret = ioctl(fd, DRM_IOCTL_GEM_FLINK, &flink);
	igt_assert(ret == 0);

	return flink.name;
}

void gem_close(int fd, uint32_t handle)
{
	struct drm_gem_close close_bo;

	close_bo.handle = handle;
	do_ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_bo);
}

void gem_write(int fd, uint32_t handle, uint32_t offset, const void *buf, uint32_t size)
{
	struct drm_i915_gem_pwrite gem_pwrite;

	gem_pwrite.handle = handle;
	gem_pwrite.offset = offset;
	gem_pwrite.size = size;
	gem_pwrite.data_ptr = (uintptr_t)buf;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite);
}

void gem_read(int fd, uint32_t handle, uint32_t offset, void *buf, uint32_t length)
{
	struct drm_i915_gem_pread gem_pread;

	gem_pread.handle = handle;
	gem_pread.offset = offset;
	gem_pread.size = length;
	gem_pread.data_ptr = (uintptr_t)buf;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_PREAD, &gem_pread);
}

void gem_set_domain(int fd, uint32_t handle,
		    uint32_t read_domains, uint32_t write_domain)
{
	struct drm_i915_gem_set_domain set_domain;

	set_domain.handle = handle;
	set_domain.read_domains = read_domains;
	set_domain.write_domain = write_domain;

	do_ioctl(fd, DRM_IOCTL_I915_GEM_SET_DOMAIN, &set_domain);
}

void gem_sync(int fd, uint32_t handle)
{
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
}

uint32_t __gem_create(int fd, int size)
{
	struct drm_i915_gem_create create;
	int ret;

	create.handle = 0;
	create.size = size;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);

	if (ret < 0)
		return 0;
	else
		return create.handle;
}

uint32_t gem_create(int fd, int size)
{
	struct drm_i915_gem_create create;

	create.handle = 0;
	create.size = size;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);
	igt_assert(create.handle);

	return create.handle;
}

void gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *execbuf)
{
	int ret;

	ret = drmIoctl(fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER2,
		       execbuf);
	igt_assert(ret == 0);
}

void *gem_mmap__gtt(int fd, uint32_t handle, int size, int prot)
{
	struct drm_i915_gem_mmap_gtt mmap_arg;
	void *ptr;

	mmap_arg.handle = handle;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_arg))
		return NULL;

	ptr = mmap64(0, size, prot, MAP_SHARED, fd, mmap_arg.offset);
	if (ptr == MAP_FAILED)
		ptr = NULL;

	return ptr;
}

void *gem_mmap__cpu(int fd, uint32_t handle, int size, int prot)
{
	struct drm_i915_gem_mmap mmap_arg;

	mmap_arg.handle = handle;
	mmap_arg.offset = 0;
	mmap_arg.size = size;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_MMAP, &mmap_arg))
		return NULL;

	return (void *)(uintptr_t)mmap_arg.addr_ptr;
}

int gem_madvise(int fd, uint32_t handle, int state)
{
	struct drm_i915_gem_madvise madv;

	madv.handle = handle;
	madv.madv = state;
	madv.retained = 1;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_MADVISE, &madv);

	return madv.retained;
}

uint32_t gem_context_create(int fd)
{
	struct drm_i915_gem_context_create create;
	int ret;

	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, &create);
	igt_require(ret == 0 || (errno != ENODEV && errno != EINVAL));
	igt_assert(ret == 0);

	return create.ctx_id;
}

void gem_sw_finish(int fd, uint32_t handle)
{
	struct drm_i915_gem_sw_finish finish;

	finish.handle = handle;

	do_ioctl(fd, DRM_IOCTL_I915_GEM_SW_FINISH, &finish);
}

bool gem_bo_busy(int fd, uint32_t handle)
{
	struct drm_i915_gem_busy busy;

	busy.handle = handle;

	do_ioctl(fd, DRM_IOCTL_I915_GEM_BUSY, &busy);

	return !!busy.busy;
}


/* feature test helpers */
bool gem_uses_aliasing_ppgtt(int fd)
{
	struct drm_i915_getparam gp;
	int val;

	gp.param = 18; /* HAS_ALIASING_PPGTT */
	gp.value = &val;

	if (ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp)))
		return 0;

	return val;
}

int gem_available_fences(int fd)
{
	struct drm_i915_getparam gp;
	int val;

	gp.param = I915_PARAM_NUM_FENCES_AVAIL;
	gp.value = &val;

	if (ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp)))
		return 0;

	return val;
}

bool gem_has_enable_ring(int fd,int param)
{
	drm_i915_getparam_t gp;
	int ret, tmp;
	memset(&gp, 0, sizeof(gp));

	gp.value = &tmp;
	gp.param = param;

	ret = drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);

	if ((ret == 0) && (*gp.value > 0))
		return true;
	else
		return false;
}

bool gem_has_bsd(int fd)
{

	return gem_has_enable_ring(fd,I915_PARAM_HAS_BSD);
}

bool gem_has_blt(int fd)
{

	return gem_has_enable_ring(fd,I915_PARAM_HAS_BLT);
}

#define LOCAL_I915_PARAM_HAS_VEBOX 22
bool gem_has_vebox(int fd)
{

	return gem_has_enable_ring(fd,LOCAL_I915_PARAM_HAS_VEBOX);
}

uint64_t gem_available_aperture_size(int fd)
{
	struct drm_i915_gem_get_aperture aperture;

	aperture.aper_size = 256*1024*1024;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_GET_APERTURE, &aperture);
	return aperture.aper_available_size;
}

uint64_t gem_aperture_size(int fd)
{
	struct drm_i915_gem_get_aperture aperture;

	aperture.aper_size = 256*1024*1024;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_GET_APERTURE, &aperture);
	return aperture.aper_size;
}

uint64_t gem_mappable_aperture_size(void)
{
	struct pci_device *pci_dev;
	int bar;
	pci_dev = intel_get_pci_device();

	if (intel_gen(pci_dev->device_id) < 3)
		bar = 0;
	else
		bar = 2;

	return pci_dev->regions[bar].size;
}

void gem_require_caching(int fd)
{
	struct local_drm_i915_gem_caching arg;
	int ret;

	arg.handle = gem_create(fd, 4096);
	igt_assert(arg.handle != 0);

	arg.caching = 0;
	ret = ioctl(fd, LOCAL_DRM_IOCTL_I915_GEM_SET_CACHEING, &arg);
	gem_close(fd, arg.handle);

	igt_require(ret == 0);
}

/* prime */
int prime_handle_to_fd(int fd, uint32_t handle)
{
	struct drm_prime_handle args;

	args.handle = handle;
	args.flags = DRM_CLOEXEC;
	args.fd = -1;

	do_ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);

	return args.fd;
}

uint32_t prime_fd_to_handle(int fd, int dma_buf_fd)
{
	struct drm_prime_handle args;

	args.fd = dma_buf_fd;
	args.flags = 0;
	args.handle = 0;

	do_ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &args);

	return args.handle;
}

off_t prime_get_size(int dma_buf_fd)
{
	off_t ret;
	ret = lseek(dma_buf_fd, 0, SEEK_END);
	igt_assert(ret >= 0 || errno == ESPIPE);
	igt_require(ret >= 0);

	return ret;
}

