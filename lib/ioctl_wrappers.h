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
void *gem_mmap__cpu(int fd, uint32_t handle, int size, int prot);
/**
 * gem_mmap:
 *
 * This is a simple convenience alias to gem_mmap__gtt()
 */
#define gem_mmap gem_mmap__gtt

int gem_madvise(int fd, uint32_t handle, int state);

uint32_t gem_context_create(int fd);

void gem_sw_finish(int fd, uint32_t handle);

bool gem_bo_busy(int fd, uint32_t handle);

/* feature test helpers */
int gem_get_num_rings(int fd);
bool gem_has_enable_ring(int fd,int param);
bool gem_has_bsd(int fd);
bool gem_has_blt(int fd);
bool gem_has_vebox(int fd);
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

#endif /* IOCTL_WRAPPERS_H */
