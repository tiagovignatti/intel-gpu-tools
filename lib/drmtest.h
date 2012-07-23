/*
 * Copyright © 2007 Intel Corporation
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
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <cairo.h>

#include "xf86drm.h"
#include "xf86drmMode.h"
#include "intel_batchbuffer.h"

int drm_get_card(int master);
int drm_open_any(void);
int drm_open_any_master(void);

void gem_quiescent_gpu(int fd);

/* ioctl wrappers and similar stuff for bare metal testing */
void gem_set_tiling(int fd, uint32_t handle, int tiling, int stride);
void gem_close(int fd, uint32_t handle);
void gem_write(int fd, uint32_t handle, uint32_t offset,  const void *buf, uint32_t size);
void gem_read(int fd, uint32_t handle, uint32_t offset, void *buf, uint32_t size);
void gem_set_domain(int fd, uint32_t handle,
		    uint32_t read_domains, uint32_t write_domain);
void gem_sync(int fd, uint32_t handle);
uint32_t gem_create(int fd, int size);

void *gem_mmap__gtt(int fd, uint32_t handle, int size, int prot);
void *gem_mmap__cpu(int fd, uint32_t handle, int size, int prot);
#define gem_mmap gem_mmap__gtt

uint64_t gem_aperture_size(int fd);
uint64_t gem_mappable_aperture_size(void);
int gem_madvise(int fd, uint32_t handle, int state);

/* feature test helpers */
bool gem_uses_aliasing_ppgtt(int fd);
int gem_available_fences(int fd);

/* prime */
int prime_handle_to_fd(int fd, uint32_t handle);
uint32_t prime_fd_to_handle(int fd, int dma_buf_fd);

/* generally useful helpers */
void drmtest_fork_signal_helper(void);
void drmtest_stop_signal_helper(void);
void drmtest_exchange_int(void *array, unsigned i, unsigned j);
void drmtest_permute_array(void *array, unsigned size,
			   void (*exchange_func)(void *array,
						 unsigned i,
						 unsigned j));
void drmtest_progress(const char *header, uint64_t i, uint64_t total);

/* helpers based upon the libdrm buffer manager */
void drmtest_init_aperture_trashers(drm_intel_bufmgr *bufmgr);
void drmtest_trash_aperture(void);
void drmtest_cleanup_aperture_trashers(void);

/* helpers to create nice-looking framebuffers */
struct kmstest_fb {
	uint32_t fb_id;
	uint32_t gem_handle;
	unsigned stride;
	unsigned size;
};

typedef void (*kmstest_paint_func)(cairo_t *cr, int width, int height, void *priv);

unsigned int kmstest_create_fb(int fd, int width, int height, int bpp,
			       int depth, bool tiled,
			       struct kmstest_fb *fb_info,
			       kmstest_paint_func paint_func,
			       void *func_arg);
void kmstest_dump_mode(drmModeModeInfo *mode);

inline static void _do_or_die(const char *function, int line, int ret)
{
	if (ret == 0)
		return;

	fprintf(stderr, "%s:%d failed, ret=%d, errno=%d\n",
		function, line, ret, errno);
	abort();
}
#define do_or_die(x) _do_or_die(__FUNCTION__, __LINE__, x)
#define do_ioctl(fd, ptr, sz) do_or_die(drmIoctl((fd), (ptr), (sz)))
