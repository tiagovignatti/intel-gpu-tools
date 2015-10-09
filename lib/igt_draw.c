/*
 * Copyright © 2015 Intel Corporation
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

#include <sys/mman.h>

#include "igt_draw.h"

#include "drmtest.h"
#include "intel_chipset.h"
#include "igt_core.h"
#include "igt_fb.h"
#include "ioctl_wrappers.h"

/**
 * SECTION:igt_draw
 * @short_description: drawing helpers for tests
 * @title: Draw
 * @include: igt.h
 *
 * This library contains some functions for drawing rectangles on buffers using
 * the many different drawing methods we have. It also contains some wrappers
 * that make the process easier if you have the abstract objects in hand.
 *
 * This library only claims support for some pixel formats, but adding support
 * for more formats should be faily easy now that we support both 16bpp and
 * 32bpp. If you need a new pixel format, make sure you update both this file
 * and tests/kms_draw_crc.c.
 */

/* Some internal data structures to avoid having to pass tons of parameters
 * around everything. */
struct cmd_data {
	drm_intel_bufmgr *bufmgr;
	drm_intel_context *context;
};

struct buf_data {
	uint32_t handle;
	uint32_t size;
	uint32_t stride;
	int bpp;
};

struct rect {
	int x;
	int y;
	int w;
	int h;
};

/**
 * igt_draw_get_method_name:
 * @method: draw method
 *
 * Simple function to transform the enum into a string. Useful when naming
 * subtests and printing debug messages.
 */
const char *igt_draw_get_method_name(enum igt_draw_method method)
{
	switch (method) {
	case IGT_DRAW_MMAP_CPU:
		return "mmap-cpu";
	case IGT_DRAW_MMAP_GTT:
		return "mmap-gtt";
	case IGT_DRAW_MMAP_WC:
		return "mmap-wc";
	case IGT_DRAW_PWRITE:
		return "pwrite";
	case IGT_DRAW_BLT:
		return "blt";
	case IGT_DRAW_RENDER:
		return "render";
	default:
		igt_assert(false);
	}
}

#define BIT(num, bit) ((num >> bit) & 1)

static int swizzle_addr(int addr, int swizzle)
{
	int bit6;

	switch (swizzle) {
	case I915_BIT_6_SWIZZLE_NONE:
		bit6 = BIT(addr, 6);
		break;
	case I915_BIT_6_SWIZZLE_9:
		bit6 = BIT(addr, 6) ^ BIT(addr, 9);
		break;
	case I915_BIT_6_SWIZZLE_9_10:
		bit6 = BIT(addr, 6) ^ BIT(addr, 9) ^ BIT(addr, 10);
		break;
	case I915_BIT_6_SWIZZLE_9_11:
		bit6 = BIT(addr, 6) ^ BIT(addr, 9) ^ BIT(addr, 11);
		break;
	case I915_BIT_6_SWIZZLE_9_10_11:
		bit6 = BIT(addr, 6) ^ BIT(addr, 9) ^ BIT(addr, 10) ^
		       BIT(addr, 11);
		break;
	case I915_BIT_6_SWIZZLE_UNKNOWN:
	case I915_BIT_6_SWIZZLE_9_17:
	case I915_BIT_6_SWIZZLE_9_10_17:
	default:
		/* If we hit this case, we need to implement support for the
		 * appropriate swizzling method. */
		igt_require(false);
		break;
	}

	addr &= ~(1 << 6);
	addr |= (bit6 << 6);
	return addr;
}

/* It's all in "pixel coordinates", so make sure you multiply/divide by the bpp
 * if you need to. */
static int linear_x_y_to_tiled_pos(int x, int y, uint32_t stride, int swizzle,
				   int bpp)
{
	int x_tile_size, y_tile_size;
	int x_tile_n, y_tile_n, x_tile_off, y_tile_off;
	int line_size, tile_size;
	int tile_n, tile_off;
	int tiled_pos, tiles_per_line;
	int pixel_size = bpp / 8;

	line_size = stride;
	x_tile_size = 512;
	y_tile_size = 8;
	tile_size = x_tile_size * y_tile_size;
	tiles_per_line = line_size / x_tile_size;

	y_tile_n = y / y_tile_size;
	y_tile_off = y % y_tile_size;

	x_tile_n = (x * pixel_size) / x_tile_size;
	x_tile_off = (x * pixel_size) % x_tile_size;

	tile_n = y_tile_n * tiles_per_line + x_tile_n;
	tile_off = y_tile_off * x_tile_size + x_tile_off;
	tiled_pos = tile_n * tile_size + tile_off;

	tiled_pos = swizzle_addr(tiled_pos, swizzle);

	return tiled_pos / pixel_size;
}

