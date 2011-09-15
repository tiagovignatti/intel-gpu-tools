/*
 * Copyright 2010 Intel Corporation
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

/*
 * This program is intended for testing of display functionality.  It should
 * allow for testing of
 *   - hotplug
 *   - mode setting
 *   - clone & twin modes
 *   - panel fitting
 *   - test patterns & pixel generators
 * Additional programs can test the detected outputs against VBT provided
 * device lists (both docked & undocked).
 *
 * TODO:
 * - pixel generator in transcoder
 * - test pattern reg in pipe
 * - test patterns on outputs (e.g. TV)
 * - handle hotplug (leaks crtcs, can't handle clones)
 * - allow mode force
 * - expose output specific controls
 *  - e.g. DDC-CI brightness
 *  - HDMI controls
 *  - panel brightness
 *  - DP commands (e.g. poweroff)
 * - verify outputs against VBT/physical connectors
 */
#include "config.h"

#include <assert.h>
#include <cairo.h>
#include <errno.h>
#include <glib.h>
#include <libudev.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "xf86drm.h"
#include "xf86drmMode.h"
#include "i915_drm.h"

#ifdef DRM_IOCTL_MODE_ADDFB2
#define TEST_PLANES 1
#endif

struct udev_monitor *uevent_monitor;
drmModeRes *resources;
int fd, modes;
int dump_info = 0, test_all_modes =0, test_preferred_mode = 0, force_mode = 0,
	test_plane, enable_tiling;
int sleep_between_modes = 5;
uint32_t depth = 24;

float force_clock;
int	force_hdisplay;
int	force_hsync_start;
int	force_hsync_end;
int	force_htotal;
int	force_vdisplay;
int	force_vsync_start;
int	force_vsync_end;
int	force_vtotal;

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct type_name {
	int type;
	char *name;
};

