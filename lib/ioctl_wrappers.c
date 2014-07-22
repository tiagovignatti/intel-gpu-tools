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
#include <errno.h>

#include "drmtest.h"
#include "i915_drm.h"
#include "intel_chipset.h"
#include "intel_io.h"
#include "igt_debugfs.h"
#include "config.h"

#include "ioctl_wrappers.h"

/**
 * SECTION:ioctl_wrappers
 * @short_description: ioctl wrappers and related functions
 * @title: ioctl wrappers
 * @include: ioctl_wrappers.h
 *
 * This helper library contains simple functions to wrap the raw drm/i915 kernel
 * ioctls. The normal versions never pass any error codes to the caller and use
 * igt_assert() to check for error conditions instead. For some ioctls raw
 * wrappers which do pass on error codes are available. These raw wrappers have
 * a __ prefix.
 *
 * For wrappers which check for feature bits there can also be two versions: The
 * normal one simply returns a boolean to the caller. But when skipping the
 * testcase entirely is the right action then it's better to use igt_skip()
 * directly in the wrapper. Such functions have _require_ in their name to
 * distinguish them.
 */

/**
 * gem_handle_to_libdrm_bo:
 * @bufmgr: libdrm buffer manager instance
 * @fd: open i915 drm file descriptor
 * @name: buffer name in libdrm
 * @handle: gem buffer object handle
 *
 * This helper function imports a raw gem buffer handle into the libdrm buffer
 * manager.
 *
 * Returns: The imported libdrm buffer manager object.
 */
drm_intel_bo *
gem_handle_to_libdrm_bo(drm_intel_bufmgr *bufmgr, int fd, const char *name, uint32_t handle)
{
	struct drm_gem_flink flink;
	int ret;
	drm_intel_bo *bo;

	memset(&flink, 0, sizeof(handle));
	flink.handle = handle;
	ret = ioctl(fd, DRM_IOCTL_GEM_FLINK, &flink);
	igt_assert(ret == 0);
	errno = 0;

	bo = drm_intel_bo_gem_create_from_name(bufmgr, name, flink.name);
	igt_assert(bo);

	return bo;
}

/**
 * gem_get_tiling:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @tiling: (out) tiling mode of the gem buffer
 * @swizzle: (out) bit 6 swizzle mode
 *
 * This wraps the GET_TILING ioctl.
 */
void
gem_get_tiling(int fd, uint32_t handle, uint32_t *tiling, uint32_t *swizzle)
{
	struct drm_i915_gem_get_tiling get_tiling;
	int ret;

	memset(&get_tiling, 0, sizeof(get_tiling));
	get_tiling.handle = handle;

	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_GET_TILING, &get_tiling);
	igt_assert(ret == 0);

	*tiling = get_tiling.tiling_mode;
	*swizzle = get_tiling.swizzle_mode;
}

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

	errno = 0;
	igt_assert(st.tiling_mode == tiling);
	return 0;
}

/**
 * gem_set_tiling:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @tiling: tiling mode bits
 * @stride: stride of the buffer when using a tiled mode, otherwise must be 0
 *
 * This wraps the SET_TILING ioctl.
 */
