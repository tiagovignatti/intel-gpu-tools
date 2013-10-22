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
 * Authors:
 *    Damien Lespiau <damien.lespiau@intel.com>
 */

#include <cairo.h>

#include "rendercopy.h"

void scratch_buf_write_to_png(struct scratch_buf *buf, const char *filename)
{
	cairo_surface_t *surface;
	cairo_status_t ret;

	drm_intel_bo_map(buf->bo, 0);
	surface = cairo_image_surface_create_for_data(buf->bo->virtual,
						      CAIRO_FORMAT_RGB24,
						      buf_width(buf),
						      buf_height(buf),
						      buf->stride);
	ret = cairo_surface_write_to_png(surface, filename);
	if (ret != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr, "%s: %s\n", __func__,
			cairo_status_to_string(ret));
	}
	cairo_surface_destroy(surface);
	drm_intel_bo_unmap(buf->bo);
}