#define type_name_fn(res) \
static char * res##_str(int type) {			\
	unsigned int i;					\
	for (i = 0; i < ARRAY_SIZE(res##_names); i++) { \
		if (res##_names[i].type == type)	\
			return res##_names[i].name;	\
	}						\
	return "(invalid)";				\
}

struct type_name encoder_type_names[] = {
	{ DRM_MODE_ENCODER_NONE, "none" },
	{ DRM_MODE_ENCODER_DAC, "DAC" },
	{ DRM_MODE_ENCODER_TMDS, "TMDS" },
	{ DRM_MODE_ENCODER_LVDS, "LVDS" },
	{ DRM_MODE_ENCODER_TVDAC, "TVDAC" },
};

type_name_fn(encoder_type)

struct type_name connector_status_names[] = {
	{ DRM_MODE_CONNECTED, "connected" },
	{ DRM_MODE_DISCONNECTED, "disconnected" },
	{ DRM_MODE_UNKNOWNCONNECTION, "unknown" },
};

type_name_fn(connector_status)

struct type_name connector_type_names[] = {
	{ DRM_MODE_CONNECTOR_Unknown, "unknown" },
	{ DRM_MODE_CONNECTOR_VGA, "VGA" },
	{ DRM_MODE_CONNECTOR_DVII, "DVI-I" },
	{ DRM_MODE_CONNECTOR_DVID, "DVI-D" },
	{ DRM_MODE_CONNECTOR_DVIA, "DVI-A" },
	{ DRM_MODE_CONNECTOR_Composite, "composite" },
	{ DRM_MODE_CONNECTOR_SVIDEO, "s-video" },
	{ DRM_MODE_CONNECTOR_LVDS, "LVDS" },
	{ DRM_MODE_CONNECTOR_Component, "component" },
	{ DRM_MODE_CONNECTOR_9PinDIN, "9-pin DIN" },
	{ DRM_MODE_CONNECTOR_DisplayPort, "DisplayPort" },
	{ DRM_MODE_CONNECTOR_HDMIA, "HDMI-A" },
	{ DRM_MODE_CONNECTOR_HDMIB, "HDMI-B" },
	{ DRM_MODE_CONNECTOR_TV, "TV" },
	{ DRM_MODE_CONNECTOR_eDP, "Embedded DisplayPort" },
};

type_name_fn(connector_type)

/*
 * Mode setting with the kernel interfaces is a bit of a chore.
 * First you have to find the connector in question and make sure the
 * requested mode is available.
 * Then you need to find the encoder attached to that connector so you
 * can bind it with a free crtc.
 */
struct connector {
	uint32_t id;
	int mode_valid;
	drmModeModeInfo mode;
	drmModeEncoder *encoder;
	drmModeConnector *connector;
	int crtc;
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

static void dump_connectors(void)
{
	int i, j;

	printf("Connectors:\n");
	printf("id\tencoder\tstatus\t\ttype\tsize (mm)\tmodes\n");
	for (i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector;

		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (!connector) {
			fprintf(stderr, "could not get connector %i: %s\n",
				resources->connectors[i], strerror(errno));
			continue;
		}

		printf("%d\t%d\t%s\t%s\t%dx%d\t\t%d\n",
		       connector->connector_id,
		       connector->encoder_id,
		       connector_status_str(connector->connection),
		       connector_type_str(connector->connector_type),
		       connector->mmWidth, connector->mmHeight,
		       connector->count_modes);

		if (!connector->count_modes)
			continue;

		printf("  modes:\n");
		printf("  name refresh (Hz) hdisp hss hse htot vdisp "
		       "vss vse vtot flags type clock\n");
		for (j = 0; j < connector->count_modes; j++)
			dump_mode(&connector->modes[j]);

		drmModeFreeConnector(connector);
	}
	printf("\n");
}

static void dump_crtcs(void)
{
	int i;

	printf("CRTCs:\n");
	printf("id\tfb\tpos\tsize\n");
	for (i = 0; i < resources->count_crtcs; i++) {
		drmModeCrtc *crtc;

		crtc = drmModeGetCrtc(fd, resources->crtcs[i]);
		if (!crtc) {
			fprintf(stderr, "could not get crtc %i: %s\n",
				resources->crtcs[i], strerror(errno));
			continue;
		}
		printf("%d\t%d\t(%d,%d)\t(%dx%d)\n",
		       crtc->crtc_id,
		       crtc->buffer_id,
		       crtc->x, crtc->y,
		       crtc->width, crtc->height);
		dump_mode(&crtc->mode);

		drmModeFreeCrtc(crtc);
	}
	printf("\n");
}

#ifdef TEST_PLANES
static void dump_planes(void)
{
	drmModePlaneRes *plane_resources;
	drmModePlane *ovr;
	int i;

	plane_resources = drmModeGetPlaneResources(fd);
	if (!plane_resources) {
		fprintf(stderr, "drmModeGetPlaneResources failed: %s\n",
			strerror(errno));
		return;
	}

	printf("Planes:\n");
	printf("id\tcrtc\tfb\tCRTC x,y\tx,y\tgamma size\n");
	for (i = 0; i < plane_resources->count_planes; i++) {
		ovr = drmModeGetPlane(fd, plane_resources->planes[i]);
		if (!ovr) {
			fprintf(stderr, "drmModeGetPlane failed: %s\n",
				strerror(errno));
			continue;
		}

		printf("%d\t%d\t%d\t%d,%d\t\t%d,%d\t%d\n",
		       ovr->plane_id, ovr->crtc_id, ovr->fb_id,
		       ovr->crtc_x, ovr->crtc_y, ovr->x, ovr->y,
		       ovr->gamma_size);

		drmModeFreePlane(ovr);
	}
	printf("\n");

	return;
}
#else
static void dump_planes(void) { return; }
#endif

static void connector_find_preferred_mode(struct connector *c)
{
	drmModeConnector *connector;
	drmModeEncoder *encoder = NULL;
	int i, j;

	/* First, find the connector & mode */
	c->mode_valid = 0;
	connector = drmModeGetConnector(fd, c->id);
	if (!connector) {
		fprintf(stderr, "could not get connector %d: %s\n",
			c->id, strerror(errno));
		drmModeFreeConnector(connector);
		return;
	}

	if (connector->connection != DRM_MODE_CONNECTED) {
		drmModeFreeConnector(connector);
		return;
	}

	if (!connector->count_modes) {
		fprintf(stderr, "connector %d has no modes\n", c->id);
		drmModeFreeConnector(connector);
		return;
	}

	if (connector->connector_id != c->id) {
		fprintf(stderr, "connector id doesn't match (%d != %d)\n",
			connector->connector_id, c->id);
		drmModeFreeConnector(connector);
		return;
	}

	for (j = 0; j < connector->count_modes; j++) {
		c->mode = connector->modes[j];
		if (c->mode.type & DRM_MODE_TYPE_PREFERRED) {
			c->mode_valid = 1;
			break;
		}
	}

	if (!c->mode_valid) {
		if (connector->count_modes > 0) {
			/* use the first mode as test mode */
			c->mode = connector->modes[0];
			c->mode_valid = 1;
		}
		else {
			fprintf(stderr, "failed to find any modes on connector %d\n",
				c->id);
			return;
		}
	}

	/* Now get the encoder */
	for (i = 0; i < connector->count_encoders; i++) {
		encoder = drmModeGetEncoder(fd, connector->encoders[i]);

		if (!encoder) {
			fprintf(stderr, "could not get encoder %i: %s\n",
				resources->encoders[i], strerror(errno));
			drmModeFreeEncoder(encoder);
			continue;
		}

		break;
	}

	c->encoder = encoder;

	if (i == resources->count_encoders) {
		fprintf(stderr, "failed to find encoder\n");
		c->mode_valid = 0;
		return;
	}

	/* Find first CRTC not in use */
	for (i = 0; i < resources->count_crtcs; i++) {
		if (resources->crtcs[i] && (c->encoder->possible_crtcs & (1<<i)))
			break;
	}
	c->crtc = resources->crtcs[i];
	resources->crtcs[i] = 0;
	c->connector = connector;
}

static uint32_t gem_create(int fd, int size)
{
	struct drm_i915_gem_create create;

	create.handle = 0;
	create.size = (size + 4095) & -4096;
	(void)drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);

	return create.handle;
}

static void *gem_mmap(int fd, uint32_t handle, int size, int prot)
{
	struct drm_i915_gem_mmap_gtt mmap_arg;
	void *ptr;

	mmap_arg.handle = handle;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_arg))
		return NULL;

	ptr = mmap(0, size, prot, MAP_SHARED, fd, mmap_arg.offset);
	if (ptr == MAP_FAILED)
		ptr = NULL;

	return ptr;
}

