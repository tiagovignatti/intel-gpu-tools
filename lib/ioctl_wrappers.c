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
 * @include: igt.h
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

int (*igt_ioctl)(int fd, unsigned long request, void *arg) = drmIoctl;


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

	ret = igt_ioctl(fd, DRM_IOCTL_I915_GEM_GET_TILING, &get_tiling);
	igt_assert(ret == 0);

	*tiling = get_tiling.tiling_mode;
	*swizzle = get_tiling.swizzle_mode;
}

int __gem_set_tiling(int fd, uint32_t handle, uint32_t tiling, uint32_t stride)
{
	struct drm_i915_gem_set_tiling st;
	int ret;

	igt_require_intel(fd);

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

static int __gem_set_caching(int fd, uint32_t handle, uint32_t caching)
{
	struct local_drm_i915_gem_caching arg;
	int err;

	memset(&arg, 0, sizeof(arg));
	arg.handle = handle;
	arg.caching = caching;

	err = 0;
	if (igt_ioctl(fd, LOCAL_DRM_IOCTL_I915_GEM_SET_CACHEING, &arg)) {
		err = -errno;
		igt_assert(errno == ENOTTY || errno == EINVAL);
	}
	return err;
}

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
	igt_require(__gem_set_caching(fd, handle, caching) == 0);
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

	memset(&arg, 0, sizeof(arg));
	arg.handle = handle;
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

	igt_assert_neq(handle, 0);

	memset(&close_bo, 0, sizeof(close_bo));
	close_bo.handle = handle;
	do_ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_bo);
}

static int __gem_write(int fd, uint32_t handle, uint64_t offset, const void *buf, uint64_t length)
{
	struct drm_i915_gem_pwrite gem_pwrite;
	int err;

	memset(&gem_pwrite, 0, sizeof(gem_pwrite));
	gem_pwrite.handle = handle;
	gem_pwrite.offset = offset;
	gem_pwrite.size = length;
	gem_pwrite.data_ptr = (uintptr_t)buf;

	err = 0;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite))
		err = -errno;
	return err;
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
void gem_write(int fd, uint32_t handle, uint64_t offset, const void *buf, uint64_t length)
{
	igt_assert_eq(__gem_write(fd, handle, offset, buf, length), 0);
}

