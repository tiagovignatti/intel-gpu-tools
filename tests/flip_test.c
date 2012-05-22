/*
 * Copyright 2012 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "config.h"

#include <assert.h>
#include <cairo.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "xf86drm.h"
#include "xf86drmMode.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "testdisplay.h"

drmModeRes *resources;
int drm_fd;
int test_time = 3;

uint32_t *fb_ptr;

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct type_name {
	int type;
	const char *name;
};

struct test_output {
	uint32_t id;
	int mode_valid;
	drmModeModeInfo mode;
	drmModeEncoder *encoder;
	drmModeConnector *connector;
	int crtc;
	int pipe;
	unsigned int current_fb_id;
	unsigned int fb_ids[2];
};

static void dump_mode(drmModeModeInfo *mode)
{
	printf("  %s %d %d %d %d %d %d %d %d %d 0x%x 0x%x %d\n",
	       mode->name,
	       mode->vrefresh,
	       mode->hdisplay,
	       mode->hsync_start,
	       mode->hsync_end,
	       mode->htotal,
	       mode->vdisplay,
	       mode->vsync_start,
	       mode->vsync_end,
	       mode->vtotal,
	       mode->flags,
	       mode->type,
	       mode->clock);
}

static void page_flip_handler(int fd, unsigned int frame, unsigned int sec,
			      unsigned int usec, void *data)
{
	struct test_output *o = data;
	unsigned int new_fb_id;

	if (o->current_fb_id == o->fb_ids[0])
		new_fb_id = o->fb_ids[1];
	else
		new_fb_id = o->fb_ids[0];

	drmModePageFlip(drm_fd, o->crtc, new_fb_id,
			DRM_MODE_PAGE_FLIP_EVENT, o);
	o->current_fb_id = new_fb_id;
}

static void connector_find_preferred_mode(struct test_output *o, int crtc_id)
{
	drmModeConnector *connector;
	drmModeEncoder *encoder = NULL;
	int i, j;

	/* First, find the connector & mode */
	o->mode_valid = 0;
	o->crtc = 0;
	connector = drmModeGetConnector(drm_fd, o->id);
	if (!connector) {
		fprintf(stderr, "could not get connector %d: %s\n",
			o->id, strerror(errno));
		drmModeFreeConnector(connector);
		return;
	}

	if (connector->connection != DRM_MODE_CONNECTED) {
		drmModeFreeConnector(connector);
		return;
	}

	if (!connector->count_modes) {
		fprintf(stderr, "connector %d has no modes\n", o->id);
		drmModeFreeConnector(connector);
		return;
	}

	if (connector->connector_id != o->id) {
		fprintf(stderr, "connector id doesn't match (%d != %d)\n",
			connector->connector_id, o->id);
		drmModeFreeConnector(connector);
		return;
	}

	for (j = 0; j < connector->count_modes; j++) {
		o->mode = connector->modes[j];
		if (o->mode.type & DRM_MODE_TYPE_PREFERRED) {
			o->mode_valid = 1;
			break;
		}
	}

	if (!o->mode_valid) {
		if (connector->count_modes > 0) {
			/* use the first mode as test mode */
			o->mode = connector->modes[0];
			o->mode_valid = 1;
		}
		else {
			fprintf(stderr, "failed to find any modes on connector %d\n",
				o->id);
			return;
		}
	}

	/* Now get the encoder */
	for (i = 0; i < connector->count_encoders; i++) {
		encoder = drmModeGetEncoder(drm_fd, connector->encoders[i]);

		if (!encoder) {
			fprintf(stderr, "could not get encoder %i: %s\n",
				resources->encoders[i], strerror(errno));
			drmModeFreeEncoder(encoder);
			continue;
		}

		break;
	}

	o->encoder = encoder;

	if (i == resources->count_encoders) {
		fprintf(stderr, "failed to find encoder\n");
		o->mode_valid = 0;
		return;
	}

	/* Find first CRTC not in use */
	for (i = 0; i < resources->count_crtcs; i++) {
		if (resources->crtcs[i] != crtc_id)
			continue;
		if (resources->crtcs[i] &&
		    (o->encoder->possible_crtcs & (1<<i))) {
			o->crtc = resources->crtcs[i];
			break;
		}
	}

	if (!o->crtc) {
		fprintf(stderr, "could not find requested crtc %d\n", crtc_id);
		return;
	}

	o->pipe = i;

	o->connector = connector;
}

