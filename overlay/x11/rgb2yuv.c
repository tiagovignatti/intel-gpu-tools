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

#include <stdint.h>
#include <stdlib.h>

#include "rgb2yuv.h"

static int RGB2YUV_YR[256], RGB2YUV_YG[256], RGB2YUV_YB[256];
static int RGB2YUV_UR[256], RGB2YUV_UG[256], RGB2YUV_UBVR[256];
static int RGB2YUV_VG[256], RGB2YUV_VB[256];

void rgb2yuv_init(void)
{
	int i;

	for (i = 0; i < 256; i++)
		RGB2YUV_YR[i] = 65.481 * (i << 8);

	for (i = 0; i < 256; i++)
		RGB2YUV_YG[i] = 128.553 * (i << 8);

	for (i = 0; i < 256; i++)
		RGB2YUV_YB[i] = 24.966 * (i << 8);

	for (i = 0; i < 256; i++)
		RGB2YUV_UR[i] = 37.797 * (i << 8);

	for (i = 0; i < 256; i++)
		RGB2YUV_UG[i] = 74.203 * (i << 8);

	for (i = 0; i < 256; i++)
		RGB2YUV_VG[i] = 93.786 * (i << 8);

	for (i = 0; i < 256; i++)
		RGB2YUV_VB[i] = 18.214 * (i << 8);

	for (i = 0; i < 256; i++)
		RGB2YUV_UBVR[i] = 112 * (i << 8);
}

int rgb2yuv(cairo_surface_t *surface, XvImage *image, uint8_t *yuv)
{
	uint8_t *data = cairo_image_surface_get_data(surface);
	int rgb_stride = cairo_image_surface_get_stride(surface);
	int width = cairo_image_surface_get_width(surface);
	int height = cairo_image_surface_get_height(surface);
	int y_stride = image->pitches[0];
	int uv_stride = image->pitches[1];
	uint8_t *tmp, *tl, *tr, *bl, *br;
	int i, j;

	tmp = malloc(2*width*height);
	if (tmp == NULL)
		return 0;

	tl = tmp;
	bl = tmp + width*height;

	for (i = 0; i < height; i++) {
		uint16_t *rgb = (uint16_t *)(data + i * rgb_stride);
		for (j = 0; j < width; j++) {
			uint8_t r = (rgb[j] >> 11) & 0x1f;
			uint8_t g = (rgb[j] >>  5) & 0x3f;
			uint8_t b = (rgb[j] >>  0) & 0x1f;

			r = r<<3 | r>>2;
			g = g<<2 | g>>4;
			b = b<<3 | b>>2;

			yuv[j] = (RGB2YUV_YR[r] + RGB2YUV_YG[g] + RGB2YUV_YB[b] + 1048576) >> 16;
			*tl++ = (-RGB2YUV_UR[r] - RGB2YUV_UG[g] + RGB2YUV_UBVR[b] + 8388608) >> 16;
			*bl++ = (RGB2YUV_UBVR[r] - RGB2YUV_VG[g] - RGB2YUV_VB[b] + 8388608) >> 16;
		}
		yuv += y_stride;
	}

	tl = tmp; tr = tl + 1;
	bl = tl + width; br = bl + 1;
	for (i = 0; i < height/2; i ++) {
		for (j = 0; j < width/2; j ++) {
			yuv[j] = ((int)*tl + *tr + *bl + *br) >> 2;
			tl += 2; tr += 2;
			bl += 2; br += 2;
		}
		yuv += uv_stride;

		tl += width; tr += width;
		bl += width; br += width;
	}

	tl = tmp + width*height; tr = tl + 1;
	bl = tl + width; br = bl + 1;
	for (i = 0; i < height/2; i++) {
		for (j = 0; j < width/2; j++) {
			yuv[j] = ((int)*tl + *tr + *bl + *br) >> 2;
			tl += 2; tr += 2;
			bl += 2; br += 2;
		}
		yuv += uv_stride;

		tl += width; tr += width;
		bl += width; br += width;
	}

	free(tmp);
	return 1;
}