void gem_set_tiling(int fd, uint32_t handle, uint32_t tiling, uint32_t stride)
{
	igt_assert(__gem_set_tiling(fd, handle, tiling, stride) == 0);
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

/**
 * gem_set_caching:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @caching: caching mode bits
 *
 * This wraps the SET_CACHING ioctl. Note that this function internally calls
 * igt_require() when SET_CACHING isn't available, hence automatically skips the
 * test. Therefore always extract test logic which uses this into its own
 * subtest.
 */
void gem_set_caching(int fd, uint32_t handle, uint32_t caching)
{
	struct local_drm_i915_gem_caching arg;
	int ret;

	memset(&arg, 0, sizeof(arg));
	arg.handle = handle;
	arg.caching = caching;
	ret = ioctl(fd, LOCAL_DRM_IOCTL_I915_GEM_SET_CACHEING, &arg);

	igt_assert(ret == 0 || (errno == ENOTTY || errno == EINVAL));
	igt_require(ret == 0);
	errno = 0;
}

/**
 * gem_get_caching:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 *
 * This wraps the GET_CACHING ioctl.
 *
 * Returns: The current caching mode bits.
 */
uint32_t gem_get_caching(int fd, uint32_t handle)
{
	struct local_drm_i915_gem_caching arg;
	int ret;

	arg.handle = handle;
	arg.caching = 0;
	ret = ioctl(fd, LOCAL_DRM_IOCTL_I915_GEM_GET_CACHEING, &arg);
	igt_assert(ret == 0);
	errno = 0;

	return arg.caching;
}

/**
 * gem_open:
 * @fd: open i915 drm file descriptor
 * @name: flink buffer name
 *
 * This wraps the GEM_OPEN ioctl, which is used to import an flink name.
 *
 * Returns: gem file-private buffer handle of the open object.
 */
uint32_t gem_open(int fd, uint32_t name)
{
	struct drm_gem_open open_struct;
	int ret;

	memset(&open_struct, 0, sizeof(open_struct));
	open_struct.name = name;
	ret = ioctl(fd, DRM_IOCTL_GEM_OPEN, &open_struct);
	igt_assert(ret == 0);
	igt_assert(open_struct.handle != 0);
	errno = 0;

	return open_struct.handle;
}

/**
 * gem_flink:
 * @fd: open i915 drm file descriptor
 * @handle: file-private gem buffer object handle
 *
 * This wraps the GEM_FLINK ioctl, which is used to export a gem buffer object
 * into the device-global flink namespace. See gem_open() for opening such a
 * buffer name on a different i915 drm file descriptor.
 *
 * Returns: The created flink buffer name.
 */
uint32_t gem_flink(int fd, uint32_t handle)
{
	struct drm_gem_flink flink;
	int ret;

	memset(&flink, 0, sizeof(flink));
	flink.handle = handle;
	ret = ioctl(fd, DRM_IOCTL_GEM_FLINK, &flink);
	igt_assert(ret == 0);
	errno = 0;

	return flink.name;
}

/**
 * gem_close:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 *
 * This wraps the GEM_CLOSE ioctl, which to release a file-private gem buffer
 * handle.
 */
void gem_close(int fd, uint32_t handle)
{
	struct drm_gem_close close_bo;

	memset(&close_bo, 0, sizeof(close_bo));
	close_bo.handle = handle;
	do_ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_bo);
}

/**
 * gem_write:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @offset: offset within the buffer of the subrange
 * @buf: pointer to the data to write into the buffer
 * @length: size of the subrange
 *
 * This wraps the PWRITE ioctl, which is to upload a linear data to a subrange
 * of a gem buffer object.
 */
void gem_write(int fd, uint32_t handle, uint32_t offset, const void *buf, uint32_t length)
{
	struct drm_i915_gem_pwrite gem_pwrite;

	memset(&gem_pwrite, 0, sizeof(gem_pwrite));
	gem_pwrite.handle = handle;
	gem_pwrite.offset = offset;
	gem_pwrite.size = length;
	gem_pwrite.data_ptr = (uintptr_t)buf;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite);
}

/**
 * gem_read:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @offset: offset within the buffer of the subrange
 * @buf: pointer to the data to read into
 * @length: size of the subrange
 *
 * This wraps the PREAD ioctl, which is to download a linear data to a subrange
 * of a gem buffer object.
 */
void gem_read(int fd, uint32_t handle, uint32_t offset, void *buf, uint32_t length)
{
	struct drm_i915_gem_pread gem_pread;

	memset(&gem_pread, 0, sizeof(gem_pread));
	gem_pread.handle = handle;
	gem_pread.offset = offset;
	gem_pread.size = length;
	gem_pread.data_ptr = (uintptr_t)buf;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_PREAD, &gem_pread);
}

/**
 * gem_set_domain:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @read_domains: gem domain bits for read access
 * @write_domain: gem domain bit for write access
 *
 * This wraps the SET_DOMAIN ioctl, which is used to control the coherency of
 * the gem buffer object between the cpu and gtt mappings. It is also use to
 * synchronize with outstanding rendering in general, but for that use-case
 * please have a look at gem_sync().
 */
void gem_set_domain(int fd, uint32_t handle,
		    uint32_t read_domains, uint32_t write_domain)
{
	struct drm_i915_gem_set_domain set_domain;

	memset(&set_domain, 0, sizeof(set_domain));
	set_domain.handle = handle;
	set_domain.read_domains = read_domains;
	set_domain.write_domain = write_domain;

	do_ioctl(fd, DRM_IOCTL_I915_GEM_SET_DOMAIN, &set_domain);
}

/**
 * gem_sync:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 *
 * This is a wrapper around gem_set_domain() which simply blocks for any
 * outstanding rendering to complete.
 */
void gem_sync(int fd, uint32_t handle)
{
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
}

uint32_t __gem_create(int fd, int size)
{
	struct drm_i915_gem_create create;
	int ret;

	memset(&create, 0, sizeof(create));
	create.handle = 0;
	create.size = size;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);

	if (ret < 0)
		return 0;

	errno = 0;
	return create.handle;
}

