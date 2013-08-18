#include <X11/Xlib.h>
#include <X11/extensions/Xvlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <drm.h>
#include <xf86drm.h>
#include <i915_drm.h>
#include "../overlay.h"
#include "dri2.h"
#include "rgb2yuv.h"

#ifndef ALIGN
#define ALIGN(i,m)	(((i) + (m) - 1) & ~((m) - 1))
#endif

#define FOURCC_XVMC (('C' << 24) + ('M' << 16) + ('V' << 8) + 'X')
#define FOURCC_RGB565 ((16 << 24) + ('B' << 16) + ('G' << 8) + 'R')
#define FOURCC_RGB888 ((24 << 24) + ('B' << 16) + ('G' << 8) + 'R')

struct x11_overlay {
	struct overlay base;
	Display *dpy;
	GC gc;
	XvPortID port;
	XvImage *image;
	void *map, *mem;
	int size;
	unsigned name;
	int x, y;
	int visible;
};
static inline struct x11_overlay *to_x11_overlay(struct overlay *o)
{
	return (struct x11_overlay *)o;
}

static int noop(Display *dpy, XErrorEvent *event)
{
	return 0;
}

static void x11_overlay_show(struct overlay *overlay)
{
	struct x11_overlay *priv = to_x11_overlay(overlay);

	if (priv->image->id == FOURCC_XVMC)
		rgb2yuv(priv->base.surface, priv->image, priv->map);
	else
		memcpy(priv->map, priv->mem, priv->size);

	if (!priv->visible) {
		XvPutImage(priv->dpy, priv->port, DefaultRootWindow(priv->dpy),
			   priv->gc, priv->image,
			   0, 0,
			   priv->image->width, priv->image->height,
			   priv->x, priv->y,
			   priv->image->width, priv->image->height);
		XFlush(priv->dpy);
		priv->visible = true;
	}
}

static void x11_overlay_position(struct overlay *overlay,
				 enum position p)
{
	struct x11_overlay *priv = to_x11_overlay(overlay);
	Screen *scr = ScreenOfDisplay(priv->dpy, DefaultScreen(priv->dpy));

	switch (p & 7) {
	default:
	case 0: priv->x = 0; break;
	case 1: priv->x = (scr->width - priv->image->width)/2; break;
	case 2: priv->x = scr->width - priv->image->width; break;
	}

	switch ((p >> 4) & 7) {
	default:
	case 0: priv->y = 0; break;
	case 1: priv->y = (scr->height - priv->image->height)/2; break;
	case 2: priv->y = scr->height - priv->image->height; break;
	}

	if (priv->visible) {
		XvPutImage(priv->dpy, priv->port, DefaultRootWindow(priv->dpy),
			   priv->gc, priv->image,
			   0, 0,
			   priv->image->width, priv->image->height,
			   priv->x, priv->y,
			   priv->image->width, priv->image->height);
		XFlush(priv->dpy);
	}
}

static void x11_overlay_hide(struct overlay *overlay)
{
	struct x11_overlay *priv = to_x11_overlay(overlay);
	if (priv->visible) {
		XClearWindow(priv->dpy, DefaultRootWindow(priv->dpy));
		XFlush(priv->dpy);
		priv->visible = false;
	}
}

static void x11_overlay_destroy(void *data)
{
	struct x11_overlay *priv = data;
	munmap(priv->map, priv->size);
	free(priv->mem);
	XCloseDisplay(priv->dpy);
	free(priv);
}

