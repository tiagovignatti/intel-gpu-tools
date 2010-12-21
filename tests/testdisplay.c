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

#include "xf86drm.h"
#include "xf86drmMode.h"
#include "intel_bufmgr.h"
#include "i915_drm.h"

struct udev_monitor *uevent_monitor;
drmModeRes *resources;
int fd, modes;

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct type_name {
	int type;
	char *name;
};

#define type_name_fn(res) \
static char * res##_str(int type) {			\
	int i;						\
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
		if (c->mode.flags & DRM_MODE_TYPE_PREFERRED) {
			c->mode_valid = 1;
			break;
		}
	}

	if (!c->mode_valid) {
		fprintf(stderr, "failed to find any modes on connector %d\n",
			c->id);
		return;
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
		if (resources->crtcs[i])
			break;
	}
	c->crtc = resources->crtcs[i];
	resources->crtcs[i] = 0;
	c->connector = connector;
}

static drm_intel_bo *
allocate_buffer(drm_intel_bufmgr *bufmgr,
		int width, int height, int *stride)
{
	int size;

	/* Scan-out has a 64 byte alignment restriction */
	width *= 4; /* 32bpp */
	size = (width + 63) & -64;
	*stride = size;
	size *= height;

	return drm_intel_bo_alloc(bufmgr, "frontbuffer", size, 0);
}

enum corner {
	topleft,
	topright,
	bottomleft,
	bottomright,
};

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

static int
create_test_buffer(drm_intel_bufmgr *bufmgr, int width, int height,
		   int *stride_out, drm_intel_bo **bo_out)
{
	drm_intel_bo *bo;
	int ret, stride;

	bo = allocate_buffer(bufmgr, width, height, &stride);
	if (!bo) {
		fprintf(stderr, "failed to alloc buffer: %s\n",

			strerror(errno));
		return -1;
	}

	ret = drm_intel_gem_bo_map_gtt(bo);
	if (ret) {
		fprintf(stderr, "failed to GTT map buffer: %s\n",
			strerror(errno));
		return -1;
	}

	*bo_out = bo;
	*stride_out = stride;
	return 0;
}

static void
set_mode(struct connector *c)
{
	drm_intel_bufmgr *bufmgr;
	drm_intel_bo *bo;
	unsigned int fb_id;
	int ret, width, height, stride;
	cairo_surface_t *surface;
	cairo_t *cr;
	char buf[128];

	width = 0;
	height = 0;
	connector_find_preferred_mode(c);
	if (!c->mode_valid)
		return;

	width += c->mode.hdisplay;
	if (height < c->mode.vdisplay)
		height = c->mode.vdisplay;

	bufmgr = drm_intel_bufmgr_gem_init(fd, 2<<20);
	if (!bufmgr) {
		fprintf(stderr, "failed to init bufmgr: %s\n", strerror(errno));
		return;
	}

	if (create_test_buffer(bufmgr, width, height, &stride, &bo))
		return;

	surface = cairo_image_surface_create_for_data(bo->virtual,
						      CAIRO_FORMAT_ARGB32,
						      width, height,
						      stride);
	cr = cairo_create(surface);
	cairo_surface_destroy(surface);

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

	cairo_destroy(cr);
	drm_intel_gem_bo_unmap_gtt(bo);

	ret = drmModeAddFB(fd, width, height, 32, 32, stride, bo->handle,
			   &fb_id);
	if (ret) {
		fprintf(stderr, "failed to add fb: %s\n", strerror(errno));
		return;
	}

	if (!c->mode_valid)
		return;

	ret = drmModeSetCrtc(fd, c->crtc, fb_id, 0, 0,
			     &c->id, 1, &c->mode);

	if (ret) {
		fprintf(stderr, "failed to set mode: %s\n", strerror(errno));
		return;
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

	/* Find any connected displays */
	for (c = 0; c < resources->count_connectors; c++) {
		connectors[c].id = resources->connectors[c];
		set_mode(&connectors[c]);
	}
	drmModeFreeResources(resources);
}

extern char *optarg;
extern int optind, opterr, optopt;
static char optstr[] = "h";

static void usage(char *name)
{
	fprintf(stderr, "usage: %s [-h]\n", name);
	exit(0);
}

static gboolean hotplug_event(GIOChannel *source, GIOCondition condition,
			      gpointer data)
{
	gchar buf[256];
	gsize count;
	struct udev_device *dev;
	dev_t udev_devnum;
	struct stat s;
	const char *hotplug;

	g_io_channel_read(source, buf, 256, &count);

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

	g_io_channel_read(source, buf, 255, &count);
	buf[count] = '\0';

	if (buf[0] == 'q' && (count == 1 || buf[1] == '\n'))
		exit(0);

	return TRUE;
}

int main(int argc, char **argv)
{
	int c;
	int encoders = 0, connectors = 0, crtcs = 0, framebuffers = 0;
	char *modules[] = { "i915" };
	int i;
	struct udev *u;
	int ret = 0;
	GIOChannel *udevchannel, *stdinchannel;
	GMainLoop *mainloop;

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

	if (argc == 1)
		encoders = connectors = crtcs = modes = framebuffers = 1;

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