/**
 * gem_create:
 * @fd: open i915 drm file descriptor
 * @size: desired size of the buffer
 *
 * This wraps the GEM_CREATE ioctl, which allocates a new gem buffer object of
 * @size.
 *
 * Returns: The file-private handle of the created buffer object
 */
uint32_t gem_create(int fd, int size)
{
	struct drm_i915_gem_create create;

	memset(&create, 0, sizeof(create));
	create.handle = 0;
	create.size = size;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);
	igt_assert(create.handle);

	return create.handle;
}

/**
 * gem_execbuf:
 * @fd: open i915 drm file descriptor
 * @execbuf: execbuffer data structure
 *
 * This wraps the EXECBUFFER2 ioctl, which submits a batchbuffer for the gpu to
 * run.
 */
void gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *execbuf)
{
	int ret;

	ret = drmIoctl(fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER2,
		       execbuf);
	igt_assert(ret == 0);
	errno = 0;
}

/**
 * gem_mmap__gtt:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @size: size of the gem buffer
 * @prot: memory protection bits as used by mmap()
 *
 * This functions wraps up procedure to establish a memory mapping through the
 * GTT.
 *
 * Returns: A pointer to the created memory mapping.
 */
void *gem_mmap__gtt(int fd, uint32_t handle, int size, int prot)
{
	struct drm_i915_gem_mmap_gtt mmap_arg;
	void *ptr;

	memset(&mmap_arg, 0, sizeof(mmap_arg));
	mmap_arg.handle = handle;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_arg))
		return NULL;

	ptr = mmap64(0, size, prot, MAP_SHARED, fd, mmap_arg.offset);
	if (ptr == MAP_FAILED)
		ptr = NULL;

	return ptr;
}

/**
 * gem_mmap__cpu:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @size: size of the gem buffer
 * @prot: memory protection bits as used by mmap()
 *
 * This functions wraps up procedure to establish a memory mapping through
 * direct cpu access, bypassing the gpu completely.
 *
 * Returns: A pointer to the created memory mapping.
 */
void *gem_mmap__cpu(int fd, uint32_t handle, int size, int prot)
{
	struct drm_i915_gem_mmap mmap_arg;

	memset(&mmap_arg, 0, sizeof(mmap_arg));
	mmap_arg.handle = handle;
	mmap_arg.offset = 0;
	mmap_arg.size = size;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_MMAP, &mmap_arg))
		return NULL;

	errno = 0;
	return (void *)(uintptr_t)mmap_arg.addr_ptr;
}

/**
 * gem_madvise:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @state: desired madvise state
 *
 * This is a wraps the MADVISE ioctl, which is used in libdrm to implement
 * opportunistic buffer object caching. Objects in the cache are set to DONTNEED
 * (internally in the kernel tracked as purgeable objects). When such a cached
 * object is in need again it must be set back to WILLNEED before first use.
 *
 * Returns: When setting the madvise state to WILLNEED this returns whether the
 * backing storage was still avialable or not.
 */
int gem_madvise(int fd, uint32_t handle, int state)
{
	struct drm_i915_gem_madvise madv;

	memset(&madv, 0, sizeof(madv));
	madv.handle = handle;
	madv.madv = state;
	madv.retained = 1;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_MADVISE, &madv);

	return madv.retained;
}

/**
 * gem_context_create:
 * @fd: open i915 drm file descriptor
 *
 * This is a wraps the CONTEXT_CREATE ioctl, which is used to allocate a new
 * hardware context. Not that similarly to gem_set_caching() this wrapper calls
 * igt_require() internally to correctly skip on kernels and platforms where hw
 * context support is not available.
 *
 * Returns: The id of the allocated hw context.
 */
uint32_t gem_context_create(int fd)
{
	struct drm_i915_gem_context_create create;
	int ret;

	memset(&create, 0, sizeof(create));
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, &create);
	igt_require(ret == 0 || (errno != ENODEV && errno != EINVAL));
	igt_assert(ret == 0);
	errno = 0;

	return create.ctx_id;
}

/**
 * gem_sw_finish:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 *
 * This is a wraps the SW_FINISH ioctl, which is used to flush out frontbuffer
 * rendering done through the direct cpu memory mappings. Shipping userspace
 * does _not_ call this after frontbuffer rendering through gtt memory mappings.
 */
void gem_sw_finish(int fd, uint32_t handle)
{
	struct drm_i915_gem_sw_finish finish;

	memset(&finish, 0, sizeof(finish));
	finish.handle = handle;

	do_ioctl(fd, DRM_IOCTL_I915_GEM_SW_FINISH, &finish);
}