static void gem_close(int fd, uint32_t handle)
{
	struct drm_gem_close close;

	close.handle = handle;
	(void)drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &close);
}

static cairo_surface_t *
allocate_surface(int fd, int width, int height, uint32_t depth, uint32_t bpp,
		 uint32_t *handle, int tiled)
{
	cairo_format_t format;
	struct drm_i915_gem_set_tiling set_tiling;
	int size, stride;

	if (tiled) {
		stride = (width * (bpp / 8) + 511) & ~511;
		size = stride * (height + 7) & ~7;
	} else {
		/* Scan-out has a 64 byte alignment restriction */
		stride = (width * (bpp / 8) + 63) & ~63;
		size = stride * height;
	}

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

	if (tiled) {
		set_tiling.handle = *handle;
		set_tiling.tiling_mode = I915_TILING_X;
		set_tiling.stride = stride;
		if (ioctl(fd, DRM_IOCTL_I915_GEM_SET_TILING, &set_tiling)) {
			fprintf(stderr, "set tiling failed: %s\n",
				strerror(errno));
			return NULL;
		}
	}

	return cairo_image_surface_create_for_data
		(gem_mmap(fd, *handle, size, PROT_READ | PROT_WRITE),
		 format, width, height, stride);
}

enum corner {
	topleft,
	topright,
	bottomleft,
	bottomright,
};