static int __gem_read(int fd, uint32_t handle, uint64_t offset, void *buf, uint64_t length)
{
	struct drm_i915_gem_pread gem_pread;
	int err;

	memset(&gem_pread, 0, sizeof(gem_pread));
	gem_pread.handle = handle;
	gem_pread.offset = offset;
	gem_pread.size = length;
	gem_pread.data_ptr = (uintptr_t)buf;

	err = 0;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_PREAD, &gem_pread))
		err = -errno;
	return err;
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
void gem_read(int fd, uint32_t handle, uint64_t offset, void *buf, uint64_t length)
{
	igt_assert_eq(__gem_read(fd, handle, offset, buf, length), 0);
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
 * __gem_wait:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @timeout_ns: [in] time to wait, [out] remaining time (in nanoseconds)
 *
 * This functions waits for outstanding rendering to complete, upto
 * the timeout_ns. If no timeout_ns is provided, the wait is indefinite and
 * only returns upon an error or when the rendering is complete.
 */
int gem_wait(int fd, uint32_t handle, int64_t *timeout_ns)
{
	struct drm_i915_gem_wait wait;
	int ret;

	memset(&wait, 0, sizeof(wait));
	wait.bo_handle = handle;
	wait.timeout_ns = timeout_ns ? *timeout_ns : -1;
	wait.flags = 0;

	ret = 0;
	if (igt_ioctl(fd, DRM_IOCTL_I915_GEM_WAIT, &wait))
		ret = -errno;

	if (timeout_ns)
		*timeout_ns = wait.timeout_ns;

	return ret;
}

/**
 * gem_sync:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 *
 * This functions waits for outstanding rendering to complete.
 */
void gem_sync(int fd, uint32_t handle)
{
	if (gem_wait(fd, handle, NULL))
		gem_set_domain(fd, handle,
			       I915_GEM_DOMAIN_GTT,
			       I915_GEM_DOMAIN_GTT);
	errno = 0;
}


bool gem_create__has_stolen_support(int fd)
{
	static int has_stolen_support = -1;
	struct drm_i915_getparam gp;
	int val = -1;

	if (has_stolen_support < 0) {
		memset(&gp, 0, sizeof(gp));
		gp.param = 36; /* CREATE_VERSION */
		gp.value = &val;

		/* Do we have the extended gem_create_ioctl? */
		ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
		has_stolen_support = val >= 2;
	}

	return has_stolen_support;
}

struct local_i915_gem_create_v2 {
	uint64_t size;
	uint32_t handle;
	uint32_t pad;
#define I915_CREATE_PLACEMENT_STOLEN (1<<0)
	uint32_t flags;
};

#define LOCAL_IOCTL_I915_GEM_CREATE       DRM_IOWR(DRM_COMMAND_BASE + DRM_I915_GEM_CREATE, struct local_i915_gem_create_v2)
uint32_t __gem_create_stolen(int fd, uint64_t size)
{
	struct local_i915_gem_create_v2 create;
	int ret;

	memset(&create, 0, sizeof(create));
	create.handle = 0;
	create.size = size;
	create.flags = I915_CREATE_PLACEMENT_STOLEN;
	ret = igt_ioctl(fd, LOCAL_IOCTL_I915_GEM_CREATE, &create);

	if (ret < 0)
		return 0;

	errno = 0;
	return create.handle;
}

/**
 * gem_create_stolen:
 * @fd: open i915 drm file descriptor
 * @size: desired size of the buffer
 *
 * This wraps the new GEM_CREATE ioctl, which allocates a new gem buffer
 * object of @size and placement in stolen memory region.
 *
 * Returns: The file-private handle of the created buffer object
 */

uint32_t gem_create_stolen(int fd, uint64_t size)
{
	struct local_i915_gem_create_v2 create;

	memset(&create, 0, sizeof(create));
	create.handle = 0;
	create.size = size;
	create.flags = I915_CREATE_PLACEMENT_STOLEN;
	do_ioctl(fd, LOCAL_IOCTL_I915_GEM_CREATE, &create);
	igt_assert(create.handle);

	return create.handle;
}


uint32_t __gem_create(int fd, int size)
{
	struct drm_i915_gem_create create;
	int ret;

	memset(&create, 0, sizeof(create));
	create.handle = 0;
	create.size = size;
	ret = igt_ioctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);

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
uint32_t gem_create(int fd, uint64_t size)
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
 * __gem_execbuf:
 * @fd: open i915 drm file descriptor
 * @execbuf: execbuffer data structure
 *
 * This wraps the EXECBUFFER2 ioctl, which submits a batchbuffer for the gpu to
 * run. This is allowed to fail, with -errno returned.
 */
int __gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *execbuf)
{
	int err = 0;
	if (igt_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, execbuf))
		err = -errno;
	errno = 0;
	return err;
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
	igt_assert_eq(__gem_execbuf(fd, execbuf), 0);
}

/**
 * __gem_mmap__gtt:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @size: size of the gem buffer
 * @prot: memory protection bits as used by mmap()
 *
 * This functions wraps up procedure to establish a memory mapping through the
 * GTT.
 *
 * Returns: A pointer to the created memory mapping, NULL on failure.
 */
void *__gem_mmap__gtt(int fd, uint32_t handle, uint64_t size, unsigned prot)
{
	struct drm_i915_gem_mmap_gtt mmap_arg;
	void *ptr;

	memset(&mmap_arg, 0, sizeof(mmap_arg));
	mmap_arg.handle = handle;
	if (igt_ioctl(fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_arg))
		return NULL;

	ptr = mmap64(0, size, prot, MAP_SHARED, fd, mmap_arg.offset);
	if (ptr == MAP_FAILED)
		ptr = NULL;
	else
		errno = 0;

	return ptr;
}

/**
 * gem_mmap__gtt:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @size: size of the gem buffer
 * @prot: memory protection bits as used by mmap()
 *
 * Like __gem_mmap__gtt() except we assert on failure.
 *
 * Returns: A pointer to the created memory mapping
 */
void *gem_mmap__gtt(int fd, uint32_t handle, uint64_t size, unsigned prot)
{
	void *ptr = __gem_mmap__gtt(fd, handle, size, prot);
	igt_assert(ptr);
	return ptr;
}

