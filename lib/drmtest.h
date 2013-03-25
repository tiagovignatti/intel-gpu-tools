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

drm_intel_bo * gem_handle_to_libdrm_bo(drm_intel_bufmgr *bufmgr, int fd,
				       const char *name, uint32_t handle);

int drm_get_card(int master);
int drm_open_any(void);
int drm_open_any_master(void);

void gem_quiescent_gpu(int fd);

/* ioctl wrappers and similar stuff for bare metal testing */
void gem_set_tiling(int fd, uint32_t handle, int tiling, int stride);
bool gem_has_enable_ring(int fd,int param);
bool gem_has_bsd(int fd);
bool gem_has_blt(int fd);
bool gem_has_vebox(int fd);
int gem_get_num_rings(int fd);
int gem_has_cacheing(int fd);
void gem_set_cacheing(int fd, uint32_t handle, int cacheing);
int gem_get_cacheing(int fd, uint32_t handle);
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
void drmtest_subtest_init(int argc, char **argv);
bool drmtest_run_subtest(const char *subtest_name);
bool drmtest_only_list_subtests(void);

bool drmtest_run_in_simulation(void);
#define SLOW_QUICK(slow,quick) (drmtest_run_in_simulation() ? (quick) : (slow))

/* helpers based upon the libdrm buffer manager */
void drmtest_init_aperture_trashers(drm_intel_bufmgr *bufmgr);
void drmtest_trash_aperture(void);
void drmtest_cleanup_aperture_trashers(void);

struct kmstest_connector_config {
	drmModeCrtc *crtc;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
	drmModeModeInfo default_mode;
	int crtc_idx;
	int pipe;
};

int kmstest_get_connector_config(int drm_fd, uint32_t connector_id,
				 unsigned long crtc_idx_mask,
				 struct kmstest_connector_config *config);
void kmstest_free_connector_config(struct kmstest_connector_config *config);

/* helpers to create nice-looking framebuffers */
struct kmstest_fb {
	uint32_t fb_id;
	uint32_t gem_handle;
	uint32_t drm_format;
	int width;
	int height;
	int depth;
	unsigned stride;
	unsigned size;
	cairo_t *cairo_ctx;
};

enum kmstest_text_align {
	align_left,
	align_bottom	= align_left,
	align_right	= 0x01,
	align_top	= 0x02,
	align_vcenter	= 0x04,
	align_hcenter	= 0x08,
};

int kmstest_cairo_printf_line(cairo_t *cr, enum kmstest_text_align align,
			       double yspacing, const char *fmt, ...)
			       __attribute__((format (printf, 4, 5)));

unsigned int kmstest_create_fb(int fd, int width, int height, int bpp,
			       int depth, bool tiled,
			       struct kmstest_fb *fb_info);
unsigned int kmstest_create_fb2(int fd, int width, int height, uint32_t format,
			        bool tiled, struct kmstest_fb *fb);
void kmstest_remove_fb(int fd, struct kmstest_fb *fb_info);
cairo_t *kmstest_get_cairo_ctx(int fd, struct kmstest_fb *fb);
void kmstest_paint_color_gradient(cairo_t *cr, int x, int y, int w, int h,
				  int r, int g, int b);
void kmstest_paint_test_pattern(cairo_t *cr, int width, int height);
void kmstest_dump_mode(drmModeModeInfo *mode);
int kmstest_get_pipe_from_crtc_id(int fd, int crtc_id);
const char *kmstest_format_str(uint32_t drm_format);
const char *kmstest_pipe_str(int pipe);
void kmstest_get_all_formats(const uint32_t **formats, int *format_count);
const char *kmstest_encoder_type_str(int type);
const char *kmstest_connector_status_str(int type);
const char *kmstest_connector_type_str(int type);

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

typedef void (*drmtest_exit_handler_t)(int sig);

int drmtest_install_exit_handler(drmtest_exit_handler_t fn);
void drmtest_enable_exit_handler(void);
void drmtest_disable_exit_handler(void);

int drmtest_set_vt_graphics_mode(void);