static void
paint_color_gradient(cairo_t *cr, int x, int y, int width, int height,
		     int r, int g, int b)
{
	cairo_pattern_t *pat;

	pat = cairo_pattern_create_linear(x, y, x + width, y + height);
	cairo_pattern_add_color_stop_rgba(pat, 1, 0, 0, 0, 1);
	cairo_pattern_add_color_stop_rgba(pat, 0, r, g, b, 1);

	cairo_rectangle(cr, x, y, width, height);
	cairo_set_source(cr, pat);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);
}

static void
paint_test_patterns(cairo_t *cr, int width, int height, int stride)
{
	double gr_height, gr_width;
	int x, y;

	y = height * 0.10;
	gr_width = width * 0.75;
	gr_height = height * 0.08;
	x = (width / 2) - (gr_width / 2);

	paint_color_gradient(cr, x, y, gr_width, gr_height, 1, 0, 0);

	y += gr_height;
	paint_color_gradient(cr, x, y, gr_width, gr_height, 0, 1, 0);

	y += gr_height;
	paint_color_gradient(cr, x, y, gr_width, gr_height, 0, 0, 1);

	y += gr_height;
	paint_color_gradient(cr, x, y, gr_width, gr_height, 1, 1, 1);
}

static void
paint_marker(cairo_t *cr, int x, int y, char *str, enum corner text_location)
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

static void
paint_output_info(cairo_t *cr, struct connector *c, int width, int height)
{
	cairo_text_extents_t name_extents, mode_extents;
	char name_buf[128], mode_buf[128];
	int i, x, y, modes_x, modes_y;

	/* Get text extents for each string */
	snprintf(name_buf, sizeof name_buf, "%s",
		 connector_type_str(c->connector->connector_type));
	cairo_set_font_size(cr, 48);
	cairo_select_font_face(cr, "Helvetica",
			       CAIRO_FONT_SLANT_NORMAL,
			       CAIRO_FONT_WEIGHT_NORMAL);
	cairo_text_extents(cr, name_buf, &name_extents);

	snprintf(mode_buf, sizeof mode_buf, "%s @ %dHz on %s encoder",
		 c->mode.name, c->mode.vrefresh,
		 encoder_type_str(c->encoder->encoder_type));
	cairo_set_font_size(cr, 36);
	cairo_text_extents(cr, mode_buf, &mode_extents);

	/* Paint output name */
	x = width / 2;
	x -= name_extents.width / 2;
	y = height / 2;
	y -= (name_extents.height / 2) - (mode_extents.height / 2) - 10;
	cairo_set_font_size(cr, 48);
	cairo_move_to(cr, x, y);
	cairo_text_path(cr, name_buf);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_fill(cr);

	/* Paint mode name */
	x = width / 2;
	x -= mode_extents.width / 2;
	modes_x = x;
	y = height / 2;
	y += (mode_extents.height / 2) + (name_extents.height / 2) + 10;
	cairo_set_font_size(cr, 36);
	cairo_move_to(cr, x, y);
	cairo_text_path(cr, mode_buf);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_fill(cr);

	/* List available modes */
	snprintf(mode_buf, sizeof mode_buf, "Available modes:");
	cairo_set_font_size(cr, 18);
	cairo_text_extents(cr, mode_buf, &mode_extents);
	x = modes_x;
	modes_x = x + mode_extents.width;
	y += mode_extents.height + 10;
	modes_y = y;
	cairo_move_to(cr, x, y);
	cairo_text_path(cr, mode_buf);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_fill(cr);

	for (i = 0; i < c->connector->count_modes; i++) {
		snprintf(mode_buf, sizeof mode_buf, "%s @ %dHz",
			 c->connector->modes[i].name,
			 c->connector->modes[i].vrefresh);
		cairo_set_font_size(cr, 18);
		cairo_text_extents(cr, mode_buf, &mode_extents);
		x = modes_x - mode_extents.width; /* right justify modes */
		y += mode_extents.height + 10;
		if (y + mode_extents.height >= height) {
			y = modes_y + mode_extents.height + 10;
			modes_x += mode_extents.width + 10;
			x = modes_x - mode_extents.width;
		}
		cairo_move_to(cr, x, y);
		cairo_text_path(cr, mode_buf);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_stroke_preserve(cr);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_fill(cr);
	}
}