static cairo_surface_t *
allocate_surface(int fd, uint32_t *handle, int width, int height,
		 int bpp, int depth, int *_stride)
{
	cairo_format_t format;
	struct drm_i915_gem_set_tiling set_tiling;
	int size, v, stride;

	/* Round the tiling up to the next power-of-two and the
	 * region up to the next pot fence size so that this works
	 * on all generations.
	 *
	 * This can still fail if the framebuffer is too large to
	 * be tiled. But then that failure is expected.
	 */
	v = width * bpp / 8;
	for (stride = 512; stride < v; stride *= 2)
		;

	v = stride * height;
	for (size = 1024*1024; size < v; size *= 2)
		;

	*_stride = stride;

	switch (depth) {
	case 16:
		format = CAIRO_FORMAT_RGB16_565;
		break;
	case 24:
		format = CAIRO_FORMAT_RGB24;
		break;
#if 0
	case 30:
		format = CAIRO_FORMAT_RGB30;
		break;
#endif
	case 32:
		format = CAIRO_FORMAT_ARGB32;
		break;
	default:
		fprintf(stderr, "bad depth %d\n", depth);
		return NULL;
	}

	*handle = gem_create(fd, size);

	set_tiling.handle = *handle;
	set_tiling.tiling_mode = I915_TILING_X;
	set_tiling.stride = stride;
	if (ioctl(fd, DRM_IOCTL_I915_GEM_SET_TILING, &set_tiling)) {
		fprintf(stderr, "set tiling failed: %s (stride=%d, size=%d)\n",
			strerror(errno), stride, size);
		return NULL;
	}

	fb_ptr = gem_mmap(fd, *handle, size, PROT_READ | PROT_WRITE);

	return cairo_image_surface_create_for_data((unsigned char *)fb_ptr,
						   format, width, height,
						   stride);
}

enum corner {
	topleft,
	topright,
	bottomleft,
	bottomright,
};

static void paint_marker(cairo_t *cr, int x, int y, char *str,
			 enum corner text_location)
{
	cairo_text_extents_t extents;
	int xoff, yoff;

	cairo_set_font_size(cr, 18);
	cairo_text_extents(cr, str, &extents);

	switch (text_location) {
	case topleft:
		xoff = -20;
		xoff -= extents.width;
		yoff = -20;
		break;
	case topright:
		xoff = 20;
		yoff = -20;
		break;
	case bottomleft:
		xoff = -20;
		xoff -= extents.width;
		yoff = 20;
		break;
	case bottomright:
		xoff = 20;
		yoff = 20;
		break;
	default:
		xoff = 0;
		yoff = 0;
	}

	cairo_move_to(cr, x, y - 20);
	cairo_line_to(cr, x, y + 20);
	cairo_move_to(cr, x - 20, y);
	cairo_line_to(cr, x + 20, y);
	cairo_new_sub_path(cr);
	cairo_arc(cr, x, y, 10, 0, M_PI * 2);
	cairo_set_line_width(cr, 4);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);

	cairo_move_to(cr, x + xoff, y + yoff);
	cairo_text_path(cr, str);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_fill(cr);
}

/*
 * Images from:
 * http://www.docstoc.com/docs/28101658/Make-a-Thaumatrope
 * listed as public domain.
 */
static unsigned int initialize_fb(int fd, int width, int height, int bpp,
				  int depth)
{
	cairo_surface_t *surface;
	cairo_status_t status;
	cairo_t *cr;
	uint32_t handle;
	unsigned int fb_id;
	int ret, stride;
	char buf[128];

	surface = allocate_surface(drm_fd, &handle, width, height, bpp, depth,
				   &stride);
	if (!surface) {
		fprintf(stderr, "allocation failed %dx%d\n", width, height);
		return 0;
	}

	cr = cairo_create(surface);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	/* Paint corner markers */
	snprintf(buf, sizeof buf, "(%d, %d)", 0, 0);
	paint_marker(cr, 0, 0, buf, bottomright);
	snprintf(buf, sizeof buf, "(%d, %d)", width, 0);
	paint_marker(cr, width, 0, buf, bottomleft);
	snprintf(buf, sizeof buf, "(%d, %d)", 0, height);
	paint_marker(cr, 0, height, buf, topright);
	snprintf(buf, sizeof buf, "(%d, %d)", width, height);
	paint_marker(cr, width, height, buf, topleft);

	status = cairo_status(cr);
	cairo_destroy(cr);
	if (status)
		fprintf(stderr, "failed to draw pretty picture %dx%d: %s\n",
			width, height, cairo_status_to_string(status));

	ret = drmModeAddFB(drm_fd, width, height, depth, bpp, stride,
			   handle, &fb_id);
	cairo_surface_destroy(surface);
	gem_close(drm_fd, handle);

	if (ret) {
		fprintf(stderr, "failed to add fb (%dx%d): %s\n",
			width, height, strerror(errno));
		return 0;
	}

	return fb_id;
}