cairo_surface_t *
x11_overlay_create(enum position position, int *width, int *height)
{
	Display *dpy;
	Screen *scr;
	cairo_surface_t *surface;
	struct drm_i915_gem_create create;
	struct drm_gem_flink flink;
	struct drm_i915_gem_mmap_gtt map;
	struct x11_overlay *priv;
	unsigned int count, i, j;
	int fd, w, h;
	XvAdaptorInfo *info;
	XvImage *image;
	XvPortID port = -1;
	void *ptr, *mem;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL)
		return NULL;

	scr = ScreenOfDisplay(dpy, DefaultScreen(dpy));

	fd = dri2_open(dpy);
	if (fd < 0)
		goto err_dpy;

	if (XvQueryAdaptors(dpy, DefaultRootWindow(dpy), &count, &info) != Success)
		goto err_fd;

	for (i = 0; i < count; i++) {
		unsigned long visual = 0;

		if (info[i].num_ports != 1)
			continue;

		for (j = 0; j < info[j].num_formats; j++) {
			if (info[i].formats[j].depth == 24) {
				visual = info[i].formats[j].visual_id;
				break;
			}
		}

		if (visual == 0)
			continue;

		port = info[i].base_id;
	}
	XvFreeAdaptorInfo(info);
	if (port == -1)
		goto err_fd;

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

	image = XvCreateImage(dpy, port, FOURCC_RGB565, NULL, w, h);
	if (image == NULL)
		image = XvCreateImage(dpy, port, FOURCC_RGB888, NULL, w, h);
	if (image == NULL) {
		image = XvCreateImage(dpy, port, FOURCC_XVMC, NULL, w, h);
		if (image->pitches[0] == 4) {
			image->pitches[0] = ALIGN(image->width, 1024);
			image->pitches[1] = ALIGN(image->width/2, 1024);
			image->pitches[2] = ALIGN(image->width/2, 1024);
			image->offsets[0] = 0;
			image->offsets[1] = image->pitches[0] * image->height;
			image->offsets[2] = image->offsets[1] + image->pitches[1] * image->height/2;
		}
		rgb2yuv_init();
	}
	if (image == NULL)
		goto err_fd;

	switch (image->id) {
	case FOURCC_RGB888:
	case FOURCC_RGB565:
		create.size = image->pitches[0] * image->height;
		break;
	case FOURCC_XVMC:
		create.size = image->pitches[0] * image->height;
		create.size += image->pitches[1] * image->height;
		break;
	}

	create.handle = 0;
	create.size = ALIGN(create.size, 4096);
	drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);
	if (create.handle == 0)
		goto err_image;

	flink.handle = create.handle;
	if (drmIoctl(fd, DRM_IOCTL_GEM_FLINK, &flink))
		goto err_create;

	map.handle = create.handle;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &map))
		goto err_create;

	ptr = mmap(0, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
	if (ptr == (void *)-1)
		goto err_create;

	mem = malloc(create.size);
	if (mem == NULL)
		goto err_map;

	switch (image->id) {
	default:
	case FOURCC_RGB888:
		i = CAIRO_FORMAT_RGB24;
		j = image->pitches[0];
		break;
	case FOURCC_RGB565:
		i = CAIRO_FORMAT_RGB16_565;
		j = image->pitches[0];
		break;
	case FOURCC_XVMC:
		i = CAIRO_FORMAT_RGB16_565;
		j = cairo_format_stride_for_width(i, image->width);
		break;
	}

	surface = cairo_image_surface_create_for_data(mem, i, image->width, image->height, j);
	if (cairo_surface_status(surface))
		goto err_mem;

	priv = malloc(sizeof(*priv));
	if (priv == NULL)
		goto err_surface;

	priv->base.surface = surface;
	priv->base.show = x11_overlay_show;
	priv->base.position = x11_overlay_position;
	priv->base.hide = x11_overlay_hide;

	priv->dpy = dpy;
	priv->gc = XCreateGC(dpy, DefaultRootWindow(dpy), 0, NULL);
	priv->port = port;
	priv->map = ptr;
	priv->mem = mem;
	priv->size = create.size;
	priv->name = flink.name;
	priv->visible = false;

	switch (position & 7) {
	default:
	case 0: priv->x = 0; break;
	case 1: priv->x = (scr->width - image->width)/2; break;
	case 2: priv->x = scr->width - image->width; break;
	}

	switch ((position >> 4) & 7) {
	default:
	case 0: priv->y = 0; break;
	case 1: priv->y = (scr->height - image->height)/2; break;
	case 2: priv->y = scr->height - image->height; break;
	}


	priv->image = image;
	priv->image->data = (void *)&priv->name;

	cairo_surface_set_user_data(surface, &overlay_key, priv, x11_overlay_destroy);

	XvSetPortAttribute(dpy, port, XInternAtom(dpy, "XV_ALWAYS_ON_TOP", True), 1);

	close(fd);

	*width = image->width;
	*height = image->height;
	return surface;

err_surface:
	cairo_surface_destroy(surface);
err_mem:
	free(mem);
err_map:
	munmap(ptr, create.size);
err_create:
	drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &create.handle);
err_image:
err_fd:
	close(fd);
err_dpy:
	XCloseDisplay(dpy);
	return NULL;
}

void x11_overlay_stop(void)
{
	Display *dpy;
	unsigned int count, i, j;
	XvAdaptorInfo *info;
	XvImage *image;
	XvPortID port = -1;
	uint32_t name;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL)
		return;

	if (XvQueryAdaptors(dpy, DefaultRootWindow(dpy), &count, &info) != Success)
		goto close;

	for (i = 0; i < count; i++) {
		unsigned long visual = 0;

		if (info[i].num_ports != 1)
			continue;

		for (j = 0; j < info[j].num_formats; j++) {
			if (info[i].formats[j].depth == 24) {
				visual = info[i].formats[j].visual_id;
				break;
			}
		}

		if (visual == 0)
			continue;

		port = info[i].base_id;
	}
	XvFreeAdaptorInfo(info);
	if (port == -1)
		goto close;

	XSetErrorHandler(noop);

	image = XvCreateImage(dpy, port, FOURCC_RGB565, NULL, 16, 16);
	if (image == NULL)
		image = XvCreateImage(dpy, port, FOURCC_RGB888, NULL, 16, 16);
	if (image == NULL)
		image = XvCreateImage(dpy, port, FOURCC_XVMC, NULL, 16, 16);
	if (image == NULL)
		goto close;

	name = 0;
	image->data = (void *)&name;

	XvPutImage(dpy, port, DefaultRootWindow(dpy),
		   XCreateGC(dpy, DefaultRootWindow(dpy), 0, NULL), image,
		   0, 0,
		   1, 1,
		   0, 0,
		   1, 1);
	XSync(dpy, True);

close:
	XCloseDisplay(dpy);
}