/* It's all in "pixel coordinates", so make sure you multiply/divide by the bpp
 * if you need to. */
static void tiled_pos_to_x_y_linear(int tiled_pos, uint32_t stride,
				    int swizzle, int bpp, int *x, int *y)
{
	int tile_n, tile_off, tiles_per_line, line_size;
	int x_tile_off, y_tile_off;
	int x_tile_n, y_tile_n;
	int x_tile_size, y_tile_size, tile_size;
	int pixel_size = bpp / 8;

	tiled_pos = swizzle_addr(tiled_pos, swizzle);

	line_size = stride;
	x_tile_size = 512;
	y_tile_size = 8;
	tile_size = x_tile_size * y_tile_size;
	tiles_per_line = line_size / x_tile_size;

	tile_n = tiled_pos / tile_size;
	tile_off = tiled_pos % tile_size;

	y_tile_off = tile_off / x_tile_size;
	x_tile_off = tile_off % x_tile_size;

	x_tile_n = tile_n % tiles_per_line;
	y_tile_n = tile_n / tiles_per_line;

	*x = (x_tile_n * x_tile_size + x_tile_off) / pixel_size;
	*y = y_tile_n * y_tile_size + y_tile_off;
}

static void set_pixel(void *_ptr, int index, uint32_t color, int bpp)
{
	if (bpp == 16) {
		uint16_t *ptr = _ptr;
		ptr[index] = color;
	} else if (bpp == 32) {
		uint32_t *ptr = _ptr;
		ptr[index] = color;
	} else {
		igt_assert_f(false, "bpp: %d\n", bpp);
	}
}

static void draw_rect_ptr_linear(void *ptr, uint32_t stride,
				 struct rect *rect, uint32_t color, int bpp)
{
	int x, y, line_begin;

	for (y = rect->y; y < rect->y + rect->h; y++) {
		line_begin = y * stride / (bpp / 8);
		for (x = rect->x; x < rect->x + rect->w; x++)
			set_pixel(ptr, line_begin + x, color, bpp);
	}
}

static void draw_rect_ptr_tiled(void *ptr, uint32_t stride, int swizzle,
				struct rect *rect, uint32_t color, int bpp)
{
	int x, y, pos;

	for (y = rect->y; y < rect->y + rect->h; y++) {
		for (x = rect->x; x < rect->x + rect->w; x++) {
			pos = linear_x_y_to_tiled_pos(x, y, stride, swizzle,
						      bpp);
			set_pixel(ptr, pos, color, bpp);
		}
	}
}

static void draw_rect_mmap_cpu(int fd, struct buf_data *buf, struct rect *rect,
			       uint32_t color)
{
	uint32_t *ptr;
	uint32_t tiling, swizzle;

	gem_set_domain(fd, buf->handle, I915_GEM_DOMAIN_CPU,
		       I915_GEM_DOMAIN_CPU);
	gem_get_tiling(fd, buf->handle, &tiling, &swizzle);

	/* We didn't implement suport for the older tiling methods yet. */
	if (tiling != I915_TILING_NONE)
		igt_require(intel_gen(intel_get_drm_devid(fd)) >= 5);

	ptr = __gem_mmap__cpu(fd, buf->handle, 0, buf->size, 0);
	igt_assert(ptr);

	switch (tiling) {
	case I915_TILING_NONE:
		draw_rect_ptr_linear(ptr, buf->stride, rect, color, buf->bpp);
		break;
	case I915_TILING_X:
		draw_rect_ptr_tiled(ptr, buf->stride, swizzle, rect, color,
				    buf->bpp);
		break;
	default:
		igt_assert(false);
		break;
	}

	gem_sw_finish(fd, buf->handle);

	igt_assert(munmap(ptr, buf->size) == 0);
}

static void draw_rect_mmap_gtt(int fd, struct buf_data *buf, struct rect *rect,
			       uint32_t color)
{
	uint32_t *ptr;

	gem_set_domain(fd, buf->handle, I915_GEM_DOMAIN_GTT,
		       I915_GEM_DOMAIN_GTT);

	ptr = __gem_mmap__gtt(fd, buf->handle, buf->size, PROT_READ | PROT_WRITE);
	igt_assert(ptr);

	draw_rect_ptr_linear(ptr, buf->stride, rect, color, buf->bpp);

	igt_assert(munmap(ptr, buf->size) == 0);
}