static void set_mode(struct test_output *o, int crtc)
{
	int ret;
	int bpp = 32, depth = 24;
	drmEventContext evctx;
	int width, height;
	struct timeval end;

	connector_find_preferred_mode(o, crtc);
	if (!o->mode_valid)
		return;

	width = o->mode.hdisplay;
	height = o->mode.vdisplay;

	o->fb_ids[0] = initialize_fb(drm_fd, width, height, bpp, depth);
	o->fb_ids[1] = initialize_fb(drm_fd, width, height, bpp, depth);
	if (!o->fb_ids[0] || !o->fb_ids[1]) {
		fprintf(stderr, "failed to create fbs\n");
		ret = -1;
		goto out;
	}

	dump_mode(&o->mode);
	if (drmModeSetCrtc(drm_fd, o->crtc, o->fb_ids[0], 0, 0,
			   &o->id, 1, &o->mode)) {
		fprintf(stderr, "failed to set mode (%dx%d@%dHz): %s\n",
			width, height, o->mode.vrefresh,
			strerror(errno));
		ret = -1;
		goto out;
	}

	ret = drmModePageFlip(drm_fd, o->crtc, o->fb_ids[1],
			      DRM_MODE_PAGE_FLIP_EVENT, o);
	if (ret) {
		fprintf(stderr, "failed to page flip: %s\n", strerror(errno));
		goto out;
	}
	o->current_fb_id = o->fb_ids[1];

	memset(&evctx, 0, sizeof evctx);
	evctx.version = DRM_EVENT_CONTEXT_VERSION;
	evctx.vblank_handler = NULL;
	evctx.page_flip_handler = page_flip_handler;

	gettimeofday(&end, NULL);
	end.tv_sec += 3;

	while (1) {
		struct timeval now, timeout = { .tv_sec = 3, .tv_usec = 0 };
		fd_set fds;

		FD_ZERO(&fds);
		FD_SET(0, &fds);
		FD_SET(drm_fd, &fds);
		ret = select(drm_fd + 1, &fds, NULL, NULL, &timeout);

		if (ret <= 0) {
			fprintf(stderr, "select timed out or error (ret %d)\n",
				ret);
			continue;
		} else if (FD_ISSET(0, &fds)) {
			fprintf(stderr, "no fds active, breaking\n");
			break;
		}

		gettimeofday(&now, NULL);
		if (now.tv_sec > end.tv_sec ||
		    (now.tv_sec == end.tv_sec && now.tv_usec >= end.tv_usec)) {
			ret = 0;
			break;
		}

		drmHandleEvent(drm_fd, &evctx);
	}

out:
	fprintf(stdout, "page flipping on crtc %d, connector %d: %s\n", crtc,
		o->id, ret ? "FAILED" : "PASSED");

	drmModeFreeEncoder(o->encoder);
	drmModeFreeConnector(o->connector);
}

static int run_test(void)
{
	struct test_output *connectors;
	int c, i;

	resources = drmModeGetResources(drm_fd);
	if (!resources) {
		fprintf(stderr, "drmModeGetResources failed: %s\n",
			strerror(errno));
		return 0;
	}

	connectors = calloc(resources->count_connectors,
			    sizeof(struct test_output));
	if (!connectors)
		return 0;

	/* Find any connected displays */
	for (c = 0; c < resources->count_connectors; c++) {
		connectors[c].id = resources->connectors[c];
		for (i = 0; i < resources->count_crtcs; i++)
			set_mode(&connectors[c], resources->crtcs[i]);
	}

	drmModeFreeResources(resources);
	return 1;
}

static char optstr[] = "h";

static void usage(char *name)
{
	fprintf(stderr, "usage: %s [-h]\n", name);
	fprintf(stderr, "\t-h: help\n");
	exit(0);
}

int main(int argc, char **argv)
{
	int c;
	const char *modules[] = { "i915" };
	unsigned int i;
	int ret = 0;

	opterr = 0;
	while ((c = getopt(argc, argv, optstr)) != -1) {
		switch (c) {
		default:
			fprintf(stderr, "unknown option %c\n", c);
			/* fall through */
		case 'h':
			usage(argv[0]);
			break;
		}
	}

	for (i = 0; i < ARRAY_SIZE(modules); i++) {
		drm_fd = drmOpen(modules[i], NULL);
		if (drm_fd < 0) {
			printf("failed to load %s driver.\n", modules[i]);
			goto out;
		} else
			break;
	}

	if (i == ARRAY_SIZE(modules)) {
		fprintf(stderr, "failed to load any modules, aborting.\n");
		ret = -1;
		goto out_close;
	}

	run_test();

out_close:
	drmClose(drm_fd);
out:
	return ret;
}
