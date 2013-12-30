/*
 * Copyright © 2013 Intel Corporation
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
 */

#ifndef __IGT_KMS_H__
#define __IGT_KMS_H__

#include <cairo.h>

struct kmstest_connector_config {
	drmModeCrtc *crtc;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
	drmModeModeInfo default_mode;
	int crtc_idx;
	int pipe;
};

int kmstest_get_connector_default_mode(int drm_fd, drmModeConnector *connector,
				      drmModeModeInfo *mode);
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
	unsigned tiling;
	unsigned size;
	cairo_surface_t *cairo_surface;
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
cairo_surface_t *kmstest_get_cairo_surface(int fd, struct kmstest_fb *fb);
void kmstest_paint_color(cairo_t *cr, int x, int y, int w, int h,
			 double r, double g, double b);
void kmstest_paint_color_alpha(cairo_t *cr, int x, int y, int w, int h,
			       double r, double g, double b, double a);
void kmstest_paint_color_gradient(cairo_t *cr, int x, int y, int w, int h,
				  int r, int g, int b);
void kmstest_paint_test_pattern(cairo_t *cr, int width, int height);
void kmstest_paint_image(cairo_t *cr, const char *filename,
			 int dst_x, int dst_y, int dst_width, int dst_height);
void kmstest_write_fb(int fd, struct kmstest_fb *fb, const char *filename);
void kmstest_dump_mode(drmModeModeInfo *mode);
int kmstest_get_pipe_from_crtc_id(int fd, int crtc_id);
const char *kmstest_format_str(uint32_t drm_format);
const char *kmstest_pipe_str(int pipe);
void kmstest_get_all_formats(const uint32_t **formats, int *format_count);
const char *kmstest_encoder_type_str(int type);
const char *kmstest_connector_status_str(int type);
const char *kmstest_connector_type_str(int type);

uint32_t drm_format_to_bpp(uint32_t drm_format);

#endif /* __IGT_KMS_H__ */

