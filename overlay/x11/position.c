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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/Xlib.h>
#ifdef HAVE_XRANDR
#include <X11/extensions/Xrandr.h>
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

static void screen_size(Display *dpy, struct config *config,
			int *scr_x, int *scr_y, int *scr_width, int *scr_height)
{
	const char *crtc;
	Screen *scr;

#ifdef HAVE_XRANDR
	crtc = config_get_value(config, "x11", "crtc");
	if (crtc) {
		XRRScreenResources *res;
		int i = atoi(crtc);
		int ok = 0;

		res = XRRGetScreenResourcesCurrent(dpy, DefaultRootWindow(dpy));
		if (res) {
			if (i < res->ncrtc) {
				XRRCrtcInfo *info = XRRGetCrtcInfo (dpy, res, res->crtcs[i]);
				if (info) {
					*scr_x = info->x;
					*scr_y = info->y;
					*scr_width = info->width;
					*scr_height = info->height;
					ok = 1;
					XRRFreeCrtcInfo(info);
				}
			}
			XRRFreeScreenResources(res);
		}
		if (ok)
			return;
	}
#endif

	scr = ScreenOfDisplay(dpy, DefaultScreen(dpy));
	*scr_x = *scr_y = 0;
	*scr_width = scr->width;
	*scr_height = scr->height;
}

enum position
x11_position(Display *dpy, int width, int height,
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
		if (*w < width/2)
			*w = width/2;
		if (*h < height/2)
			*h = height/2;
	} else {
		int scr_x, scr_y, scr_width, scr_height;

		screen_size(dpy, config, &scr_x, &scr_y, &scr_width, &scr_height);
		position = get_position(config);

		if (position != POS_UNSET) {
			if (width == -1) {
				*w = scr_width;
				switch (position & 7) {
				default:
				case 0:
				case 2: *w >>= 1; break;
				}
			}

			if (height == -1) {
				*h = scr_height;
				switch ((position >> 4) & 7) {
				default:
				case 0:
				case 2: *h >>= 1; break;
				}
			}
		}

		geometry = config_get_value(config, "window", "size");
		if (geometry) {
			int size_w, size_h;
			float scale_x, scale_y;

			if (sscanf(geometry, "%dx%d", &size_w, &size_h) == 2) {
				*w = size_w;
				*h = size_h;
			} else if (sscanf(geometry, "%f%%x%f%%", &scale_x, &scale_y) == 2) {
				if (*w != -1)
					*w = (*w * scale_x) / 100.;
				if (*h != -1)
					*h = (*h * scale_y) / 100.;
			} else if (sscanf(geometry, "%f%%", &scale_x) == 1) {
				if (*w != -1)
					*w = (*w * scale_x) / 100.;
				if (*h != -1)
					*h = (*h * scale_x) / 100.;
			}
			if ((unsigned)*w < width/2)
				*w = width/2;
			if ((unsigned)*h < height/2)
				*h = height/2;
		}

		if ((unsigned)*w > scr_width)
			*w = scr_width;

		if ((unsigned)*h > scr_height)
			*h = scr_height;

		if (position != POS_UNSET) {
			switch (position & 7) {
			default:
			case 0: *x = 0; break;
			case 1: *x = (scr_width - *w)/2; break;
			case 2: *x = scr_width - *w; break;
			}

			switch ((position >> 4) & 7) {
			default:
			case 0: *y = 0; break;
			case 1: *y = (scr_height - *h)/2; break;
			case 2: *y = scr_height - *h; break;
			}
		}

		*x += scr_x;
		*y += scr_y;
	}

	return position;
}
