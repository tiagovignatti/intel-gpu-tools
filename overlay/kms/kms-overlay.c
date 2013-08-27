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

#include <sys/types.h>
#include <sys/mman.h>
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <drm.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <i915_drm.h>
#include "../overlay.h"
//#include "rgb2yuv.h"

#ifndef ALIGN
#define ALIGN(i,m)	(((i) + (m) - 1) & ~((m) - 1))
#endif

struct kms_image {
	uint32_t handle, name;
	uint32_t format;
	uint32_t width, height, stride;
	uint32_t size;
	void *map;
};

struct kms_overlay {
	struct overlay base;
	struct kms_image image;
	int fd;
	int crtc;

	int x, y;
	int visible;

	void *mem;
	int size;
};

static inline struct kms_overlay *to_kms_overlay(struct overlay *o)
{
	return (struct kms_overlay *)o;
}

static int kms_create_fb(int fd, struct kms_image *image)
{
	uint32_t offsets[4], pitches[4], handles[4];

	handles[0] = image->handle;
	pitches[0] = image->stride;
	offsets[0] = 0;

	return drmModeAddFB2(fd,
			     image->width, image->height, image->format,
			     handles, pitches, offsets,
			     &image->name, 0) == 0;
}

static int attach_to_crtc(int fd, int crtc, int x, int y, struct kms_image *image)
{
	struct drm_mode_set_plane s;

	s.crtc_id = crtc;
	s.fb_id = image->name;
	s.flags = 0;
	s.crtc_x = x;
	s.crtc_y = y;
	s.crtc_w = image->width;
	s.crtc_h = image->height;
	s.src_x = 0;
	s.src_y = 0;
	s.src_w = image->width << 16;
	s.src_h = image->height << 16;

	return drmIoctl(fd, DRM_IOCTL_MODE_SETPLANE, &s) == 0;
}

static int detach_from_crtc(int fd, int crtc)
{
	struct drm_mode_set_plane s;

	memset(&s, 0, sizeof(s));
	s.crtc_id = crtc;
	return drmIoctl(fd, DRM_IOCTL_MODE_SETPLANE, &s) == 0;
}

static void kms_overlay_show(struct overlay *overlay)
{
	struct kms_overlay *priv = to_kms_overlay(overlay);

	memcpy(priv->image.map, priv->mem, priv->size);

	if (!priv->visible) {
		attach_to_crtc(priv->fd, priv->crtc, priv->x, priv->y, &priv->image);
		priv->visible = true;
	}
}

static void kms_overlay_hide(struct overlay *overlay)
{
	struct kms_overlay *priv = to_kms_overlay(overlay);

	if (priv->visible) {
		detach_from_crtc(priv->fd, priv->crtc);
		priv->visible = false;
	}
}

static void kms_overlay_destroy(void *data)
{
	struct kms_overlay *priv = data;
	drmIoctl(priv->fd, DRM_IOCTL_MODE_RMFB, &priv->image.name);
	munmap(priv->image.map, priv->image.size);
	free(priv->mem);
	close(priv->fd);
	free(priv);
}

static int is_i915_device(int fd)
{
	drm_version_t version;
	char name[5] = "";

	memset(&version, 0, sizeof(version));
	version.name_len = 4;
	version.name = name;

	if (drmIoctl(fd, DRM_IOCTL_VERSION, &version))
		return 0;

	return strcmp("i915", name) == 0;
}

static int check_device(int fd)
{
	int ret;

	/* Confirm that this is a i915.ko device with GEM/KMS enabled */
	ret = is_i915_device(fd);
	if (ret) {
		struct drm_i915_getparam gp;
		gp.param = I915_PARAM_HAS_GEM;
		gp.value = &ret;
		if (drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
			ret = 0;
	}
	if (ret) {
		struct drm_mode_card_res res;

		memset(&res, 0, sizeof(res));
		if (drmIoctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res))
			ret = 0;
	}

	return ret;
}

