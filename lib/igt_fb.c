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
#include "igt_kms.h"
#include "ioctl_wrappers.h"
#include "intel_chipset.h"

/**
 * SECTION:igt_fb
 * @short_description: Framebuffer handling and drawing library
 * @title: Framebuffer
 * @include: igt.h
 *
 * This library contains helper functions for handling kms framebuffer objects
 * using #igt_fb structures to track all the metadata.  igt_create_fb() creates
 * a basic framebuffer and igt_remove_fb() cleans everything up again.
 *
 * It also supports drawing using the cairo library and provides some simplified
 * helper functions to easily draw test patterns. The main function to create a
 * cairo drawing context for a framebuffer object is igt_get_cairo_ctx().
 *
 * Finally it also pulls in the drm fourcc headers and provides some helper
 * functions to work with these pixel format codes.
 */

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
	//DF(RGB888,	INVALID,	24, 24),
	DF(XRGB8888,	RGB24,		32, 24),
	DF(XRGB2101010,	RGB30,		32, 30),
	DF(ARGB8888,	ARGB32,		32, 32),
};
#undef DF

#define for_each_format(f)	\
	for (f = format_desc; f - format_desc < ARRAY_SIZE(format_desc); f++)

static void igt_get_fb_tile_size(int fd, uint64_t tiling, int fb_bpp,
				 unsigned *width_ret, unsigned *height_ret)
{
	switch (tiling) {
	case LOCAL_DRM_FORMAT_MOD_NONE:
		*width_ret = 64;
		*height_ret = 1;
		break;
	case LOCAL_I915_FORMAT_MOD_X_TILED:
		igt_require_intel(fd);
		if (intel_gen(intel_get_drm_devid(fd)) == 2) {
			*width_ret = 128;
			*height_ret = 16;
		} else {
			*width_ret = 512;
			*height_ret = 8;
		}
		break;
	case LOCAL_I915_FORMAT_MOD_Y_TILED:
		igt_require_intel(fd);
		if (intel_gen(intel_get_drm_devid(fd)) == 2) {
			*width_ret = 128;
			*height_ret = 16;
		} else if (IS_915(intel_get_drm_devid(fd))) {
			*width_ret = 512;
			*height_ret = 8;
		} else {
			*width_ret = 128;
			*height_ret = 32;
		}
		break;
	case LOCAL_I915_FORMAT_MOD_Yf_TILED:
		igt_require_intel(fd);
		switch (fb_bpp) {
		case 8:
			*width_ret = 64;
			*height_ret = 64;
			break;
		case 16:
		case 32:
			*width_ret = 128;
			*height_ret = 32;
			break;
		case 64:
		case 128:
			*width_ret = 256;
			*height_ret = 16;
			break;
		default:
			igt_assert(false);
		}
		break;
	default:
		igt_assert(false);
	}
}

/**
 * igt_calc_fb_size:
 * @fd: the DRM file descriptor
 * @width: width of the framebuffer in pixels
 * @height: height of the framebuffer in pixels
 * @bpp: bytes per pixel of the framebuffer
 * @tiling: tiling layout of the framebuffer (as framebuffer modifier)
 * @size_ret: returned size for the framebuffer
 * @stride_ret: returned stride for the framebuffer
 *
 * This function returns valid stride and size values for a framebuffer with the
 * specified parameters.
 */
void igt_calc_fb_size(int fd, int width, int height, int bpp, uint64_t tiling,
		      unsigned *size_ret, unsigned *stride_ret)
{
	unsigned int tile_width, tile_height, stride, size;
	int byte_width = width * (bpp / 8);

	igt_get_fb_tile_size(fd, tiling, bpp, &tile_width, &tile_height);

	if (tiling != LOCAL_DRM_FORMAT_MOD_NONE &&
	    intel_gen(intel_get_drm_devid(fd)) <= 3) {
		int v;

		/* Round the tiling up to the next power-of-two and the region
		 * up to the next pot fence size so that this works on all
		 * generations.
		 *
		 * This can still fail if the framebuffer is too large to be
		 * tiled. But then that failure is expected.
		 */

		v = width * bpp / 8;
		for (stride = 512; stride < v; stride *= 2)
			;

		v = stride * height;
		for (size = 1024*1024; size < v; size *= 2)
			;
	} else {
		stride = ALIGN(byte_width, tile_width);
		size = stride * ALIGN(height, tile_height);
	}

	*stride_ret = stride;
	*size_ret = size;
}