static void draw_rect_mmap_wc(int fd, struct buf_data *buf, struct rect *rect,
			      uint32_t color)
{
	uint32_t *ptr;
	uint32_t tiling, swizzle;

	gem_set_domain(fd, buf->handle, I915_GEM_DOMAIN_GTT,
		       I915_GEM_DOMAIN_GTT);
	gem_get_tiling(fd, buf->handle, &tiling, &swizzle);

	/* We didn't implement suport for the older tiling methods yet. */
	if (tiling != I915_TILING_NONE)
		igt_require(intel_gen(intel_get_drm_devid(fd)) >= 5);

	ptr = __gem_mmap__wc(fd, buf->handle, 0, buf->size,
			   PROT_READ | PROT_WRITE);
	igt_assert(ptr);

	switch (tiling) {
	case I915_TILING_NONE:
		draw_rect_ptr_linear(ptr, buf->stride, rect, color, buf->bpp);
		break;
	case I915_TILING_X:
		draw_rect_ptr_tiled(ptr, buf->stride, swizzle, rect, color,
				    buf->bpp);
		break;
	default:
		igt_assert(false);
		break;
	}

	igt_assert(munmap(ptr, buf->size) == 0);
}

static void draw_rect_pwrite_untiled(int fd, struct buf_data *buf,
				     struct rect *rect, uint32_t color)
{
	int i, y, offset;
	int pixel_size = buf->bpp / 8;
	uint8_t tmp[rect->w * pixel_size];

	for (i = 0; i < rect->w; i++)
		set_pixel(tmp, i, color, buf->bpp);

	for (y = rect->y; y < rect->y + rect->h; y++) {
		offset = (y * buf->stride) + (rect->x * pixel_size);
		gem_write(fd, buf->handle, offset, tmp, rect->w * pixel_size);
	}
}

static void draw_rect_pwrite_tiled(int fd, struct buf_data *buf,
				   struct rect *rect, uint32_t color,
				   uint32_t swizzle)
{
	int i;
	int tiled_pos, x, y, pixel_size;
	uint8_t tmp[4096];
	int tmp_used = 0, tmp_size;
	bool flush_tmp = false;
	int tmp_start_pos = 0;
	int pixels_written = 0;

	/* We didn't implement suport for the older tiling methods yet. */
	igt_require(intel_gen(intel_get_drm_devid(fd)) >= 5);

	pixel_size = buf->bpp / 8;
	tmp_size = sizeof(tmp) / pixel_size;

	/* Instead of doing one pwrite per pixel, we try to group the maximum
	 * amount of consecutive pixels we can in a single pwrite: that's why we
	 * use the "tmp" variables. */
	for (i = 0; i < tmp_size; i++)
		set_pixel(tmp, i, color, buf->bpp);

	for (tiled_pos = 0; tiled_pos < buf->size; tiled_pos += pixel_size) {
		tiled_pos_to_x_y_linear(tiled_pos, buf->stride, swizzle,
					buf->bpp, &x, &y);

		if (x >= rect->x && x < rect->x + rect->w &&
		    y >= rect->y && y < rect->y + rect->h) {
			if (tmp_used == 0)
				tmp_start_pos = tiled_pos;
			tmp_used++;
		} else {
			flush_tmp = true;
		}

		if (tmp_used == tmp_size || (flush_tmp && tmp_used > 0) ||
		    tiled_pos + pixel_size >= buf->size) {
			gem_write(fd, buf->handle, tmp_start_pos, tmp,
				  tmp_used * pixel_size);
			flush_tmp = false;
			pixels_written += tmp_used;
			tmp_used = 0;

			if (pixels_written == rect->w * rect->h)
				break;
		}
	}
}

static void draw_rect_pwrite(int fd, struct buf_data *buf,
			     struct rect *rect, uint32_t color)
{
	uint32_t tiling, swizzle;

	gem_get_tiling(fd, buf->handle, &tiling, &swizzle);

	switch (tiling) {
	case I915_TILING_NONE:
		draw_rect_pwrite_untiled(fd, buf, rect, color);
		break;
	case I915_TILING_X:
		draw_rect_pwrite_tiled(fd, buf, rect, color, swizzle);
		break;
	default:
		igt_assert(false);
		break;
	}
}