#ifdef TEST_PLANES
static int
connector_find_plane(struct connector *c)
{
	drmModePlaneRes *plane_resources;
	drmModePlane *ovr;
	uint32_t id = 0;
	int i;

	plane_resources = drmModeGetPlaneResources(fd);
	if (!plane_resources) {
		fprintf(stderr, "drmModeGetPlaneResources failed: %s\n",
			strerror(errno));
		return 0;
	}

	for (i = 0; i < plane_resources->count_planes; i++) {
		ovr = drmModeGetPlane(fd, plane_resources->planes[i]);
		if (!ovr) {
			fprintf(stderr, "drmModeGetPlane failed: %s\n",
				strerror(errno));
			continue;
		}

		if (ovr->possible_crtcs & (1<<i)) {
			id = ovr->plane_id;
			drmModeFreePlane(ovr);
			break;
		}
		drmModeFreePlane(ovr);
	}

	return 4;
	return id;
}

static void
paint_plane(cairo_t *cr, int width, int height, int stride)
{
	double gr_height, gr_width;
	int x, y;

	y = height * 0.10;
	gr_width = width * 0.75;
	gr_height = height * 0.08;
	x = (width / 2) - (gr_width / 2);

	paint_color_gradient(cr, x, y, gr_width, gr_height, 1, 0, 0);

	y += gr_height;
	paint_color_gradient(cr, x, y, gr_width, gr_height, 0, 1, 0);

	y += gr_height;
	paint_color_gradient(cr, x, y, gr_width, gr_height, 0, 0, 1);

	y += gr_height;
	paint_color_gradient(cr, x, y, gr_width, gr_height, 1, 1, 1);
}

static void
enable_plane(struct connector *c)
{
	cairo_surface_t *surface;
	cairo_status_t status;
	cairo_t *cr;
	unsigned int fb_id;
	uint32_t handle, x, y, plane_id;
	int width, height;
	int ret;

	width = c->mode.hdisplay * 0.50;
	height = c->mode.vdisplay * 0.50;

	x = (c->mode.hdisplay - width) / 2;
	y = (c->mode.vdisplay - height) / 2;

	plane_id = connector_find_plane(c);
	if (!plane_id) {
		fprintf(stderr, "failed to find plane for crtc\n");
		return;
	}

	surface = allocate_surface(fd, width, height, 24, 32, &handle, 1);
	if (!surface) {
		fprintf(stderr, "allocation failed %dx%d\n", width, height);
		return;
	}

	cr = cairo_create(surface);

	paint_plane(cr, width, height,
		      cairo_image_surface_get_stride(surface));
	status = cairo_status(cr);
	cairo_destroy(cr);
	if (status)
		fprintf(stderr, "failed to draw plane %dx%d: %s\n",
			width, height, cairo_status_to_string(status));

	ret = drmModeAddFB2(fd, width, height, V4L2_PIX_FMT_RGB32, 24, 32,
			   cairo_image_surface_get_stride(surface),
			   handle, &fb_id);
	cairo_surface_destroy(surface);
	gem_close(fd, handle);

	if (ret) {
		fprintf(stderr, "failed to add fb (%dx%d): %s\n",
			width, height, strerror(errno));
		return;
	}

	if (drmModeSetPlane(fd, plane_id, c->crtc, fb_id, x, y, 0, 0)) {
		fprintf(stderr, "failed to enable plane: %s\n",
			strerror(errno));
		return;
	}
}