/**
 * gem_bo_busy:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 *
 * This is a wraps the BUSY ioctl, which tells whether a buffer object is still
 * actively used by the gpu in a execbuffer.
 *
 * Returns: The busy state of the buffer object.
 */
bool gem_bo_busy(int fd, uint32_t handle)
{
	struct drm_i915_gem_busy busy;

	memset(&busy, 0, sizeof(busy));
	busy.handle = handle;

	do_ioctl(fd, DRM_IOCTL_I915_GEM_BUSY, &busy);

	return !!busy.busy;
}


/* feature test helpers */

/**
 * gem_uses_aliasing_ppgtt:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to check whether the kernel internally uses ppgtt to
 * execute batches. The /aliasing/ in the function name is a bit a misnomer,
 * this driver parameter is also true when full ppgtt address spaces are
 * availabel since for batchbuffer construction only ppgtt or global gtt is
 * relevant.
 *
 * Returns: Whether batches are run through ppgtt.
 */
bool gem_uses_aliasing_ppgtt(int fd)
{
	struct drm_i915_getparam gp;
	int val = 0;

	memset(&gp, 0, sizeof(gp));
	gp.param = 18; /* HAS_ALIASING_PPGTT */
	gp.value = &val;

	if (ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp)))
		return 0;

	errno = 0;
	return val;
}

/**
 * gem_uses_aliasing_ppgtt:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query the kernel for the number of available fences
 * useable in a batchbuffer. Only relevant for pre-gen4.
 *
 * Returns: The number of available fences.
 */
int gem_available_fences(int fd)
{
	struct drm_i915_getparam gp;
	int val = 0;

	memset(&gp, 0, sizeof(gp));
	gp.param = I915_PARAM_NUM_FENCES_AVAIL;
	gp.value = &val;

	if (ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp)))
		return 0;

	errno = 0;
	return val;
}

/**
 * gem_get_num_rings:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query the number of avaible rings. This is useful in
 * test loops which need to step through all rings and similar logic.
 *
 * For more explicit tests of ring availability see gem_has_enable_ring() and
 * the ring specific versions like gem_has_bsd().
 *
 * Returns: The number of available rings.
 */
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

/**
 * gem_has_enable_ring:
 * @fd: open i915 drm file descriptor
 * @param: ring flag bit as used in gem_execbuf()
 *
 * Feature test macro to query whether a specific ring is available.
 *
 * Returns: Whether the ring is avaible or not.
 */
bool gem_has_enable_ring(int fd,int param)
{
	drm_i915_getparam_t gp;
	int tmp = 0;

	memset(&gp, 0, sizeof(gp));
	gp.value = &tmp;
	gp.param = param;

	if (drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
		return false;

	errno = 0;
	return tmp > 0;
}

/**
 * gem_has_bsd:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the BSD ring is available. This is simply
 * a specific version of gem_has_enable_ring() for the BSD ring.
 *
 * Note that recent Bspec calls this the VCS ring for Video Command Submission.
 *
 * Returns: Whether the BSD ring is avaible or not.
 */
bool gem_has_bsd(int fd)
{
	return gem_has_enable_ring(fd,I915_PARAM_HAS_BSD);
}

/**
 * gem_has_blt:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the blitter ring is available. This is simply
 * a specific version of gem_has_enable_ring() for the blitter ring.
 *
 * Note that recent Bspec calls this the BCS ring for Blitter Command Submission.
 *
 * Returns: Whether the blitter ring is avaible or not.
 */
bool gem_has_blt(int fd)
{
	return gem_has_enable_ring(fd,I915_PARAM_HAS_BLT);
}

#define LOCAL_I915_PARAM_HAS_VEBOX 22
/**
 * gem_has_vebox:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the vebox ring is available. This is simply
 * a specific version of gem_has_enable_ring() for the vebox ring.
 *
 * Note that recent Bspec calls this the VECS ring for Video Enhancement Command
 * Submission.
 *
 * Returns: Whether the vebox ring is avaible or not.
 */
bool gem_has_vebox(int fd)
{
	return gem_has_enable_ring(fd,LOCAL_I915_PARAM_HAS_VEBOX);
}

/**
 * gem_available_aperture_size:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query the kernel for the available gpu aperture size
 * useable in a batchbuffer.
 *
 * Returns: The available gtt address space size.
 */
uint64_t gem_available_aperture_size(int fd)
{
	struct drm_i915_gem_get_aperture aperture;

	memset(&aperture, 0, sizeof(aperture));
	aperture.aper_size = 256*1024*1024;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_GET_APERTURE, &aperture);

	return aperture.aper_available_size;
}