/**
 * igt_create_bo_with_dimensions:
 * @fd: open drm file descriptor
 * @width: width of the buffer object in pixels
 * @height: height of the buffer object in pixels
 * @format: drm fourcc pixel format code
 * @modifier: modifier corresponding to the tiling layout of the buffer object
 * @stride: stride of the buffer object in bytes (0 for automatic stride)
 * @size_ret: size of the buffer object as created by the kernel
 * @stride_ret: stride of the buffer object as created by the kernel
 * @is_dumb: whether the created buffer object is a dumb buffer or not
 *
 * This function allocates a gem buffer object matching the requested
 * properties.
 *
 * Returns:
 * The kms id of the created buffer object.
 */
int igt_create_bo_with_dimensions(int fd, int width, int height,
				  uint32_t format, uint64_t modifier,
				  unsigned stride, unsigned *size_ret,
				  unsigned *stride_ret, bool *is_dumb)
{
	int bpp = igt_drm_format_to_bpp(format);
	int bo;

	if (modifier || stride) {
		unsigned size, calculated_stride;

		igt_calc_fb_size(fd, width, height, bpp, modifier, &size,
				 &calculated_stride);
		if (stride == 0)
			stride = calculated_stride;

		if (is_dumb)
			*is_dumb = false;

		if (is_i915_device(fd)) {

			bo = gem_create(fd, size);
			gem_set_tiling(fd, bo, modifier, stride);

			if (size_ret)
				*size_ret = size;

			if (stride_ret)
				*stride_ret = stride;

			return bo;
		} else {
			bool driver_has_tiling_support = false;

			igt_require(driver_has_tiling_support);
			return -EINVAL;
		}
	} else {
		if (is_dumb)
			*is_dumb = true;

		return kmstest_dumb_create(fd, width, height, bpp, stride_ret,
					   size_ret);
	}
}

/* helpers to create nice-looking framebuffers */
static int create_bo_for_fb(int fd, int width, int height, uint32_t format,
			    uint64_t tiling, unsigned bo_size,
			    unsigned bo_stride, uint32_t *gem_handle_ret,
			    unsigned *size_ret, unsigned *stride_ret,
			    bool *is_dumb)
{
	uint32_t gem_handle;
	int ret = 0;

	gem_handle = igt_create_bo_with_dimensions(fd, width, height, format,
						   tiling, bo_stride, size_ret,
						   stride_ret, is_dumb);

	*gem_handle_ret = gem_handle;

	return ret;
}

/**
 * igt_paint_color:
 * @cr: cairo drawing context
 * @x: pixel x-coordination of the fill rectangle
 * @y: pixel y-coordination of the fill rectangle
 * @w: width of the fill rectangle
 * @h: height of the fill rectangle
 * @r: red value to use as fill color
 * @g: green value to use as fill color
 * @b: blue value to use as fill color
 *
 * This functions draws a solid rectangle with the given color using the drawing
 * context @cr.
 */
void igt_paint_color(cairo_t *cr, int x, int y, int w, int h,
		     double r, double g, double b)
{
	cairo_rectangle(cr, x, y, w, h);
	cairo_set_source_rgb(cr, r, g, b);
	cairo_fill(cr);
}

/**
 * igt_paint_color_alpha:
 * @cr: cairo drawing context
 * @x: pixel x-coordination of the fill rectangle
 * @y: pixel y-coordination of the fill rectangle
 * @w: width of the fill rectangle
 * @h: height of the fill rectangle
 * @r: red value to use as fill color
 * @g: green value to use as fill color
 * @b: blue value to use as fill color
 * @a: alpha value to use as fill color
 *
 * This functions draws a rectangle with the given color and alpha values using
 * the drawing context @cr.
 */
void igt_paint_color_alpha(cairo_t *cr, int x, int y, int w, int h,
			   double r, double g, double b, double a)
{
	cairo_rectangle(cr, x, y, w, h);
	cairo_set_source_rgba(cr, r, g, b, a);
	cairo_fill(cr);
}

/**
 * igt_paint_color_gradient:
 * @cr: cairo drawing context
 * @x: pixel x-coordination of the fill rectangle
 * @y: pixel y-coordination of the fill rectangle
 * @w: width of the fill rectangle
 * @h: height of the fill rectangle
 * @r: red value to use as fill color
 * @g: green value to use as fill color
 * @b: blue value to use as fill color
 *
 * This functions draws a gradient into the rectangle which fades in from black
 * to the given values using the drawing context @cr.
 */