static void
disable_plane(struct connector *c)
{
	uint32_t plane_id;

	plane_id = connector_find_plane(c);
	if (!plane_id) {
		fprintf(stderr, "failed to find plane for crtc\n");
		return;
	}

	if (drmModeSetPlane(fd, plane_id, c->crtc, 0, 0, 0, 0, 0)) {
		fprintf(stderr, "failed to enable plane: %s\n",
			strerror(errno));
		return;
	}
}
#else
static void enable_plane(struct connector *c) { return; }
#endif

static void
set_mode(struct connector *c)
{
	unsigned int fb_id;
	int ret, width, height;
	char buf[128];
	int j, test_mode_num;
	uint32_t bpp = 32;

	if (depth <= 8)
		bpp = 8;
	else if (depth > 8 && depth <= 16)
		bpp = 16;
	else if (depth > 16 && depth <= 32)
		bpp = 32;

	connector_find_preferred_mode(c);
	if (!c->mode_valid)
		return;

	test_mode_num = 1;
	if (force_mode){
		c->mode.clock = force_clock*1000;
		c->mode.hdisplay = force_hdisplay;
		c->mode.hsync_start = force_hsync_start;
		c->mode.hsync_end = force_hsync_end;
		c->mode.htotal = force_htotal;
		c->mode.vdisplay = force_vdisplay;
		c->mode.vsync_start = force_vsync_start;
		c->mode.vsync_end = force_vsync_end;
		c->mode.vtotal = force_vtotal;
		c->mode.vrefresh =(force_clock*1e6)/(force_htotal*force_vtotal);
		c->mode_valid = 1;
		sprintf(c->mode.name, "%dx%d", force_hdisplay, force_vdisplay);
	} else if (test_all_modes)
		test_mode_num = c->connector->count_modes;

	for (j = 0; j < test_mode_num; j++) {
		cairo_surface_t *surface;
		cairo_status_t status;
		cairo_t *cr;
		uint32_t handle;

		if (test_all_modes)
			c->mode = c->connector->modes[j];

		if (!c->mode_valid)
			continue;

		width = c->mode.hdisplay;
		height = c->mode.vdisplay;

		surface = allocate_surface(fd, width, height, depth, bpp,
					   &handle, enable_tiling);
		if (!surface) {
			fprintf(stderr, "allocation failed %dx%d\n", width, height);
			continue;
		}

		cr = cairo_create(surface);

		paint_test_patterns(cr, width, height,
				    cairo_image_surface_get_stride(surface));

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

		/* Paint output info */
		paint_output_info(cr, c, width, height);

		status = cairo_status(cr);
		cairo_destroy(cr);
		if (status)
			fprintf(stderr, "failed to draw pretty picture %dx%d: %s\n",
				width, height, cairo_status_to_string(status));

		ret = drmModeAddFB(fd, width, height, depth, bpp,
				   cairo_image_surface_get_stride(surface),
				   handle, &fb_id);
		cairo_surface_destroy(surface);
		gem_close(fd, handle);

		if (ret) {
			fprintf(stderr, "failed to add fb (%dx%d): %s\n",
				width, height, strerror(errno));
			continue;
		}

		dump_mode(&c->mode);
		if (drmModeSetCrtc(fd, c->crtc, fb_id, 0, 0,
				   &c->id, 1, &c->mode)) {
			fprintf(stderr, "failed to set mode (%dx%d@%dHz): %s\n",
				width, height, c->mode.vrefresh,
				strerror(errno));
			continue;
		}

		if (test_plane)
			enable_plane(c);

		if (sleep_between_modes && test_all_modes)
			sleep(sleep_between_modes);
	}

	drmModeFreeEncoder(c->encoder);
	drmModeFreeConnector(c->connector);
}

