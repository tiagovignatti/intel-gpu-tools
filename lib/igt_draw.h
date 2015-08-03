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

#ifndef __IGT_DRAW_H__
#define __IGT_DRAW_H__

#include <intel_bufmgr.h>
#include "igt_fb.h"

/**
 * igt_draw_method:
 * @IGT_DRAW_MMAP_CPU: draw using a CPU mmap.
 * @IGT_DRAW_MMAP_GTT: draw using a GTT mmap.
 * @IGT_DRAW_MMAP_WC: draw using the WC mmap.
 * @IGT_DRAW_PWRITE: draw using the pwrite ioctl.
 * @IGT_DRAW_BLT: draw using the BLT ring.
 * @IGT_DRAW_RENDER: draw using the render ring.
 * @IGT_DRAW_METHOD_COUNT: useful for iterating through everything.
 */
enum igt_draw_method {
	IGT_DRAW_MMAP_CPU,
	IGT_DRAW_MMAP_GTT,
	IGT_DRAW_MMAP_WC,
	IGT_DRAW_PWRITE,
	IGT_DRAW_BLT,
	IGT_DRAW_RENDER,
	IGT_DRAW_METHOD_COUNT,
};

const char *igt_draw_get_method_name(enum igt_draw_method method);

void igt_draw_rect(int fd, drm_intel_bufmgr *bufmgr, drm_intel_context *context,
		   uint32_t buf_handle, uint32_t buf_size, uint32_t buf_stride,
		   enum igt_draw_method method, int rect_x, int rect_y,
		   int rect_w, int rect_h, uint32_t color, int bpp);

void igt_draw_rect_fb(int fd, drm_intel_bufmgr *bufmgr,
		      drm_intel_context *context, struct igt_fb *fb,
		      enum igt_draw_method method, int rect_x, int rect_y,
		      int rect_w, int rect_h, uint32_t color);

void igt_draw_fill_fb(int fd, struct igt_fb *fb, uint32_t color);

#endif /* __IGT_DRAW_H__ */
