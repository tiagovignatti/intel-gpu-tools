/*
 * Copyright © 2007,2014 Intel Corporation
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


#ifndef IOCTL_WRAPPERS_H
#define IOCTL_WRAPPERS_H

#include <stdint.h>
#include <stdbool.h>
#include <intel_bufmgr.h>
#include <i915_drm.h>

/* libdrm interfacing */
drm_intel_bo * gem_handle_to_libdrm_bo(drm_intel_bufmgr *bufmgr, int fd,
				       const char *name, uint32_t handle);

/* ioctl_wrappers.c:
 *
 * ioctl wrappers and similar stuff for bare metal testing */
void gem_get_tiling(int fd, uint32_t handle, uint32_t *tiling, uint32_t *swizzle);
void gem_set_tiling(int fd, uint32_t handle, uint32_t tiling, uint32_t stride);
int __gem_set_tiling(int fd, uint32_t handle, uint32_t tiling, uint32_t stride);

void gem_set_caching(int fd, uint32_t handle, uint32_t caching);
uint32_t gem_get_caching(int fd, uint32_t handle);
uint32_t gem_flink(int fd, uint32_t handle);
uint32_t gem_open(int fd, uint32_t name);
void gem_close(int fd, uint32_t handle);
void gem_write(int fd, uint32_t handle, uint32_t offset,  const void *buf, uint32_t length);
void gem_read(int fd, uint32_t handle, uint32_t offset, void *buf, uint32_t length);
void gem_set_domain(int fd, uint32_t handle,
		    uint32_t read_domains, uint32_t write_domain);
void gem_sync(int fd, uint32_t handle);
uint32_t __gem_create(int fd, int size);
uint32_t gem_create(int fd, int size);
void gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *execbuf);

void *gem_mmap__gtt(int fd, uint32_t handle, int size, int prot);
void *gem_mmap__cpu(int fd, uint32_t handle, int offset, int size, int prot);

bool gem_mmap__has_wc(int fd);
void *gem_mmap__wc(int fd, uint32_t handle, int offset, int size, int prot);

/**
 * gem_require_mmap_wc:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether direct (i.e. cpu access path, bypassing
 * the gtt) write-combine memory mappings are available. Automatically skips
 * through igt_require() if not.
 */
#define gem_require_mmap_wc(fd) igt_require(gem_mmap__has_wc(fd))

/**
 * gem_mmap:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @size: size of the gem buffer
 * @prot: memory protection bits as used by mmap()
 *
 * This functions wraps up procedure to establish a memory mapping through the
 * GTT.
 *
 * This is a simple convenience alias to gem_mmap__gtt()
 *
 * Returns: A pointer to the created memory mapping.
 */
#define gem_mmap(fd, handle, size, prot) gem_mmap__gtt(fd, handle, size, prot)

int gem_madvise(int fd, uint32_t handle, int state);

uint32_t gem_context_create(int fd);
void gem_context_destroy(int fd, uint32_t ctx_id);
int __gem_context_destroy(int fd, uint32_t ctx_id);
struct local_i915_gem_context_param {
	uint32_t context;
	uint32_t size;
	uint64_t param;
#define LOCAL_CONTEXT_PARAM_BAN_PERIOD 0x1
	uint64_t value;
};
void gem_context_require_param(int fd, uint64_t param);
void gem_context_get_param(int fd, struct local_i915_gem_context_param *p);
void gem_context_set_param(int fd, struct local_i915_gem_context_param *p);

void gem_sw_finish(int fd, uint32_t handle);

bool gem_bo_busy(int fd, uint32_t handle);

/* feature test helpers */
bool gem_has_llc(int fd);
int gem_get_num_rings(int fd);
bool gem_has_enable_ring(int fd,int param);
bool gem_has_bsd(int fd);
bool gem_has_blt(int fd);
bool gem_has_vebox(int fd);
bool gem_has_bsd2(int fd);
bool gem_uses_aliasing_ppgtt(int fd);
int gem_available_fences(int fd);
uint64_t gem_available_aperture_size(int fd);
uint64_t gem_aperture_size(int fd);
uint64_t gem_mappable_aperture_size(void);

/* check functions which auto-skip tests by calling igt_skip() */
void gem_require_caching(int fd);
void gem_require_ring(int fd, int ring_id);

/* prime */
int prime_handle_to_fd(int fd, uint32_t handle);
uint32_t prime_fd_to_handle(int fd, int dma_buf_fd);
off_t prime_get_size(int dma_buf_fd);

/* addfb2 fb modifiers */
struct local_drm_mode_fb_cmd2 {
	uint32_t fb_id;
	uint32_t width, height;
	uint32_t pixel_format;
	uint32_t flags;
	uint32_t handles[4];
	uint32_t pitches[4];
	uint32_t offsets[4];
	uint64_t modifier[4];
};

#define LOCAL_DRM_MODE_FB_MODIFIERS	(1<<1)

#define LOCAL_DRM_FORMAT_MOD_VENDOR_INTEL	0x01

#define local_fourcc_mod_code(vendor, val) \
		((((uint64_t)LOCAL_DRM_FORMAT_MOD_VENDOR_## vendor) << 56) | \
		(val & 0x00ffffffffffffffL))

#define LOCAL_DRM_FORMAT_MOD_NONE	(0)
#define LOCAL_I915_FORMAT_MOD_X_TILED	local_fourcc_mod_code(INTEL, 1)
#define LOCAL_I915_FORMAT_MOD_Y_TILED	local_fourcc_mod_code(INTEL, 2)
#define LOCAL_I915_FORMAT_MOD_Yf_TILED	local_fourcc_mod_code(INTEL, 3)

#define LOCAL_DRM_IOCTL_MODE_ADDFB2	DRM_IOWR(0xB8, \
						 struct local_drm_mode_fb_cmd2)

#define LOCAL_DRM_CAP_ADDFB2_MODIFIERS	0x10

void igt_require_fb_modifiers(int fd);

/**
 * __kms_addfb:
 *
 * Creates a framebuffer object.
 */
int __kms_addfb(int fd, uint32_t handle, uint32_t width, uint32_t height,
		uint32_t stride, uint32_t pixel_format, uint64_t modifier,
		uint32_t flags, uint32_t *buf_id);

#endif /* IOCTL_WRAPPERS_H */