static void draw_rect_blt(int fd, struct cmd_data *cmd_data,
			  struct buf_data *buf, struct rect *rect,
			  uint32_t color)
{
	drm_intel_bo *dst;
	struct intel_batchbuffer *batch;
	int blt_cmd_len, blt_cmd_tiling, blt_cmd_depth;
	uint32_t devid = intel_get_drm_devid(fd);
	int gen = intel_gen(devid);
	uint32_t tiling, swizzle;
	int pitch;

	gem_get_tiling(fd, buf->handle, &tiling, &swizzle);

	dst = gem_handle_to_libdrm_bo(cmd_data->bufmgr, fd, "", buf->handle);
	igt_assert(dst);

	batch = intel_batchbuffer_alloc(cmd_data->bufmgr, devid);
	igt_assert(batch);

	switch (buf->bpp) {
	case 8:
		blt_cmd_depth = 0;
		break;
	case 16: /* we're assuming 565 */
		blt_cmd_depth = 1 << 24;
		break;
	case 32:
		blt_cmd_depth = 3 << 24;
		break;
	default:
		igt_assert(false);
	}

	blt_cmd_len = (gen >= 8) ?  0x5 : 0x4;
	blt_cmd_tiling = (tiling) ? XY_COLOR_BLT_TILED : 0;
	pitch = (tiling) ? buf->stride / 4 : buf->stride;

	BEGIN_BATCH(6, 1);
	OUT_BATCH(XY_COLOR_BLT_CMD_NOLEN | XY_COLOR_BLT_WRITE_ALPHA |
		  XY_COLOR_BLT_WRITE_RGB | blt_cmd_tiling | blt_cmd_len);
	OUT_BATCH(blt_cmd_depth | (0xF0 << 16) | pitch);
	OUT_BATCH((rect->y << 16) | rect->x);
	OUT_BATCH(((rect->y + rect->h) << 16) | (rect->x + rect->w));
	OUT_RELOC_FENCED(dst, 0, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(color);
	ADVANCE_BATCH();

	intel_batchbuffer_flush(batch);
	intel_batchbuffer_free(batch);
}

static void draw_rect_render(int fd, struct cmd_data *cmd_data,
			     struct buf_data *buf, struct rect *rect,
			     uint32_t color)
{
	drm_intel_bo *src, *dst;
	uint32_t devid = intel_get_drm_devid(fd);
	igt_render_copyfunc_t rendercopy = igt_get_render_copyfunc(devid);
	struct igt_buf src_buf, dst_buf;
	struct intel_batchbuffer *batch;
	uint32_t tiling, swizzle;
	struct buf_data tmp;
	int pixel_size = buf->bpp / 8;
	unsigned adjusted_w, adjusted_dst_x;

	igt_skip_on(!rendercopy);

	/* Rendercopy works at 32bpp, so if you try to do copies on buffers with
	 * smaller bpps you won't succeeed if you need to copy "half" of a 32bpp
	 * pixel or something similar. */
	igt_skip_on(rect->x % (32 / buf->bpp) != 0 ||
		    rect->y % (32 / buf->bpp) != 0 ||
		    rect->w % (32 / buf->bpp) != 0 ||
		    rect->h % (32 / buf->bpp) != 0);

	gem_get_tiling(fd, buf->handle, &tiling, &swizzle);

	/* We create a temporary buffer and copy from it using rendercopy. */
	tmp.size = rect->w * rect->h * pixel_size;
	tmp.handle = gem_create(fd, tmp.size);
	tmp.stride = rect->w * pixel_size;
	tmp.bpp = buf->bpp;
	draw_rect_mmap_cpu(fd, &tmp, &(struct rect){0, 0, rect->w, rect->h},
			   color);

	src = gem_handle_to_libdrm_bo(cmd_data->bufmgr, fd, "", tmp.handle);
	igt_assert(src);
	dst = gem_handle_to_libdrm_bo(cmd_data->bufmgr, fd, "", buf->handle);
	igt_assert(dst);

	src_buf.bo = src;
	src_buf.stride = tmp.stride;
	src_buf.tiling = I915_TILING_NONE;
	src_buf.size = tmp.size;
	dst_buf.bo = dst;
	dst_buf.stride = buf->stride;
	dst_buf.tiling = tiling;
	dst_buf.size = buf->size;

	batch = intel_batchbuffer_alloc(cmd_data->bufmgr, devid);
	igt_assert(batch);

	switch (buf->bpp) {
	case 16:
	case 32:
		adjusted_w = rect->w / (32 / buf->bpp);
		adjusted_dst_x = rect->x / (32 / buf->bpp);
		break;
	default:
		igt_assert(false);
	}

	rendercopy(batch, cmd_data->context, &src_buf, 0, 0, adjusted_w,
		   rect->h, &dst_buf, adjusted_dst_x, rect->y);

	intel_batchbuffer_free(batch);
	gem_close(fd, tmp.handle);
}

/**
 * igt_draw_rect:
 * @fd: the DRM file descriptor
 * @bufmgr: the libdrm bufmgr, only required for IGT_DRAW_BLT and
 *          IGT_DRAW_RENDER
 * @context: the context, can be NULL if you don't want to think about it
 * @buf_handle: the handle of the buffer where you're going to draw to
 * @buf_size: the size of the buffer
 * @buf_stride: the stride of the buffer
 * @method: method you're going to use to write to the buffer
 * @rect_x: horizontal position on the buffer where your rectangle starts
 * @rect_y: vertical position on the buffer where your rectangle starts
 * @rect_w: width of the rectangle
 * @rect_h: height of the rectangle
 * @color: color of the rectangle
 * @bpp: bits per pixel
 *
 * This function draws a colored rectangle on the destination buffer, allowing
 * you to specify the method used to draw the rectangle.
 */
void igt_draw_rect(int fd, drm_intel_bufmgr *bufmgr, drm_intel_context *context,
		   uint32_t buf_handle, uint32_t buf_size, uint32_t buf_stride,
		   enum igt_draw_method method, int rect_x, int rect_y,
		   int rect_w, int rect_h, uint32_t color, int bpp)
{
	struct cmd_data cmd_data = {
		.bufmgr = bufmgr,
		.context = context,
	};
	struct buf_data buf = {
		.handle = buf_handle,
		.size = buf_size,
		.stride = buf_stride,
		.bpp = bpp,
	};
	struct rect rect = {
		.x = rect_x,
		.y = rect_y,
		.w = rect_w,
		.h = rect_h,
	};

	switch (method) {
	case IGT_DRAW_MMAP_CPU:
		draw_rect_mmap_cpu(fd, &buf, &rect, color);
		break;
	case IGT_DRAW_MMAP_GTT:
		draw_rect_mmap_gtt(fd, &buf, &rect, color);
		break;
	case IGT_DRAW_MMAP_WC:
		draw_rect_mmap_wc(fd, &buf, &rect, color);
		break;
	case IGT_DRAW_PWRITE:
		draw_rect_pwrite(fd, &buf, &rect, color);
		break;
	case IGT_DRAW_BLT:
		draw_rect_blt(fd, &cmd_data, &buf, &rect, color);
		break;
	case IGT_DRAW_RENDER:
		draw_rect_render(fd, &cmd_data, &buf, &rect, color);
		break;
	default:
		igt_assert(false);
		break;
	}
}

static int get_format_bpp(uint32_t drm_format)
{
	switch (drm_format) {
	case DRM_FORMAT_RGB565:
		return 16;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB2101010:
		return 32;
	default:
		igt_assert(false);
	}
}

/**
 * igt_draw_rect_fb:
 * @fd: the DRM file descriptor
 * @bufmgr: the libdrm bufmgr, only required for IGT_DRAW_BLT and
 *          IGT_DRAW_RENDER
 * @context: the context, can be NULL if you don't want to think about it
 * @fb: framebuffer
 * @method: method you're going to use to write to the buffer
 * @rect_x: horizontal position on the buffer where your rectangle starts
 * @rect_y: vertical position on the buffer where your rectangle starts
 * @rect_w: width of the rectangle
 * @rect_h: height of the rectangle
 * @color: color of the rectangle
 *
 * This is exactly the same as igt_draw_rect, but you can pass an igt_fb instead
 * of manually providing its details. See igt_draw_rect.
 */
void igt_draw_rect_fb(int fd, drm_intel_bufmgr *bufmgr,
		      drm_intel_context *context, struct igt_fb *fb,
		      enum igt_draw_method method, int rect_x, int rect_y,
		      int rect_w, int rect_h, uint32_t color)
{
	igt_draw_rect(fd, bufmgr, context, fb->gem_handle, fb->size, fb->stride,
		      method, rect_x, rect_y, rect_w, rect_h, color,
		      get_format_bpp(fb->drm_format));
}

/**
 * igt_draw_fill_fb:
 * @fd: the DRM file descriptor
 * @fb: the FB that is going to be filled
 * @color: the color you're going to paint it
 *
 * This function just paints an igt_fb using the provided color.
 */
void igt_draw_fill_fb(int fd, struct igt_fb *fb, uint32_t color)
{
	igt_draw_rect_fb(fd, NULL, NULL, fb, IGT_DRAW_MMAP_GTT,
			 0, 0, fb->width, fb->height, color);
}