/**
 * gem_aperture_size:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query the kernel for the total gpu aperture size.
 *
 * Returns: The total gtt address space size.
 */
uint64_t gem_aperture_size(int fd)
{
	struct drm_i915_gem_get_aperture aperture;

	memset(&aperture, 0, sizeof(aperture));
	aperture.aper_size = 256*1024*1024;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_GET_APERTURE, &aperture);

	return aperture.aper_size;
}

/**
 * gem_aperture_size:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query the kernel for the mappable gpu aperture size.
 * This is the area avaialble for GTT memory mappings.
 *
 * Returns: The mappable gtt address space size.
 */
uint64_t gem_mappable_aperture_size(void)
{
	struct pci_device *pci_dev = intel_get_pci_device();
	int bar;

	if (intel_gen(pci_dev->device_id) < 3)
		bar = 0;
	else
		bar = 2;

	return pci_dev->regions[bar].size;
}

/**
 * gem_require_caching:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether buffer object caching control is
 * available. Automatically skips through igt_require() if not.
 */
void gem_require_caching(int fd)
{
	struct local_drm_i915_gem_caching arg;
	int ret;

	memset(&arg, 0, sizeof(arg));
	arg.handle = gem_create(fd, 4096);
	igt_assert(arg.handle != 0);

	arg.caching = 0;
	ret = ioctl(fd, LOCAL_DRM_IOCTL_I915_GEM_SET_CACHEING, &arg);
	gem_close(fd, arg.handle);

	igt_require(ret == 0);
	errno = 0;
}

/**
 * gem_require_ring:
 * @fd: open i915 drm file descriptor
 * @ring_id: ring flag bit as used in gem_execbuf()
 *
 * Feature test macro to query whether a specific ring is available.
 * In contrast to gem_has_enable_ring() this automagically skips if the ring
 * isn't available by calling igt_require().
 */
void gem_require_ring(int fd, int ring_id)
{
	switch (ring_id) {
	case I915_EXEC_RENDER:
		return;
	case I915_EXEC_BLT:
		igt_require(HAS_BLT_RING(intel_get_drm_devid(fd)));
		return;
	case I915_EXEC_BSD:
		igt_require(HAS_BSD_RING(intel_get_drm_devid(fd)));
		return;
#ifdef I915_EXEC_VEBOX
	case I915_EXEC_VEBOX:
		igt_require(gem_has_vebox(fd));
		return;
#endif
	default:
		igt_assert(0);
		return;
	}
}

/* prime */

/**
 * prime_handle_to_fd:
 * @fd: open i915 drm file descriptor
 * @handle: file-private gem buffer object handle
 *
 * This wraps the PRIME_HANDLE_TO_FD ioctl, which is used to export a gem buffer
 * object into a global (i.e. potentially cross-device) dma-buf file-descriptor
 * handle.
 *
 * Returns: The created dma-buf fd handle.
 */
int prime_handle_to_fd(int fd, uint32_t handle)
{
	struct drm_prime_handle args;

	memset(&args, 0, sizeof(args));
	args.handle = handle;
	args.flags = DRM_CLOEXEC;
	args.fd = -1;

	do_ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);

	return args.fd;
}

/**
 * prime_fd_to_handle:
 * @fd: open i915 drm file descriptor
 * @dma_buf_fd: dma-buf fd handle
 *
 * This wraps the PRIME_FD_TO_HANDLE ioctl, which is used to import a dma-buf
 * file-descriptor into a gem buffer object.
 *
 * Returns: The created gem buffer object handle.
 */
uint32_t prime_fd_to_handle(int fd, int dma_buf_fd)
{
	struct drm_prime_handle args;

	memset(&args, 0, sizeof(args));
	args.fd = dma_buf_fd;
	args.flags = 0;
	args.handle = 0;

	do_ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &args);

	return args.handle;
}

/**
 * prime_get_size:
 * @dma_buf_fd: dma-buf fd handle
 *
 * This wraps the lseek() protocol used to query the invariant size of a
 * dma-buf.  Not all kernels support this, which is check with igt_require() and
 * so will result in automagic test skipping.
 *
 * Returns: The lifetime-invariant size of the dma-buf object.
 */
off_t prime_get_size(int dma_buf_fd)
{
	off_t ret;

	ret = lseek(dma_buf_fd, 0, SEEK_END);
	igt_assert(ret >= 0 || errno == ESPIPE);
	igt_require(ret >= 0);
	errno = 0;

	return ret;
}

