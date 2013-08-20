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
#include <cairo.h>
#include <cairo-xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "../overlay.h"

struct x11_window {
	struct overlay base;
	Display *dpy;
	Window win;
	int width, height;
	int visible;
};

static inline struct x11_window *to_x11_window(struct overlay *o)
{
	return (struct x11_window *)o;
}

static int noop(Display *dpy, XErrorEvent *event)
{
	return 0;
}

static void x11_window_show(struct overlay *overlay)
{
	struct x11_window *priv = to_x11_window(overlay);

	if (!priv->visible) {
		XMapWindow(priv->dpy, priv->win);
		priv->visible = true;
	}

	XFlush(priv->dpy);
}

static void x11_window_position(struct overlay *overlay,
				enum position p)
{
	struct x11_window *priv = to_x11_window(overlay);
	Screen *scr = ScreenOfDisplay(priv->dpy, DefaultScreen(priv->dpy));
	int x, y;

	switch (p & 7) {
	default:
	case 0: x = 0; break;
	case 1: x = (scr->width - priv->width)/2; break;
	case 2: x = scr->width - priv->width; break;
	}

	switch ((p >> 4) & 7) {
	default:
	case 0: y = 0; break;
	case 1: y = (scr->height - priv->height)/2; break;
	case 2: y = scr->height - priv->height; break;
	}

	if (priv->visible) {
		XMoveWindow(priv->dpy, priv->win, x, y);
		XFlush(priv->dpy);
	}
}

static void x11_window_hide(struct overlay *overlay)
{
	struct x11_window *priv = to_x11_window(overlay);
	if (priv->visible) {
		XUnmapWindow(priv->dpy, priv->win);
		XFlush(priv->dpy);
		priv->visible = false;
	}
}

static void x11_window_destroy(void *data)
{
	struct x11_window *priv = data;
	XDestroyWindow(priv->dpy, priv->win);
	XCloseDisplay(priv->dpy);
	free(priv);
}

cairo_surface_t *
x11_window_create(enum position position, int *width, int *height)
{
	Display *dpy;
	Screen *scr;
	Window win;
	int screen;
	cairo_surface_t *surface;
	XSetWindowAttributes attr;
	struct x11_window *priv;
	int x, y, w, h;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL)
		return NULL;

	screen = DefaultScreen(dpy);
	scr = XScreenOfDisplay(dpy, screen);

	XSetErrorHandler(noop);

	if (*width == -1) {
		w = scr->width;
		switch (position & 7) {
		default:
		case 0:
		case 2: w >>= 1; break;
		}
	} else if (*width > scr->width) {
		w = scr->width;
	} else
		w = *width;

	if (*height == -1) {
		h = scr->height;
		switch ((position >> 4) & 7) {
		default:
		case 0:
		case 2: h >>= 1; break;
		}
	} else if (*height > scr->height)
		h = scr->height;
	else
		h = *height;

	switch (position & 7) {
	default:
	case 0: x = 0; break;
	case 1: x = (scr->width - w)/2; break;
	case 2: x = scr->width - w; break;
	}

	switch ((position >> 4) & 7) {
	default:
	case 0: y = 0; break;
	case 1: y = (scr->height - h)/2; break;
	case 2: y = scr->height - h; break;
	}

	attr.override_redirect = True;
	win = XCreateWindow(dpy, DefaultRootWindow(dpy),
			   x, y, w, h, 0,
			   DefaultDepth(dpy, screen),
			   InputOutput,
			   DefaultVisual(dpy, screen),
			   CWOverrideRedirect, &attr);

	surface = cairo_xlib_surface_create(dpy, win, DefaultVisual (dpy, screen), w, h);
	if (cairo_surface_status(surface))
		goto err_win;

	priv = malloc(sizeof(*priv));
	if (priv == NULL)
		goto err_surface;

	priv->base.surface = surface;
	priv->base.show = x11_window_show;
	priv->base.position = x11_window_position;
	priv->base.hide = x11_window_hide;

	priv->dpy = dpy;
	priv->win = win;
	priv->visible = false;

	priv->width = w;
	priv->height = h;

	cairo_surface_set_user_data(surface, &overlay_key, priv, x11_window_destroy);

	*width = w;
	*height = h;
	return surface;

err_surface:
	cairo_surface_destroy(surface);
err_win:
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return NULL;
}
