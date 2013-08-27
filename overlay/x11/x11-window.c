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
#include "position.h"

struct x11_window {
	struct overlay base;
	cairo_surface_t *front;
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
	cairo_t *cr;

	cr = cairo_create(priv->front);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, priv->base.surface, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);

	cairo_surface_flush(priv->front);

	if (!priv->visible) {
		XMapWindow(priv->dpy, priv->win);
		priv->visible = true;
	}

	XFlush(priv->dpy);
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
	cairo_surface_destroy(priv->front);
	XDestroyWindow(priv->dpy, priv->win);
	XCloseDisplay(priv->dpy);
	free(priv);
}

static int prefer_image(struct config *config)
{
	const char *v = config_get_value(config, "x11", "prefer-image");

	if (v == NULL)
		return 0;
	if (*v == '\0')
		return 1;

	return atoi(v);
}

cairo_surface_t *
x11_window_create(struct config *config, int *width, int *height)
{
	Display *dpy;
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

	XSetErrorHandler(noop);

	x11_position(dpy, *width, *height, config, &x, &y, &w, &h);

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

	if (prefer_image(config))
		priv->base.surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
	else
		priv->base.surface = cairo_surface_create_similar(surface, CAIRO_CONTENT_COLOR, w, h);
	if (cairo_surface_status(priv->base.surface))
		goto err_priv;

	priv->base.show = x11_window_show;
	priv->base.hide = x11_window_hide;

	priv->dpy = dpy;
	priv->win = win;
	priv->front = surface;
	priv->visible = false;

	priv->width = w;
	priv->height = h;

	cairo_surface_set_user_data(priv->base.surface, &overlay_key, priv, x11_window_destroy);

	*width = w;
	*height = h;
	return priv->base.surface;

err_priv:
	free(priv);
err_surface:
	cairo_surface_destroy(surface);
err_win:
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return NULL;
}