struct local_i915_gem_mmap_v2 {
	uint32_t handle;
	uint32_t pad;
	uint64_t offset;
	uint64_t size;
	uint64_t addr_ptr;
	uint64_t flags;
#define I915_MMAP_WC 0x1
};
#define LOCAL_IOCTL_I915_GEM_MMAP_v2 DRM_IOWR(DRM_COMMAND_BASE + DRM_I915_GEM_MMAP, struct local_i915_gem_mmap_v2)

bool gem_mmap__has_wc(int fd)
{
	static int has_wc = -1;

	if (has_wc == -1) {
		struct drm_i915_getparam gp;
		int val = -1;

		has_wc = 0;

		memset(&gp, 0, sizeof(gp));
		gp.param = 30; /* MMAP_VERSION */
		gp.value = &val;

		/* Do we have the new mmap_ioctl? */
		ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
		if (val >= 1) {
			struct local_i915_gem_mmap_v2 arg;

			/* Does this device support wc-mmaps ? */
			memset(&arg, 0, sizeof(arg));
			arg.handle = gem_create(fd, 4096);
			arg.offset = 0;
			arg.size = 4096;
			arg.flags = I915_MMAP_WC;
			has_wc = igt_ioctl(fd, LOCAL_IOCTL_I915_GEM_MMAP_v2, &arg) == 0;
			gem_close(fd, arg.handle);
		}
		errno = 0;
	}

	return has_wc > 0;
}

/**
 * __gem_mmap__wc:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @offset: offset in the gem buffer of the mmap arena
 * @size: size of the mmap arena
 * @prot: memory protection bits as used by mmap()
 *
 * This functions wraps up procedure to establish a memory mapping through
 * direct cpu access, bypassing the gpu and cpu caches completely and also
 * bypassing the GTT system agent (i.e. there is no automatic tiling of
 * the mmapping through the fence registers).
 *
 * Returns: A pointer to the created memory mapping, NULL on failure.
 */
void *__gem_mmap__wc(int fd, uint32_t handle, uint64_t offset, uint64_t size, unsigned prot)
{
	struct local_i915_gem_mmap_v2 arg;

	if (!gem_mmap__has_wc(fd)) {
		errno = ENOSYS;
		return NULL;
	}

	memset(&arg, 0, sizeof(arg));
	arg.handle = handle;
	arg.offset = offset;
	arg.size = size;
	arg.flags = I915_MMAP_WC;
	if (igt_ioctl(fd, LOCAL_IOCTL_I915_GEM_MMAP_v2, &arg))
		return NULL;

	errno = 0;
	return (void *)(uintptr_t)arg.addr_ptr;
}

/**
 * gem_mmap__wc:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @offset: offset in the gem buffer of the mmap arena
 * @size: size of the mmap arena
 * @prot: memory protection bits as used by mmap()
 *
 * Like __gem_mmap__wc() except we assert on failure.
 *
 * Returns: A pointer to the created memory mapping
 */
void *gem_mmap__wc(int fd, uint32_t handle, uint64_t offset, uint64_t size, unsigned prot)
{
	void *ptr = __gem_mmap__wc(fd, handle, offset, size, prot);
	igt_assert(ptr);
	return ptr;
}

/**
 * __gem_mmap__cpu:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @offset: offset in the gem buffer of the mmap arena
 * @size: size of the mmap arena
 * @prot: memory protection bits as used by mmap()
 *
 * This functions wraps up procedure to establish a memory mapping through
 * direct cpu access, bypassing the gpu completely.
 *
 * Returns: A pointer to the created memory mapping, NULL on failure.
 */
void *__gem_mmap__cpu(int fd, uint32_t handle, uint64_t offset, uint64_t size, unsigned prot)
{
	struct drm_i915_gem_mmap mmap_arg;

	memset(&mmap_arg, 0, sizeof(mmap_arg));
	mmap_arg.handle = handle;
	mmap_arg.offset = offset;
	mmap_arg.size = size;
	if (igt_ioctl(fd, DRM_IOCTL_I915_GEM_MMAP, &mmap_arg))
		return NULL;

	errno = 0;
	return (void *)(uintptr_t)mmap_arg.addr_ptr;
}

/**
 * gem_mmap__cpu:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @offset: offset in the gem buffer of the mmap arena
 * @size: size of the mmap arena
 * @prot: memory protection bits as used by mmap()
 *
 * Like __gem_mmap__cpu() except we assert on failure.
 *
 * Returns: A pointer to the created memory mapping
 */