void
igt_paint_color_gradient(cairo_t *cr, int x, int y, int w, int h,
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

/**
 * igt_paint_color_gradient_range:
 * @cr: cairo drawing context
 * @x: pixel x-coordination of the fill rectangle
 * @y: pixel y-coordination of the fill rectangle
 * @w: width of the fill rectangle
 * @h: height of the fill rectangle
 * @sr: red value to use as start gradient color
 * @sg: green value to use as start gradient color
 * @sb: blue value to use as start gradient color
 * @er: red value to use as end gradient color
 * @eg: green value to use as end gradient color
 * @eb: blue value to use as end gradient color
 *
 * This functions draws a gradient into the rectangle which fades in
 * from one color to the other using the drawing context @cr.
 */
void
igt_paint_color_gradient_range(cairo_t *cr, int x, int y, int w, int h,
			       double sr, double sg, double sb,
			       double er, double eg, double eb)
{
	cairo_pattern_t *pat;

	pat = cairo_pattern_create_linear(x, y, x + w, y + h);
	cairo_pattern_add_color_stop_rgba(pat, 1, sr, sg, sb, 1);
	cairo_pattern_add_color_stop_rgba(pat, 0, er, eg, eb, 1);

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

	igt_paint_color_gradient(cr, x, y, gr_width, gr_height, 1, 0, 0);

	y += gr_height;
	igt_paint_color_gradient(cr, x, y, gr_width, gr_height, 0, 1, 0);

	y += gr_height;
	igt_paint_color_gradient(cr, x, y, gr_width, gr_height, 0, 0, 1);

	y += gr_height;
	igt_paint_color_gradient(cr, x, y, gr_width, gr_height, 1, 1, 1);
}

/**
 * igt_cairo_printf_line:
 * @cr: cairo drawing context
 * @align: text alignment
 * @yspacing: additional y-direction feed after this line
 * @fmt: format string
 * @...: optional arguments used in the format string
 *
 * This is a little helper to draw text onto framebuffers. All the initial setup
 * (like setting the font size and the moving to the starting position) still
 * needs to be done manually with explicit cairo calls on @cr.
 *
 * Returns:
 * The width of the drawn text.
 */
int igt_cairo_printf_line(cairo_t *cr, enum igt_text_align align,
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
	enum igt_text_align align;
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
	igt_cairo_printf_line(cr, align, 0, "(%d, %d)", x, y);
}

/**
 * igt_paint_test_pattern:
 * @cr: cairo drawing context
 * @width: width of the visible area
 * @height: height of the visible area
 *
 * This functions draws an entire set of test patterns for the given visible
 * area using the drawing context @cr. This is useful for manual visual
 * inspection of displayed framebuffers.
 *
 * The test patterns include
 *  - corner markers to check for over/underscan and
 *  - a set of color and b/w gradients.
 */
void igt_paint_test_pattern(cairo_t *cr, int width, int height)
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

static cairo_status_t
stdio_read_func(void *closure, unsigned char* data, unsigned int size)
{
	if (fread(data, 1, size, (FILE*)closure) != size)
		return CAIRO_STATUS_READ_ERROR;

	return CAIRO_STATUS_SUCCESS;
}

/**
 * igt_paint_image:
 * @cr: cairo drawing context
 * @filename: filename of the png image to draw
 * @dst_x: pixel x-coordination of the destination rectangle
 * @dst_y: pixel y-coordination of the destination rectangle
 * @dst_width: width of the destination rectangle
 * @dst_height: height of the destination rectangle
 *
 * This function can be used to draw a scaled version of the supplied png image,
 * which is loaded from the package data directory.
 */
void igt_paint_image(cairo_t *cr, const char *filename,
		     int dst_x, int dst_y, int dst_width, int dst_height)
{
	cairo_surface_t *image;
	int img_width, img_height;
	double scale_x, scale_y;
	FILE* f;

	f = igt_fopen_data(filename);

	image = cairo_image_surface_create_from_png_stream(&stdio_read_func, f);
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

	fclose(f);
}

/**
 * igt_create_fb_with_bo_size:
 * @fd: open i915 drm file descriptor
 * @width: width of the framebuffer in pixel
 * @height: height of the framebuffer in pixel
 * @format: drm fourcc pixel format code
 * @tiling: tiling layout of the framebuffer (as framebuffer modifier)
 * @fb: pointer to an #igt_fb structure
 * @bo_size: size of the backing bo (0 for automatic size)
 * @bo_stride: stride of the backing bo (0 for automatic stride)
 *
 * This function allocates a gem buffer object suitable to back a framebuffer
 * with the requested properties and then wraps it up in a drm framebuffer
 * object of the requested size. All metadata is stored in @fb.
 *
 * The backing storage of the framebuffer is filled with all zeros, i.e. black
 * for rgb pixel formats.
 *
 * Returns:
 * The kms id of the created framebuffer.
 */
unsigned int
igt_create_fb_with_bo_size(int fd, int width, int height,
			   uint32_t format, uint64_t tiling,
			   struct igt_fb *fb, unsigned bo_size,
			   unsigned bo_stride)
{
	uint32_t fb_id;

	memset(fb, 0, sizeof(*fb));

	igt_debug("%s(width=%d, height=%d, format=0x%x, tiling=0x%"PRIx64", size=%d)\n",
		  __func__, width, height, format, tiling, bo_size);
	do_or_die(create_bo_for_fb(fd, width, height, format, tiling, bo_size,
				   bo_stride, &fb->gem_handle, &fb->size,
				   &fb->stride, &fb->is_dumb));

	igt_debug("%s(handle=%d, pitch=%d)\n",
		  __func__, fb->gem_handle, fb->stride);

	if (tiling != LOCAL_DRM_FORMAT_MOD_NONE &&
	    tiling != LOCAL_I915_FORMAT_MOD_X_TILED) {
		do_or_die(__kms_addfb(fd, fb->gem_handle, width, height,
				      fb->stride, format, tiling,
				      LOCAL_DRM_MODE_FB_MODIFIERS, &fb_id));
	} else {
		uint32_t handles[4];
		uint32_t pitches[4];
		uint32_t offsets[4];

		memset(handles, 0, sizeof(handles));
		memset(pitches, 0, sizeof(pitches));
		memset(offsets, 0, sizeof(offsets));

		handles[0] = fb->gem_handle;
		pitches[0] = fb->stride;

		do_or_die(drmModeAddFB2(fd, width, height, format,
					handles, pitches, offsets,
					&fb_id, 0));
	}

	fb->width = width;
	fb->height = height;
	fb->tiling = tiling;
	fb->drm_format = format;
	fb->fb_id = fb_id;
	fb->fd = fd;

	return fb_id;
}

/**
 * igt_create_fb:
 * @fd: open i915 drm file descriptor
 * @width: width of the framebuffer in pixel
 * @height: height of the framebuffer in pixel
 * @format: drm fourcc pixel format code
 * @tiling: tiling layout of the framebuffer
 * @fb: pointer to an #igt_fb structure
 *
 * This function allocates a gem buffer object suitable to back a framebuffer
 * with the requested properties and then wraps it up in a drm framebuffer
 * object. All metadata is stored in @fb.
 *
 * The backing storage of the framebuffer is filled with all zeros, i.e. black
 * for rgb pixel formats.
 *
 * Returns:
 * The kms id of the created framebuffer.
 */
unsigned int igt_create_fb(int fd, int width, int height, uint32_t format,
			   uint64_t tiling, struct igt_fb *fb)
{
	return igt_create_fb_with_bo_size(fd, width, height, format, tiling, fb,
					  0, 0);
}

/**
 * igt_create_color_fb:
 * @fd: open i915 drm file descriptor
 * @width: width of the framebuffer in pixel
 * @height: height of the framebuffer in pixel
 * @format: drm fourcc pixel format code
 * @tiling: tiling layout of the framebuffer
 * @r: red value to use as fill color
 * @g: green value to use as fill color
 * @b: blue value to use as fill color
 * @fb: pointer to an #igt_fb structure
 *
 * This function allocates a gem buffer object suitable to back a framebuffer
 * with the requested properties and then wraps it up in a drm framebuffer
 * object. All metadata is stored in @fb.
 *
 * Compared to igt_create_fb() this function also fills the entire framebuffer
 * with the given color, which is useful for some simple pipe crc based tests.
 *
 * Returns:
 * The kms id of the created framebuffer on success or a negative error code on
 * failure.
 */
unsigned int igt_create_color_fb(int fd, int width, int height,
				 uint32_t format, uint64_t tiling,
				 double r, double g, double b,
				 struct igt_fb *fb /* out */)
{
	unsigned int fb_id;
	cairo_t *cr;

	fb_id = igt_create_fb(fd, width, height, format, tiling, fb);
	igt_assert(fb_id);

	cr = igt_get_cairo_ctx(fd, fb);
	igt_paint_color(cr, 0, 0, width, height, r, g, b);
	igt_assert(cairo_status(cr) == 0);
	cairo_destroy(cr);

	return fb_id;
}

/**
 * igt_create_pattern_fb:
 * @fd: open i915 drm file descriptor
 * @width: width of the framebuffer in pixel
 * @height: height of the framebuffer in pixel
 * @format: drm fourcc pixel format code
 * @tiling: tiling layout of the framebuffer
 * @fb: pointer to an #igt_fb structure
 *
 * This function allocates a gem buffer object suitable to back a framebuffer
 * with the requested properties and then wraps it up in a drm framebuffer
 * object. All metadata is stored in @fb.
 *
 * Compared to igt_create_fb() this function also draws the standard test pattern
 * into the framebuffer.
 *
 * Returns:
 * The kms id of the created framebuffer on success or a negative error code on
 * failure.
 */
unsigned int igt_create_pattern_fb(int fd, int width, int height,
				   uint32_t format, uint64_t tiling,
				   struct igt_fb *fb /* out */)
{
	unsigned int fb_id;
	cairo_t *cr;

	fb_id = igt_create_fb(fd, width, height, format, tiling, fb);
	igt_assert(fb_id);

	cr = igt_get_cairo_ctx(fd, fb);
	igt_paint_test_pattern(cr, width, height);
	igt_assert(cairo_status(cr) == 0);
	cairo_destroy(cr);

	return fb_id;
}

/**
 * igt_create_color_pattern_fb:
 * @fd: open i915 drm file descriptor
 * @width: width of the framebuffer in pixel
 * @height: height of the framebuffer in pixel
 * @format: drm fourcc pixel format code
 * @tiling: tiling layout of the framebuffer
 * @r: red value to use as fill color
 * @g: green value to use as fill color
 * @b: blue value to use as fill color
 * @fb: pointer to an #igt_fb structure
 *
 * This function allocates a gem buffer object suitable to back a framebuffer
 * with the requested properties and then wraps it up in a drm framebuffer
 * object. All metadata is stored in @fb.
 *
 * Compared to igt_create_fb() this function also fills the entire framebuffer
 * with the given color, and then draws the standard test pattern into the
 * framebuffer.
 *
 * Returns:
 * The kms id of the created framebuffer on success or a negative error code on
 * failure.
 */
unsigned int igt_create_color_pattern_fb(int fd, int width, int height,
					 uint32_t format, uint64_t tiling,
					 double r, double g, double b,
					 struct igt_fb *fb /* out */)
{
	unsigned int fb_id;
	cairo_t *cr;

	fb_id = igt_create_fb(fd, width, height, format, tiling, fb);
	igt_assert(fb_id);

	cr = igt_get_cairo_ctx(fd, fb);
	igt_paint_color(cr, 0, 0, width, height, r, g, b);
	igt_paint_test_pattern(cr, width, height);
	igt_assert(cairo_status(cr) == 0);
	cairo_destroy(cr);

	return fb_id;
}

/**
 * igt_create_image_fb:
 * @drm_fd: open i915 drm file descriptor
 * @width: width of the framebuffer in pixel or 0
 * @height: height of the framebuffer in pixel or 0
 * @format: drm fourcc pixel format code
 * @tiling: tiling layout of the framebuffer
 * @filename: filename of the png image to draw
 * @fb: pointer to an #igt_fb structure
 *
 * Create a framebuffer with the specified image. If @width is zero the
 * image width will be used. If @height is zero the image height will be used.
 *
 * Returns:
 * The kms id of the created framebuffer on success or a negative error code on
 * failure.
 */
unsigned int igt_create_image_fb(int fd, int width, int height,
				 uint32_t format, uint64_t tiling,
				 const char *filename,
				 struct igt_fb *fb /* out */)
{
	cairo_surface_t *image;
	uint32_t fb_id;
	cairo_t *cr;

	image = cairo_image_surface_create_from_png(filename);
	igt_assert(cairo_surface_status(image) == CAIRO_STATUS_SUCCESS);
	if (width == 0)
		width = cairo_image_surface_get_width(image);
	if (height == 0)
		height = cairo_image_surface_get_height(image);
	cairo_surface_destroy(image);

	fb_id = igt_create_fb(fd, width, height, format, tiling, fb);

	cr = igt_get_cairo_ctx(fd, fb);
	igt_paint_image(cr, filename, 0, 0, width, height);
	igt_assert(cairo_status(cr) == 0);
	cairo_destroy(cr);

	return fb_id;
}

struct box {
	int x, y, width, height;
};

struct stereo_fb_layout {
	int fb_width, fb_height;
	struct box left, right;
};

static void box_init(struct box *box, int x, int y, int bwidth, int bheight)
{
	box->x = x;
	box->y = y;
	box->width = bwidth;
	box->height = bheight;
}


static void stereo_fb_layout_from_mode(struct stereo_fb_layout *layout,
				       drmModeModeInfo *mode)
{
	unsigned int format = mode->flags & DRM_MODE_FLAG_3D_MASK;
	const int hdisplay = mode->hdisplay, vdisplay = mode->vdisplay;
	int middle;

	switch (format) {
	case DRM_MODE_FLAG_3D_TOP_AND_BOTTOM:
		layout->fb_width = hdisplay;
		layout->fb_height = vdisplay;

		middle = vdisplay / 2;
		box_init(&layout->left, 0, 0, hdisplay, middle);
		box_init(&layout->right,
			 0, middle, hdisplay, vdisplay - middle);
		break;
	case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_HALF:
		layout->fb_width = hdisplay;
		layout->fb_height = vdisplay;

		middle = hdisplay / 2;
		box_init(&layout->left, 0, 0, middle, vdisplay);
		box_init(&layout->right,
			 middle, 0, hdisplay - middle, vdisplay);
		break;
	case DRM_MODE_FLAG_3D_FRAME_PACKING:
	{
		int vactive_space = mode->vtotal - vdisplay;

		layout->fb_width = hdisplay;
		layout->fb_height = 2 * vdisplay + vactive_space;

		box_init(&layout->left,
			 0, 0, hdisplay, vdisplay);
		box_init(&layout->right,
			 0, vdisplay + vactive_space, hdisplay, vdisplay);
		break;
	}
	default:
		igt_assert(0);
	}
}

/**
 * igt_create_stereo_fb:
 * @drm_fd: open i915 drm file descriptor
 * @mode: A stereo 3D mode.
 * @format: drm fourcc pixel format code
 * @tiling: tiling layout of the framebuffer
 *
 * Create a framebuffer for use with the stereo 3D mode specified by @mode.
 *
 * Returns:
 * The kms id of the created framebuffer on success or a negative error code on
 * failure.
 */
unsigned int igt_create_stereo_fb(int drm_fd, drmModeModeInfo *mode,
				  uint32_t format, uint64_t tiling)
{
	struct stereo_fb_layout layout;
	cairo_t *cr;
	uint32_t fb_id;
	struct igt_fb fb;

	stereo_fb_layout_from_mode(&layout, mode);
	fb_id = igt_create_fb(drm_fd, layout.fb_width, layout.fb_height, format,
			      tiling, &fb);
	cr = igt_get_cairo_ctx(drm_fd, &fb);

	igt_paint_image(cr, "1080p-left.png",
			layout.left.x, layout.left.y,
			layout.left.width, layout.left.height);
	igt_paint_image(cr, "1080p-right.png",
			layout.right.x, layout.right.y,
			layout.right.width, layout.right.height);

	cairo_destroy(cr);

	return fb_id;
}

static cairo_format_t drm_format_to_cairo(uint32_t drm_format)
{
	struct format_desc_struct *f;

	for_each_format(f)
		if (f->drm_id == drm_format)
			return f->cairo_id;

	igt_assert_f(0, "can't find a cairo format for %08x (%s)\n",
		     drm_format, igt_format_str(drm_format));
}

struct fb_blit_upload {
	int fd;
	struct igt_fb *fb;
	struct {
		uint32_t handle;
		unsigned size, stride;
		uint8_t *map;
		bool is_dumb;
	} linear;
};

static unsigned int fb_mod_to_obj_tiling(uint64_t fb_mod)
{
	switch (fb_mod) {
	case LOCAL_DRM_FORMAT_MOD_NONE:
		return I915_TILING_NONE;
	case LOCAL_I915_FORMAT_MOD_X_TILED:
		return I915_TILING_X;
	case LOCAL_I915_FORMAT_MOD_Y_TILED:
		return I915_TILING_Y;
	case LOCAL_I915_FORMAT_MOD_Yf_TILED:
		return I915_TILING_Yf;
	default:
		igt_assert(0);
	}
}

static void destroy_cairo_surface__blit(void *arg)
{
	struct fb_blit_upload *blit = arg;
	struct igt_fb *fb = blit->fb;
	unsigned int obj_tiling = fb_mod_to_obj_tiling(fb->tiling);

	munmap(blit->linear.map, blit->linear.size);
	fb->cairo_surface = NULL;

	gem_set_domain(blit->fd, blit->linear.handle,
			I915_GEM_DOMAIN_GTT, 0);

	igt_blitter_fast_copy__raw(blit->fd,
				   blit->linear.handle,
				   blit->linear.stride,
				   I915_TILING_NONE,
				   0, 0, /* src_x, src_y */
				   fb->width, fb->height,
				   fb->gem_handle,
				   fb->stride,
				   obj_tiling,
				   0, 0 /* dst_x, dst_y */);

	gem_sync(blit->fd, blit->linear.handle);
	gem_close(blit->fd, blit->linear.handle);

	free(blit);
}

static void create_cairo_surface__blit(int fd, struct igt_fb *fb)
{
	struct fb_blit_upload *blit;
	cairo_format_t cairo_format;
	unsigned int obj_tiling = fb_mod_to_obj_tiling(fb->tiling);
	int ret;

	blit = malloc(sizeof(*blit));
	igt_assert(blit);

	/*
	 * We create a linear BO that we'll map for the CPU to write to (using
	 * cairo). This linear bo will be then blitted to its final
	 * destination, tiling it at the same time.
	 */
	ret = create_bo_for_fb(fd, fb->width, fb->height, fb->drm_format,
				LOCAL_DRM_FORMAT_MOD_NONE, 0, 0,
				&blit->linear.handle,
				&blit->linear.size,
				&blit->linear.stride,
				&blit->linear.is_dumb);

	igt_assert(ret == 0);

	blit->fd = fd;
	blit->fb = fb;

	/* Copy fb content to linear BO */
	gem_set_domain(fd, blit->linear.handle,
			I915_GEM_DOMAIN_GTT, 0);

	igt_blitter_fast_copy__raw(fd,
				   fb->gem_handle,
				   fb->stride,
				   obj_tiling,
				   0, 0, /* src_x, src_y */
				   fb->width, fb->height,
				   blit->linear.handle,
				   blit->linear.stride,
				   I915_TILING_NONE,
				   0, 0 /* dst_x, dst_y */);

	gem_sync(fd, blit->linear.handle);

	gem_set_domain(fd, blit->linear.handle,
		       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);

	/* Setup cairo context */
	blit->linear.map = gem_mmap__cpu(fd,
					 blit->linear.handle,
					 0,
					 blit->linear.size,
					 PROT_READ | PROT_WRITE);

	cairo_format = drm_format_to_cairo(fb->drm_format);
	fb->cairo_surface =
		cairo_image_surface_create_for_data(blit->linear.map,
						    cairo_format,
						    fb->width, fb->height,
						    blit->linear.stride);
	fb->domain = I915_GEM_DOMAIN_GTT;

	cairo_surface_set_user_data(fb->cairo_surface,
				    (cairo_user_data_key_t *)create_cairo_surface__blit,
				    blit, destroy_cairo_surface__blit);
}

/**
 * igt_dirty_fb:
 * @fd: open drm file descriptor
 * @fb: pointer to an #igt_fb structure
 *
 * Flushes out the whole framebuffer.
 *
 * Returns: 0 upon success.
 */
int igt_dirty_fb(int fd, struct igt_fb *fb)
{
	return drmModeDirtyFB(fb->fd, fb->fb_id, NULL, 0);
}

static void destroy_cairo_surface__gtt(void *arg)
{
	struct igt_fb *fb = arg;

	munmap(cairo_image_surface_get_data(fb->cairo_surface), fb->size);
	fb->cairo_surface = NULL;

	if (fb->is_dumb)
		igt_dirty_fb(fb->fd, fb);
}

static void create_cairo_surface__gtt(int fd, struct igt_fb *fb)
{
	void *ptr;

	if (fb->is_dumb)
		ptr = kmstest_dumb_map_buffer(fd, fb->gem_handle, fb->size,
					      PROT_READ | PROT_WRITE);
	else
		ptr = gem_mmap__gtt(fd, fb->gem_handle, fb->size,
				    PROT_READ | PROT_WRITE);

	fb->cairo_surface =
		cairo_image_surface_create_for_data(ptr,
						    drm_format_to_cairo(fb->drm_format),
						    fb->width, fb->height, fb->stride);
	fb->domain = I915_GEM_DOMAIN_GTT;

	cairo_surface_set_user_data(fb->cairo_surface,
				    (cairo_user_data_key_t *)create_cairo_surface__gtt,
				    fb, destroy_cairo_surface__gtt);
}

static cairo_surface_t *get_cairo_surface(int fd, struct igt_fb *fb)
{
	if (fb->cairo_surface == NULL) {
		if (fb->tiling == LOCAL_I915_FORMAT_MOD_Y_TILED ||
		    fb->tiling == LOCAL_I915_FORMAT_MOD_Yf_TILED)
			create_cairo_surface__blit(fd, fb);
		else
			create_cairo_surface__gtt(fd, fb);
	}

	if (!fb->is_dumb)
		gem_set_domain(fd, fb->gem_handle, I915_GEM_DOMAIN_CPU,
			       I915_GEM_DOMAIN_CPU);

	igt_assert(cairo_surface_status(fb->cairo_surface) == CAIRO_STATUS_SUCCESS);
	return fb->cairo_surface;
}

/**
 * igt_get_cairo_ctx:
 * @fd: open i915 drm file descriptor
 * @fb: pointer to an #igt_fb structure
 *
 * This initializes a cairo surface for @fb and then allocates a drawing context
 * for it. The return cairo drawing context should be released by calling
 * cairo_destroy(). This also sets a default font for drawing text on
 * framebuffers.
 *
 * Returns:
 * The created cairo drawing context.
 */
cairo_t *igt_get_cairo_ctx(int fd, struct igt_fb *fb)
{
	cairo_surface_t *surface;
	cairo_t *cr;

	surface = get_cairo_surface(fd, fb);
	cr = cairo_create(surface);
	cairo_surface_destroy(surface);
	igt_assert(cairo_status(cr) == CAIRO_STATUS_SUCCESS);

	cairo_select_font_face(cr, "Helvetica", CAIRO_FONT_SLANT_NORMAL,
			       CAIRO_FONT_WEIGHT_NORMAL);
	igt_assert(cairo_status(cr) == CAIRO_STATUS_SUCCESS);

	return cr;
}

/**
 * igt_write_fb_to_png:
 * @fd: open i915 drm file descriptor
 * @fb: pointer to an #igt_fb structure
 * @filename: target name for the png image
 *
 * This function stores the contents of the supplied framebuffer into a png
 * image stored at @filename.
 */
void igt_write_fb_to_png(int fd, struct igt_fb *fb, const char *filename)
{
	cairo_surface_t *surface;
	cairo_status_t status;

	surface = get_cairo_surface(fd, fb);
	status = cairo_surface_write_to_png(surface, filename);
	cairo_surface_destroy(surface);

	igt_assert(status == CAIRO_STATUS_SUCCESS);
}

/**
 * igt_remove_fb:
 * @fd: open i915 drm file descriptor
 * @fb: pointer to an #igt_fb structure
 *
 * This function releases all resources allocated in igt_create_fb() for @fb.
 * Note that if this framebuffer is still in use on a primary plane the kernel
 * will disable the corresponding crtc.
 */
void igt_remove_fb(int fd, struct igt_fb *fb)
{
	cairo_surface_destroy(fb->cairo_surface);
	do_or_die(drmModeRmFB(fd, fb->fb_id));
	gem_close(fd, fb->gem_handle);
}

/**
 * igt_bpp_depth_to_drm_format:
 * @bpp: desired bits per pixel
 * @depth: desired depth
 *
 * Returns:
 * The rgb drm fourcc pixel format code corresponding to the given @bpp and
 * @depth values.  Fails hard if no match was found.
 */
uint32_t igt_bpp_depth_to_drm_format(int bpp, int depth)
{
	struct format_desc_struct *f;

	for_each_format(f)
		if (f->bpp == bpp && f->depth == depth)
			return f->drm_id;


	igt_assert_f(0, "can't find drm format with bpp=%d, depth=%d\n", bpp,
		     depth);
}

/**
 * igt_drm_format_to_bpp:
 * @drm_format: drm fourcc pixel format code
 *
 * Returns:
 * The bits per pixel for the given drm fourcc pixel format code. Fails hard if
 * no match was found.
 */
uint32_t igt_drm_format_to_bpp(uint32_t drm_format)
{
	struct format_desc_struct *f;

	for_each_format(f)
		if (f->drm_id == drm_format)
			return f->bpp;

	igt_assert_f(0, "can't find a bpp format for %08x (%s)\n",
		     drm_format, igt_format_str(drm_format));
}

/**
 * igt_format_str:
 * @drm_format: drm fourcc pixel format code
 *
 * Returns:
 * Human-readable fourcc pixel format code for @drm_format or "invalid" no match
 * was found.
 */
const char *igt_format_str(uint32_t drm_format)
{
	struct format_desc_struct *f;

	for_each_format(f)
		if (f->drm_id == drm_format)
			return f->name;

	return "invalid";
}

/**
 * igt_get_all_cairo_formats:
 * @formats: pointer to pointer to store the allocated formats array
 * @format_count: pointer to integer to store the size of the allocated array
 *
 * This functions returns an array of all the drm fourcc codes supported by
 * cairo and this library.
 */
void igt_get_all_cairo_formats(const uint32_t **formats, int *format_count)
{
	static uint32_t *drm_formats;
	static int n_formats;

	if (!drm_formats) {
		struct format_desc_struct *f;
		uint32_t *format;

		n_formats = 0;
		for_each_format(f)
			if (f->cairo_id != CAIRO_FORMAT_INVALID)
				n_formats++;

		drm_formats = calloc(n_formats, sizeof(*drm_formats));
		format = &drm_formats[0];
		for_each_format(f)
			if (f->cairo_id != CAIRO_FORMAT_INVALID)
				*format++ = f->drm_id;
	}

	*formats = drm_formats;
	*format_count = n_formats;
}