/*
 * Re-probe outputs and light up as many as possible.
 *
 * On Intel, we have two CRTCs that we can drive independently with
 * different timings and scanout buffers.
 *
 * Each connector has a corresponding encoder, except in the SDVO case
 * where an encoder may have multiple connectors.
 */
static void update_display(void)
{
	struct connector *connectors;
	int c;

	resources = drmModeGetResources(fd);
	if (!resources) {
		fprintf(stderr, "drmModeGetResources failed: %s\n",
			strerror(errno));
		return;
	}

	connectors = calloc(resources->count_connectors,
			    sizeof(struct connector));
	if (!connectors)
		return;

	if (dump_info) {
		dump_connectors();
		dump_crtcs();
		dump_planes();
	}

	if (test_preferred_mode || test_all_modes || force_mode) {
		/* Find any connected displays */
		for (c = 0; c < resources->count_connectors; c++) {
			connectors[c].id = resources->connectors[c];
			set_mode(&connectors[c]);
		}
	}
	drmModeFreeResources(resources);
	if (dump_info || test_all_modes)
		exit(0);
}

extern char *optarg;
extern int optind, opterr, optopt;
static char optstr[] = "hiaf:s:d:pt";

static void usage(char *name)
{
	fprintf(stderr, "usage: %s [-hiafs]\n", name);
	fprintf(stderr, "\t-i\tdump info\n");
	fprintf(stderr, "\t-a\ttest all modes\n");
	fprintf(stderr, "\t-s\t<duration>\tsleep between each mode test\n");
	fprintf(stderr, "\t-d\t<depth>\tbit depth of scanout buffer\n");
	fprintf(stderr, "\t-p\ttest overlay plane\n");
	fprintf(stderr, "\t-t\tuse a tiled framebuffer\n");
	fprintf(stderr, "\t-f\t<clock MHz>,<hdisp>,<hsync-start>,<hsync-end>,<htotal>,\n");
	fprintf(stderr, "\t\t<vdisp>,<vsync-start>,<vsync-end>,<vtotal>\n");
	fprintf(stderr, "\t\ttest force mode\n");
	fprintf(stderr, "\tDefault is to test the preferred mode.\n");
	exit(0);
}

#define dump_resource(res) if (res) dump_##res()

static gboolean hotplug_event(GIOChannel *source, GIOCondition condition,
			      gpointer data)
{
	struct udev_device *dev;
	dev_t udev_devnum;
	struct stat s;
	const char *hotplug;

	dev = udev_monitor_receive_device(uevent_monitor);
	if (!dev)
		goto out;

	udev_devnum = udev_device_get_devnum(dev);
	fstat(fd, &s);

	hotplug = udev_device_get_property_value(dev, "HOTPLUG");

	if (memcmp(&s.st_rdev, &udev_devnum, sizeof(dev_t)) == 0 &&
	    hotplug && atoi(hotplug) == 1)
		update_display();

	udev_device_unref(dev);
out:
	return TRUE;
}

static gboolean input_event(GIOChannel *source, GIOCondition condition,
			    gpointer data)
{
	gchar buf[256];
	gsize count;

	count = read(g_io_channel_unix_get_fd(source), buf, sizeof(buf));
	if (buf[0] == 'q' && (count == 1 || buf[1] == '\n'))
		exit(0);

	return TRUE;
}