void *gem_mmap__cpu(int fd, uint32_t handle, uint64_t offset, uint64_t size, unsigned prot)
{
	void *ptr = __gem_mmap__cpu(fd, handle, offset, size, prot);
	igt_assert(ptr);
	return ptr;
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
 * backing storage was still available or not.
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

	memset(&create, 0, sizeof(create));
	if (igt_ioctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, &create)) {
		int err = -errno;
		igt_skip_on(err == -ENODEV || errno == -EINVAL);
		igt_assert_eq(err, 0);
	}
	igt_assert(create.ctx_id != 0);
	errno = 0;

	return create.ctx_id;
}

int __gem_context_destroy(int fd, uint32_t ctx_id)
{
	struct drm_i915_gem_context_destroy destroy;
	int ret;

	memset(&destroy, 0, sizeof(destroy));
	destroy.ctx_id = ctx_id;

	ret = igt_ioctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_DESTROY, &destroy);
	if (ret)
		return -errno;
	return 0;
}

/**
 * gem_context_destroy:
 * @fd: open i915 drm file descriptor
 * @ctx_id: i915 hw context id
 *
 * This is a wraps the CONTEXT_DESTROY ioctl, which is used to free a hardware
 * context.
 */
void gem_context_destroy(int fd, uint32_t ctx_id)
{
	struct drm_i915_gem_context_destroy destroy;

	memset(&destroy, 0, sizeof(destroy));
	destroy.ctx_id = ctx_id;

	do_ioctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_DESTROY, &destroy);
}

/**
 * gem_context_get_param:
 * @fd: open i915 drm file descriptor
 * @p: i915 hw context parameter
 *
 * This is a wraps the CONTEXT_GET_PARAM ioctl, which is used to free a hardware
 * context. Not that similarly to gem_set_caching() this wrapper calls
 * igt_require() internally to correctly skip on kernels and platforms where hw
 * context parameter support is not available.
 */
void gem_context_get_param(int fd, struct local_i915_gem_context_param *p)
{
#define LOCAL_I915_GEM_CONTEXT_GETPARAM       0x34
#define LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM DRM_IOWR (DRM_COMMAND_BASE + LOCAL_I915_GEM_CONTEXT_GETPARAM, struct local_i915_gem_context_param)
	do_ioctl(fd, LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM, p);
}

int __gem_context_set_param(int fd, struct local_i915_gem_context_param *p)
{
#define LOCAL_I915_GEM_CONTEXT_SETPARAM       0x35
#define LOCAL_IOCTL_I915_GEM_CONTEXT_SETPARAM DRM_IOWR (DRM_COMMAND_BASE + LOCAL_I915_GEM_CONTEXT_SETPARAM, struct local_i915_gem_context_param)
	if (igt_ioctl(fd, LOCAL_IOCTL_I915_GEM_CONTEXT_SETPARAM, p))
		return -errno;

	errno = 0;
	return 0;
}
/**
 * gem_context_set_param:
 * @fd: open i915 drm file descriptor
 * @p: i915 hw context parameter
 *
 * This is a wraps the CONTEXT_SET_PARAM ioctl, which is used to free a hardware
 * context. Not that similarly to gem_set_caching() this wrapper calls
 * igt_require() internally to correctly skip on kernels and platforms where hw
 * context parameter support is not available.
 */
void gem_context_set_param(int fd, struct local_i915_gem_context_param *p)
{
	igt_assert(__gem_context_set_param(fd, p) == 0);
}

/**
 * gem_context_require_param:
 * @fd: open i915 drm file descriptor
 * @param: i915 hw context parameter
 *
 * Feature test macro to query whether hw context parameter support for @param
 * is available. Automatically skips through igt_require() if not.
 */
void gem_context_require_param(int fd, uint64_t param)
{
	struct local_i915_gem_context_param p;

	p.context = 0;
	p.param = param;
	p.value = 0;
	p.size = 0;

	igt_require(igt_ioctl(fd, LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM, &p) == 0);
}

void gem_context_require_ban_period(int fd)
{
	static int has_ban_period = -1;

	if (has_ban_period < 0) {
		struct local_i915_gem_context_param p;

		p.context = 0;
		p.param = LOCAL_CONTEXT_PARAM_BAN_PERIOD;
		p.value = 0;
		p.size = 0;

		has_ban_period = igt_ioctl(fd, LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM, &p) == 0;
	}

	igt_require(has_ban_period);
}

