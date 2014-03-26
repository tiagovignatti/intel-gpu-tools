/*
 * Copyright © 2013,2014 Intel Corporation
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
 * 	Daniel Vetter <daniel.vetter@ffwll.ch>
 * 	Damien Lespiau <damien.lespiau@intel.com>
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <math.h>

#include "drmtest.h"
#include "igt_fb.h"
#include "ioctl_wrappers.h"

/* drm fourcc/cairo format maps */
#define DF(did, cid, _bpp, _depth)	\
	{ DRM_FORMAT_##did, CAIRO_FORMAT_##cid, # did, _bpp, _depth }
static struct format_desc_struct {
	uint32_t drm_id;
	cairo_format_t cairo_id;
	const char *name;
	int bpp;
	int depth;
} format_desc[] = {
	DF(RGB565,	RGB16_565,	16, 16),
	DF(RGB888,	INVALID,	24, 24),
	DF(XRGB8888,	RGB24,		32, 24),
	DF(XRGB2101010,	RGB30,		32, 30),
	DF(ARGB8888,	ARGB32,		32, 32),
};
#undef DF

#define for_each_format(f)	\
	for (f = format_desc; f - format_desc < ARRAY_SIZE(format_desc); f++)


/* helpers to create nice-looking framebuffers */
static int create_bo_for_fb(int fd, int width, int height, int bpp,
			    bool tiled, uint32_t *gem_handle_ret,
			    unsigned *size_ret, unsigned *stride_ret)
{
	uint32_t gem_handle;
	int size;
	unsigned stride;

	if (tiled) {
		int v;

		/* Round the tiling up to the next power-of-two and the
		 * region up to the next pot fence size so that this works
		 * on all generations.
		 *
		 * This can still fail if the framebuffer is too large to
		 * be tiled. But then that failure is expected.
		 */

		v = width * bpp / 8;
		for (stride = 512; stride < v; stride *= 2)
			;

		v = stride * height;
		for (size = 1024*1024; size < v; size *= 2)
			;
	} else {
		/* Scan-out has a 64 byte alignment restriction */
		stride = (width * (bpp / 8) + 63) & ~63;
		size = stride * height;
	}

	gem_handle = gem_create(fd, size);

	if (tiled)
		gem_set_tiling(fd, gem_handle, I915_TILING_X, stride);

	*stride_ret = stride;
	*size_ret = size;
	*gem_handle_ret = gem_handle;

	return 0;
}

void kmstest_paint_color(cairo_t *cr, int x, int y, int w, int h,
			 double r, double g, double b)
{
	cairo_rectangle(cr, x, y, w, h);
	cairo_set_source_rgb(cr, r, g, b);
	cairo_fill(cr);
}

void kmstest_paint_color_alpha(cairo_t *cr, int x, int y, int w, int h,
			       double r, double g, double b, double a)
{
	cairo_rectangle(cr, x, y, w, h);
	cairo_set_source_rgba(cr, r, g, b, a);
	cairo_fill(cr);
}

void
kmstest_paint_color_gradient(cairo_t *cr, int x, int y, int w, int h,
		     int r, int g, int b)
{
	cairo_pattern_t *pat;

	pat = cairo_pattern_create_linear(x, y, x + w, y + h);
	cairo_pattern_add_color_stop_rgba(pat, 1, 0, 0, 0, 1);
	cairo_pattern_add_color_stop_rgba(pat, 0, r, g, b, 1);

	cairo_rectangle(cr, x, y, w, h);
	cairo_set_source(cr, pat);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);
}

static void
paint_test_patterns(cairo_t *cr, int width, int height)
{
	double gr_height, gr_width;
	int x, y;

	y = height * 0.10;
	gr_width = width * 0.75;
	gr_height = height * 0.08;
	x = (width / 2) - (gr_width / 2);

	kmstest_paint_color_gradient(cr, x, y, gr_width, gr_height, 1, 0, 0);

	y += gr_height;
	kmstest_paint_color_gradient(cr, x, y, gr_width, gr_height, 0, 1, 0);

	y += gr_height;
	kmstest_paint_color_gradient(cr, x, y, gr_width, gr_height, 0, 0, 1);

	y += gr_height;
	kmstest_paint_color_gradient(cr, x, y, gr_width, gr_height, 1, 1, 1);
}

