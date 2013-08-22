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

#include <X11/Xlib.h>
#include <string.h>
#include <stdio.h>

#include "position.h"
#include "../overlay.h"

static enum position get_position(struct config *config)
{
	const char *v = config_get_value(config, "window", "position");
	if (v == NULL)
		return POS_UNSET;

	if (strcmp(v, "top-left") == 0)
		return POS_TOP_LEFT;

	if (strcmp(v, "top-centre") == 0)
		return POS_TOP_CENTRE;

	if (strcmp(v, "top-right") == 0)
		return POS_TOP_RIGHT;

	if (strcmp(v, "middle-left") == 0)
		return POS_MIDDLE_LEFT;

	if (strcmp(v, "middle-centre") == 0)
		return POS_MIDDLE_CENTRE;

	if (strcmp(v, "middle-right") == 0)
		return POS_MIDDLE_RIGHT;

	if (strcmp(v, "bottom-left") == 0)
		return POS_BOTTOM_LEFT;

	if (strcmp(v, "bottom-centre") == 0)
		return POS_BOTTOM_CENTRE;

	if (strcmp(v, "bottom-right") == 0)
		return POS_BOTTOM_RIGHT;

	return POS_UNSET;
}

enum position
x11_position(Screen *scr, int width, int height,
	     struct config *config,
	     int *x, int *y, int *w, int *h)
{
	enum position position = POS_UNSET;
	const char *geometry;

	*x = *y = 0;
	*w = width;
	*h = height;

	geometry = config_get_value(config, "window", "geometry");
	if (geometry) {
		sscanf(geometry, "%dx%d+%d+%d", w, h, x, y);
		if (*w < width)
			*w = width;
		if (*h < height)
			*h = height;
	} else {
		position = get_position(config);
		if (position != POS_UNSET) {
			if (width == -1) {
				*w = scr->width;
				switch (position & 7) {
				default:
				case 0:
				case 2: *w >>= 1; break;
				}
			} else if (width > scr->width) {
				*w = scr->width;
			} else
				*w = width;

			if (height == -1) {
				*h = scr->height;
				switch ((position >> 4) & 7) {
				default:
				case 0:
				case 2: *h >>= 1; break;
				}
			} else if (height > scr->height)
				*h = scr->height;
			else
				*h = height;

			switch (position & 7) {
			default:
			case 0: *x = 0; break;
			case 1: *x = (scr->width - *w)/2; break;
			case 2: *x = scr->width - *w; break;
			}

			switch ((position >> 4) & 7) {
			default:
			case 0: *y = 0; break;
			case 1: *y = (scr->height - *h)/2; break;
			case 2: *y = scr->height - *h; break;
			}
		}
	}

	return position;
}