int __gem_userptr(int fd, void *ptr, int size, int read_only, uint32_t flags, uint32_t *handle)
{
	struct local_i915_gem_userptr userptr;
	int ret;

	memset(&userptr, 0, sizeof(userptr));
	userptr.user_ptr = (uintptr_t)ptr;
	userptr.user_size = size;
	userptr.flags = flags;
	if (read_only)
		userptr.flags |= LOCAL_I915_USERPTR_READ_ONLY;

	ret = igt_ioctl(fd, LOCAL_IOCTL_I915_GEM_USERPTR, &userptr);
	if (ret)
		ret = errno;
	igt_skip_on_f(ret == ENODEV &&
			(flags & LOCAL_I915_USERPTR_UNSYNCHRONIZED) == 0 &&
			!read_only,
			"Skipping, synchronized mappings with no kernel CONFIG_MMU_NOTIFIER?");
	if (ret == 0)
		*handle = userptr.handle;

	return ret;
}

/**
 * gem_userptr:
 * @fd: open i915 drm file descriptor
 * @ptr: userptr pointer to be passed
 * @size: desired size of the buffer
 * @read_only: specify whether userptr is opened read only
 * @flags: other userptr flags
 * @handle: returned handle for the object
 *
 * Returns userptr handle for the GEM object.
 */
void gem_userptr(int fd, void *ptr, int size, int read_only, uint32_t flags, uint32_t *handle)
{
	igt_assert_eq(__gem_userptr(fd, ptr, size, read_only, flags, handle), 0);
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
 * gem_gtt_type:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to check what type of gtt is being used by the kernel:
 * 0 - global gtt
 * 1 - aliasing ppgtt
 * 2 - full ppgtt, limited to 32bit address space
 * 3 - full ppgtt, 64bit address space
 *
 * Returns: Type of gtt being used.
 */
int gem_gtt_type(int fd)
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
 * gem_uses_ppgtt:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to check whether the kernel internally uses ppgtt to
 * execute batches. Note that this is also true when we're using full ppgtt.
 *
 * Returns: Whether batches are run through ppgtt.
 */
bool gem_uses_ppgtt(int fd)
{
	return gem_gtt_type(fd) > 0;
}

/**
 * gem_uses_full_ppgtt:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to check whether the kernel internally uses full
 * per-process gtt to execute batches. Note that this is also true when we're
 * using full 64b ppgtt.
 *
 * Returns: Whether batches are run through full ppgtt.
 */
bool gem_uses_full_ppgtt(int fd)
{
	return gem_gtt_type(fd) > 1;
}

/**
 * gem_available_fences:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query the kernel for the number of available fences
 * usable in a batchbuffer. Only relevant for pre-gen4.
 *
 * Returns: The number of available fences.
 */
int gem_available_fences(int fd)
{
	static int num_fences = -1;

	if (num_fences < 0) {
		struct drm_i915_getparam gp;

		memset(&gp, 0, sizeof(gp));
		gp.param = I915_PARAM_NUM_FENCES_AVAIL;
		gp.value = &num_fences;

		num_fences = 0;
		ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp));
		errno = 0;
	}

	return num_fences;
}

bool gem_has_llc(int fd)
{
	static int has_llc = -1;

	if (has_llc < 0) {
		struct drm_i915_getparam gp;

		memset(&gp, 0, sizeof(gp));
		gp.param = I915_PARAM_HAS_LLC;
		gp.value = &has_llc;

		has_llc = 0;
		ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp));
		errno = 0;
	}

	return has_llc;
}