int kmstest_cairo_printf_line(cairo_t *cr, enum kmstest_text_align align,
				double yspacing, const char *fmt, ...)
{
	double x, y, xofs, yofs;
	cairo_text_extents_t extents;
	char *text;
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vasprintf(&text, fmt, ap);
	igt_assert(ret >= 0);
	va_end(ap);

	cairo_text_extents(cr, text, &extents);

	xofs = yofs = 0;
	if (align & align_right)
		xofs = -extents.width;
	else if (align & align_hcenter)
		xofs = -extents.width / 2;

	if (align & align_top)
		yofs = extents.height;
	else if (align & align_vcenter)
		yofs = extents.height / 2;

	cairo_get_current_point(cr, &x, &y);
	if (xofs || yofs)
		cairo_rel_move_to(cr, xofs, yofs);

	cairo_text_path(cr, text);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_fill(cr);

	cairo_move_to(cr, x, y + extents.height + yspacing);

	free(text);

	return extents.width;
}

static void
paint_marker(cairo_t *cr, int x, int y)
{
	enum kmstest_text_align align;
	int xoff, yoff;

	cairo_move_to(cr, x, y - 20);
	cairo_line_to(cr, x, y + 20);
	cairo_move_to(cr, x - 20, y);
	cairo_line_to(cr, x + 20, y);
	cairo_new_sub_path(cr);
	cairo_arc(cr, x, y, 10, 0, M_PI * 2);
	cairo_set_line_width(cr, 4);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);

	xoff = x ? -20 : 20;
	align = x ? align_right : align_left;

	yoff = y ? -20 : 20;
	align |= y ? align_bottom : align_top;

	cairo_move_to(cr, x + xoff, y + yoff);
	cairo_set_font_size(cr, 18);
	kmstest_cairo_printf_line(cr, align, 0, "(%d, %d)", x, y);
}

void kmstest_paint_test_pattern(cairo_t *cr, int width, int height)
{
	paint_test_patterns(cr, width, height);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	/* Paint corner markers */
	paint_marker(cr, 0, 0);
	paint_marker(cr, width, 0);
	paint_marker(cr, 0, height);
	paint_marker(cr, width, height);

	igt_assert(!cairo_status(cr));
}

void kmstest_paint_image(cairo_t *cr, const char *filename,
			 int dst_x, int dst_y, int dst_width, int dst_height)
{
	cairo_surface_t *image;
	int img_width, img_height;
	double scale_x, scale_y;

	image = cairo_image_surface_create_from_png(filename);
	igt_assert(cairo_surface_status(image) == CAIRO_STATUS_SUCCESS);

	img_width = cairo_image_surface_get_width(image);
	img_height = cairo_image_surface_get_height(image);

	scale_x = (double)dst_width / img_width;
	scale_y = (double)dst_height / img_height;

	cairo_save(cr);

	cairo_translate(cr, dst_x, dst_y);
	cairo_scale(cr, scale_x, scale_y);
	cairo_set_source_surface(cr, image, 0, 0);
	cairo_paint(cr);

	cairo_surface_destroy(image);

	cairo_restore(cr);
}

unsigned int kmstest_create_fb(int fd, int width, int height, uint32_t format,
			        bool tiled, struct kmstest_fb *fb)
{
	uint32_t handles[4];
	uint32_t pitches[4];
	uint32_t offsets[4];
	uint32_t fb_id;
	int bpp;
	int ret;

	memset(fb, 0, sizeof(*fb));

	bpp = drm_format_to_bpp(format);
	ret = create_bo_for_fb(fd, width, height, bpp, tiled, &fb->gem_handle,
			      &fb->size, &fb->stride);
	if (ret < 0)
		return ret;

	memset(handles, 0, sizeof(handles));
	handles[0] = fb->gem_handle;
	memset(pitches, 0, sizeof(pitches));
	pitches[0] = fb->stride;
	memset(offsets, 0, sizeof(offsets));
	if (drmModeAddFB2(fd, width, height, format, handles, pitches,
			  offsets, &fb_id, 0) < 0) {
		gem_close(fd, fb->gem_handle);

		return 0;
	}

	fb->width = width;
	fb->height = height;
	fb->tiling = tiled;
	fb->drm_format = format;
	fb->fb_id = fb_id;

	return fb_id;
}