int main(int argc, char **argv)
{
	int c;
	int encoders = 0, connectors = 0, crtcs = 0, framebuffers = 0;
	char *modules[] = { "i915" };
	unsigned int i;
	struct udev *u;
	int ret = 0;
	GIOChannel *udevchannel, *stdinchannel;
	GMainLoop *mainloop;

	opterr = 0;
	while ((c = getopt(argc, argv, optstr)) != -1) {
		switch (c) {
		case 'i':
			dump_info = 1;
			encoders = connectors = crtcs = modes = framebuffers = 1;
			break;
		case 'a':
			test_all_modes = 1;
			break;
		case 'f':
			force_mode = 1;
			if(sscanf(optarg,"%f,%d,%d,%d,%d,%d,%d,%d,%d",
				&force_clock,&force_hdisplay, &force_hsync_start,&force_hsync_end,&force_htotal,
				&force_vdisplay, &force_vsync_start, &force_vsync_end, &force_vtotal)!= 9)
				usage(argv[0]);
			break;
		case 's':
			sleep_between_modes = atoi(optarg);
			break;
		case 'd':
			depth = atoi(optarg);
			fprintf(stderr, "using depth %d\n", depth);
			break;
		case 'p':
			test_plane = 1;
			break;
		case 't':
			enable_tiling = 1;
			break;
		default:
			fprintf(stderr, "unknown option %c\n", c);
			/* fall through */
		case 'h':
			usage(argv[0]);
			break;
		}
	}
	if (!test_all_modes && !force_mode && !dump_info)
		test_preferred_mode = 1;

	for (i = 0; i < ARRAY_SIZE(modules); i++) {
		fd = drmOpen(modules[i], NULL);
		if (fd < 0)
			printf("failed to load %s driver.\n", modules[i]);
		else
			break;
	}

	if (i == ARRAY_SIZE(modules)) {
		fprintf(stderr, "failed to load any modules, aborting.\n");
		ret = -1;
		goto out;
	}

	u = udev_new();
	if (!u) {
		fprintf(stderr, "failed to create udev object\n");
		ret = -1;
		goto out_close;
	}

	uevent_monitor = udev_monitor_new_from_netlink(u, "udev");
	if (!uevent_monitor) {
		fprintf(stderr, "failed to create udev event monitor\n");
		ret = -1;
		goto out_udev_unref;
	}

	ret = udev_monitor_filter_add_match_subsystem_devtype(uevent_monitor,
							      "drm",
							      "drm_minor");
	if (ret < 0) {
		fprintf(stderr, "failed to filter for drm events\n");
		goto out_udev_mon_unref;
	}

	ret = udev_monitor_enable_receiving(uevent_monitor);
	if (ret < 0) {
		fprintf(stderr, "failed to enable udev event reception\n");
		goto out_udev_mon_unref;
	}

	mainloop = g_main_loop_new(NULL, FALSE);
	if (!mainloop) {
		fprintf(stderr, "failed to create glib mainloop\n");
		ret = -1;
		goto out_mainloop_unref;
	}

	udevchannel =
		g_io_channel_unix_new(udev_monitor_get_fd(uevent_monitor));
	if (!udevchannel) {
		fprintf(stderr, "failed to create udev GIO channel\n");
		goto out_mainloop_unref;
	}

	ret = g_io_add_watch(udevchannel, G_IO_IN | G_IO_ERR, hotplug_event,
			     u);
	if (ret < 0) {
		fprintf(stderr, "failed to add watch on udev GIO channel\n");
		goto out_udev_off;
	}

	stdinchannel = g_io_channel_unix_new(0);
	if (!stdinchannel) {
		fprintf(stderr, "failed to create stdin GIO channel\n");
		goto out_udev_off;
	}

	ret = g_io_add_watch(stdinchannel, G_IO_IN | G_IO_ERR, input_event,
			     NULL);
	if (ret < 0) {
		fprintf(stderr, "failed to add watch on stdin GIO channel\n");
		goto out_stdio_off;
	}

	update_display();
	g_main_loop_run(mainloop);

out_stdio_off:
	g_io_channel_shutdown(stdinchannel, TRUE, NULL);
out_udev_off:
	g_io_channel_shutdown(udevchannel, TRUE, NULL);
out_mainloop_unref:
	g_main_loop_unref(mainloop);
out_udev_mon_unref:
	udev_monitor_unref(uevent_monitor);
out_udev_unref:
	udev_unref(u);
out_close:
	drmClose(fd);
out:
	return ret;
}