static int i915_open(void)
{
	char buf[80];
	int fd, n;

	for (n = 0; n < 16; n++) {
		sprintf(buf, "/dev/dri/card%d", n);
		fd = open(buf, O_RDWR);
		if (fd == -1)
			continue;

		if (!check_device(fd)) {
			close(fd);
			continue;
		}
		return fd;
	}

	return -1;
}

static int config_get_pipe(struct config *config)
{
	const char *str;

	str = config_get_value(config, "kms", "pipe");
	if (str == NULL)
		return 0;

	return atoi(str);
}

cairo_surface_t *
kms_overlay_create(struct config *config, int *width, int *height)
{
	struct drm_i915_gem_create create;
	struct drm_i915_gem_mmap_gtt map;
	struct kms_overlay *priv;
	drmModeResPtr kmode;
	int i, pipe;

	priv = malloc(sizeof(*priv));
	if (priv == NULL)
		return NULL;

	priv->fd = i915_open();
	if (priv->fd == -1)
		goto err_priv;

	kmode = drmModeGetResources(priv->fd);
	if (kmode == 0)
		goto err_fd;

	pipe = config_get_pipe(config);
	priv->crtc = 0;

	for (i = 0; i < kmode->count_crtcs; i++) {
		struct drm_i915_get_pipe_from_crtc_id get_pipe;

		get_pipe.pipe = 0;
		get_pipe.crtc_id = kmode->crtcs[i];
		if (drmIoctl(priv->fd,
			     DRM_IOCTL_I915_GET_PIPE_FROM_CRTC_ID,
			     &get_pipe)) {
			continue;
		}

		if (get_pipe.pipe != pipe)
			continue;

		priv->crtc = get_pipe.crtc_id;
	}

	if (priv->crtc == 0)
		goto err_fd;

	priv->image.format = DRM_FORMAT_XRGB8888;
	priv->image.width = ALIGN(*width, 4);
	priv->image.height = ALIGN(*height, 2);
	priv->image.stride = ALIGN(4*priv->image.width, 64);
	priv->image.size = ALIGN(priv->image.stride * priv->image.height, 4096);

	create.handle = 0;
	create.size = ALIGN(priv->image.size, 4096);
	drmIoctl(priv->fd, DRM_IOCTL_I915_GEM_CREATE, &create);
	if (create.handle == 0)
		goto err_fd;

	priv->image.handle = create.handle;

	if (!kms_create_fb(priv->fd, &priv->image))
		goto err_create;

	/* XXX set color keys */

	if (!attach_to_crtc(priv->fd, priv->crtc, 0, 0, &priv->image))
		goto err_fb;
	detach_from_crtc(priv->fd, priv->crtc);

	map.handle = create.handle;
	if (drmIoctl(priv->fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &map))
		goto err_fb;

	priv->image.map = mmap(0, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, priv->fd, map.offset);
	if (priv->image.map == (void *)-1)
		goto err_fb;

	priv->mem = malloc(create.size);
	if (priv->mem == NULL)
		goto err_map;

	priv->base.surface =
		cairo_image_surface_create_for_data(priv->mem,
						    CAIRO_FORMAT_RGB24,
						    priv->image.width,
						    priv->image.height,
						    priv->image.stride);
	if (cairo_surface_status(priv->base.surface))
		goto err_mem;

	priv->base.show = kms_overlay_show;
	priv->base.hide = kms_overlay_hide;

	priv->visible = false;
	priv->x = 0;
	priv->y = 0;

	cairo_surface_set_user_data(priv->base.surface, &overlay_key, priv, kms_overlay_destroy);

	*width = priv->image.width;
	*height = priv->image.height;

	drmIoctl(priv->fd, DRM_IOCTL_GEM_CLOSE, &create.handle);
	return priv->base.surface;

err_mem:
	free(priv->mem);
err_map:
	munmap(priv->image.map, create.size);
err_fb:
	drmIoctl(priv->fd, DRM_IOCTL_MODE_RMFB, &priv->image.name);
err_create:
	drmIoctl(priv->fd, DRM_IOCTL_GEM_CLOSE, &create.handle);
err_fd:
	close(priv->fd);
err_priv:
	free(priv);
	return NULL;
}