unsigned int kmstest_create_color_fb(int fd, int width, int height,
				     uint32_t format, bool tiled,
				     double r, double g, double b,
				     struct kmstest_fb *fb /* out */)
{
	unsigned int fb_id;
	cairo_t *cr;

	fb_id = kmstest_create_fb(fd, width, height, format, tiled, fb);
	igt_assert(fb_id);

	cr = kmstest_get_cairo_ctx(fd, fb);
	kmstest_paint_color(cr, 0, 0, width, height, r, g, b);
	igt_assert(cairo_status(cr) == 0);
	cairo_destroy(cr);

	return fb_id;
}

static cairo_format_t drm_format_to_cairo(uint32_t drm_format)
{
	struct format_desc_struct *f;

	for_each_format(f)
		if (f->drm_id == drm_format)
			return f->cairo_id;

	abort();
}

static void __kmstest_destroy_cairo_surface(void *arg)
{
	struct kmstest_fb *fb = arg;
	munmap(cairo_image_surface_get_data(fb->cairo_surface), fb->size);
}

static cairo_surface_t *kmstest_get_cairo_surface(int fd, struct kmstest_fb *fb)
{
	if (fb->cairo_surface == NULL) {
		fb->cairo_surface =
			cairo_image_surface_create_for_data(gem_mmap(fd, fb->gem_handle, fb->size, PROT_READ | PROT_WRITE),
							    drm_format_to_cairo(fb->drm_format),
							    fb->width, fb->height, fb->stride);

		cairo_surface_set_user_data(fb->cairo_surface,
					    (cairo_user_data_key_t *)kmstest_get_cairo_surface,
					    fb, __kmstest_destroy_cairo_surface);
	}

	gem_set_domain(fd, fb->gem_handle,
		       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);

	igt_assert(cairo_surface_status(fb->cairo_surface) == CAIRO_STATUS_SUCCESS);
	return cairo_surface_reference(fb->cairo_surface);
}

cairo_t *kmstest_get_cairo_ctx(int fd, struct kmstest_fb *fb)
{
	cairo_surface_t *surface;
	cairo_t *cr;

	surface = kmstest_get_cairo_surface(fd, fb);
	cr = cairo_create(surface);
	cairo_surface_destroy(surface);

	igt_assert(cairo_status(cr) == CAIRO_STATUS_SUCCESS);

	return cr;
}

void kmstest_write_fb(int fd, struct kmstest_fb *fb, const char *filename)
{
	cairo_surface_t *surface;
	cairo_status_t status;

	surface = kmstest_get_cairo_surface(fd, fb);
	status = cairo_surface_write_to_png(surface, filename);
	cairo_surface_destroy(surface);

	igt_assert(status == CAIRO_STATUS_SUCCESS);
}

void kmstest_remove_fb(int fd, struct kmstest_fb *fb)
{
	cairo_surface_destroy(fb->cairo_surface);
	do_or_die(drmModeRmFB(fd, fb->fb_id));
	gem_close(fd, fb->gem_handle);
}

/* helpers to handle drm fourcc codes */
uint32_t bpp_depth_to_drm_format(int bpp, int depth)
{
	struct format_desc_struct *f;

	for_each_format(f)
		if (f->bpp == bpp && f->depth == depth)
			return f->drm_id;

	abort();
}

/* Return fb_id on success, 0 on error */
uint32_t drm_format_to_bpp(uint32_t drm_format)
{
	struct format_desc_struct *f;

	for_each_format(f)
		if (f->drm_id == drm_format)
			return f->bpp;

	abort();
}

const char *kmstest_format_str(uint32_t drm_format)
{
	struct format_desc_struct *f;

	for_each_format(f)
		if (f->drm_id == drm_format)
			return f->name;

	return "invalid";
}

void kmstest_get_all_formats(const uint32_t **formats, int *format_count)
{
	static uint32_t *drm_formats;

	if (!drm_formats) {
		struct format_desc_struct *f;
		uint32_t *format;

		drm_formats = calloc(ARRAY_SIZE(format_desc),
				     sizeof(*drm_formats));
		format = &drm_formats[0];
		for_each_format(f)
			*format++ = f->drm_id;
	}

	*formats = drm_formats;
	*format_count = ARRAY_SIZE(format_desc);
}