static bool has_param(int fd, int param)
{
	drm_i915_getparam_t gp;
	int tmp = 0;

	memset(&gp, 0, sizeof(gp));
	gp.value = &tmp;
	gp.param = param;

	if (igt_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
		return false;

	errno = 0;
	return tmp > 0;
}

/**
 * gem_has_bsd:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the BSD ring is available.
 *
 * Note that recent Bspec calls this the VCS ring for Video Command Submission.
 *
 * Returns: Whether the BSD ring is available or not.
 */
bool gem_has_bsd(int fd)
{
	static int has_bsd = -1;
	if (has_bsd < 0)
		has_bsd = has_param(fd, I915_PARAM_HAS_BSD);
	return has_bsd;
}

/**
 * gem_has_blt:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the blitter ring is available.
 *
 * Note that recent Bspec calls this the BCS ring for Blitter Command Submission.
 *
 * Returns: Whether the blitter ring is available or not.
 */
bool gem_has_blt(int fd)
{
	static int has_blt = -1;
	if (has_blt < 0)
		has_blt =  has_param(fd, I915_PARAM_HAS_BLT);
	return has_blt;
}

#define LOCAL_I915_PARAM_HAS_VEBOX 22
/**
 * gem_has_vebox:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the vebox ring is available.
 *
 * Note that recent Bspec calls this the VECS ring for Video Enhancement Command
 * Submission.
 *
 * Returns: Whether the vebox ring is available or not.
 */
bool gem_has_vebox(int fd)
{
	static int has_vebox = -1;
	if (has_vebox < 0)
		has_vebox =  has_param(fd, LOCAL_I915_PARAM_HAS_VEBOX);
	return has_vebox;
}

#define LOCAL_I915_PARAM_HAS_BSD2 31
/**
 * gem_has_bsd2:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the BSD2 ring is available.
 *
 * Note that recent Bspec calls this the VCS ring for Video Command Submission.
 *
 * Returns: Whether the BSD ring is avaible or not.
 */
bool gem_has_bsd2(int fd)
{
	static int has_bsd2 = -1;
	if (has_bsd2 < 0)
		has_bsd2 = has_param(fd, LOCAL_I915_PARAM_HAS_BSD2);
	return has_bsd2;
}
/**
 * gem_available_aperture_size:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query the kernel for the available gpu aperture size
 * usable in a batchbuffer.
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
	static uint64_t aperture_size = 0;

	if (aperture_size == 0) {
		struct local_i915_gem_context_param p;

		memset(&p, 0, sizeof(p));
		p.param = 0x3;
		if (ioctl(fd, LOCAL_IOCTL_I915_GEM_CONTEXT_GETPARAM, &p) == 0) {
			aperture_size = p.value;
		} else {
			struct drm_i915_gem_get_aperture aperture;

			memset(&aperture, 0, sizeof(aperture));
			aperture.aper_size = 256*1024*1024;

			do_ioctl(fd, DRM_IOCTL_I915_GEM_GET_APERTURE, &aperture);
			aperture_size =  aperture.aper_size;
		}
	}

	return aperture_size;
}

/**
 * gem_mappable_aperture_size:
 *
 * Feature test macro to query the kernel for the mappable gpu aperture size.
 * This is the area available for GTT memory mappings.
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
 * gem_global_aperture_size:
 *
 * Feature test macro to query the kernel for the global gpu aperture size.
 * This is the area available for the kernel to perform address translations.
 *
 * Returns: The mappable gtt address space size.
 */
uint64_t gem_global_aperture_size(int fd)
{
	struct drm_i915_gem_get_aperture aperture;

	memset(&aperture, 0, sizeof(aperture));
	aperture.aper_size = 256*1024*1024;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_GET_APERTURE, &aperture);

	return aperture.aper_size;
}

#define LOCAL_I915_PARAM_HAS_EXEC_SOFTPIN 37
/**
 * gem_has_softpin:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the softpinning functionality is
 * supported.
 *
 * Returns: Whether softpin support is available
 */
bool gem_has_softpin(int fd)
{
	static int has_softpin = -1;

	if (has_softpin < 0) {
		struct drm_i915_getparam gp;

		memset(&gp, 0, sizeof(gp));
		gp.param = LOCAL_I915_PARAM_HAS_EXEC_SOFTPIN;
		gp.value = &has_softpin;

		has_softpin = 0;
		ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp));
		errno = 0;
	}

	return has_softpin;
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
	uint32_t handle;

	handle = gem_create(fd, 4096);
	gem_set_caching(fd, handle, 0);
	gem_close(fd, handle);

	errno = 0;
}

bool gem_has_ring(int fd, unsigned ring)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec;

	/* silly ABI, the kernel thinks everyone who has BSD also has BSD2 */
	if ((ring & ~(3<<13)) == I915_EXEC_BSD) {
		if (ring & (3 << 13) && !gem_has_bsd2(fd))
			return false;
	}

	memset(&exec, 0, sizeof(exec));
	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&exec;
	execbuf.buffer_count = 1;
	execbuf.flags = ring;
	return __gem_execbuf(fd, &execbuf) == -ENOENT;
}

/**
 * gem_require_ring:
 * @fd: open i915 drm file descriptor
 * @ring: ring flag bit as used in gem_execbuf()
 *
 * Feature test macro to query whether a specific ring is available.
 * This automagically skips if the ring isn't available by
 * calling igt_require().
 */
void gem_require_ring(int fd, unsigned ring)
{
	igt_require(gem_has_ring(fd, ring));
}

/**
 * gem_has_mocs_registers:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the device has MOCS registers.
 * These exist gen 9+.
 */
bool gem_has_mocs_registers(int fd)
{
	return intel_gen(intel_get_drm_devid(fd)) >= 9;
}

/**
 * gem_require_mocs_registers:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the device has MOCS registers.
 * These exist gen 9+.
 */
void gem_require_mocs_registers(int fd)
{
	igt_require(gem_has_mocs_registers(fd));
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
 * prime_handle_to_fd_for_mmap:
 * @fd: open i915 drm file descriptor
 * @handle: file-private gem buffer object handle
 *
 * Same as prime_handle_to_fd above but with DRM_RDWR capabilities, which can
 * be useful for writing into the mmap'ed dma-buf file-descriptor.
 *
 * Returns: The created dma-buf fd handle or -1 if the ioctl fails.
 */
int prime_handle_to_fd_for_mmap(int fd, uint32_t handle)
{
	struct drm_prime_handle args;

	memset(&args, 0, sizeof(args));
	args.handle = handle;
	args.flags = DRM_CLOEXEC | DRM_RDWR;
	args.fd = -1;

	if (igt_ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args) != 0)
		return -1;

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

/**
 * prime_sync_start
 * @dma_buf_fd: dma-buf fd handle
 */
void prime_sync_start(int dma_buf_fd, bool write)
{
	struct local_dma_buf_sync sync_start;

	memset(&sync_start, 0, sizeof(sync_start));
	sync_start.flags = LOCAL_DMA_BUF_SYNC_START;
	sync_start.flags |= LOCAL_DMA_BUF_SYNC_READ;
	if (write)
		sync_start.flags |= LOCAL_DMA_BUF_SYNC_WRITE;
	do_ioctl(dma_buf_fd, LOCAL_DMA_BUF_IOCTL_SYNC, &sync_start);
}

/**
 * prime_sync_end
 * @dma_buf_fd: dma-buf fd handle
 */
void prime_sync_end(int dma_buf_fd, bool write)
{
	struct local_dma_buf_sync sync_end;

	memset(&sync_end, 0, sizeof(sync_end));
	sync_end.flags = LOCAL_DMA_BUF_SYNC_END;
	sync_end.flags |= LOCAL_DMA_BUF_SYNC_READ;
	if (write)
		sync_end.flags |= LOCAL_DMA_BUF_SYNC_WRITE;
	do_ioctl(dma_buf_fd, LOCAL_DMA_BUF_IOCTL_SYNC, &sync_end);
}

/**
 * igt_require_fb_modifiers:
 * @fd: Open DRM file descriptor.
 *
 * Requires presence of DRM_CAP_ADDFB2_MODIFIERS.
 */
void igt_require_fb_modifiers(int fd)
{
	static bool has_modifiers, cap_modifiers_tested;

	if (!cap_modifiers_tested) {
		uint64_t cap_modifiers;
		int ret;

		ret = drmGetCap(fd, LOCAL_DRM_CAP_ADDFB2_MODIFIERS, &cap_modifiers);
		igt_assert(ret == 0 || errno == EINVAL);
		has_modifiers = ret == 0 && cap_modifiers == 1;
		cap_modifiers_tested = true;
	}

	igt_require(has_modifiers);
}

int __kms_addfb(int fd, uint32_t handle, uint32_t width, uint32_t height,
		uint32_t stride, uint32_t pixel_format, uint64_t modifier,
		uint32_t flags, uint32_t *buf_id)
{
	struct local_drm_mode_fb_cmd2 f;
	int ret;

	igt_require_fb_modifiers(fd);

	memset(&f, 0, sizeof(f));

	f.width  = width;
	f.height = height;
	f.pixel_format = pixel_format;
	f.flags = flags;
	f.handles[0] = handle;
	f.pitches[0] = stride;
	f.modifier[0] = modifier;

	ret = igt_ioctl(fd, LOCAL_DRM_IOCTL_MODE_ADDFB2, &f);

	*buf_id = f.fb_id;

	return ret < 0 ? -errno : ret;
}
