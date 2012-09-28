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

#include "i915_drm.h"
#include "drmtest.h"
#include "testdisplay.h"

#define TEST_DPMS 1

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
	int flags;
	unsigned int current_fb_id;
	unsigned int fb_ids[2];
};

static int set_dpms(struct test_output *o, int mode)
{
	int i, dpms = 0;

	for (i = 0; i < o->connector->count_props; i++) {
		struct drm_mode_get_property prop;

		prop.prop_id = o->connector->props[i];
		prop.count_values = 0;
		prop.count_enum_blobs = 0;
		if (drmIoctl(drm_fd, DRM_IOCTL_MODE_GETPROPERTY, &prop))
			continue;

		if (strcmp(prop.name, "DPMS"))
			continue;

		dpms = prop.prop_id;
		break;
	}
	if (!dpms) {
		fprintf(stderr, "DPMS property not found on %d\n", o->id);
		errno = ENOENT;
		return -1;
	}

	return drmModeConnectorSetProperty(drm_fd, o->id, dpms, mode);
}

static void page_flip_handler(int fd, unsigned int frame, unsigned int sec,
			      unsigned int usec, void *data)
{
	struct test_output *o = data;
	unsigned int new_fb_id;

	o->current_fb_id = !o->current_fb_id;
	new_fb_id = o->fb_ids[o->current_fb_id];

	printf("."); fflush(stdout);
	if (o->flags & TEST_DPMS)
		do_or_die(set_dpms(o, 0));

	do_or_die(drmModePageFlip(drm_fd, o->crtc, new_fb_id,
				  DRM_MODE_PAGE_FLIP_EVENT, o));

	if (o->flags & TEST_DPMS)
		do_or_die(set_dpms(o, 3));
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
	assert(connector);

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
		o->mode_valid = 0;
		return;
	}

	o->connector = connector;
}

static void
paint_flip_mode(cairo_t *cr, int width, int height, void *priv)
{
	bool odd_frame = (bool) priv;

	if (odd_frame)
		cairo_rectangle(cr, width/4, height/2, width/4, height/8);
	else
		cairo_rectangle(cr, width/2, height/2, width/4, height/8);

	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_fill(cr);
}

static int
fb_is_bound(struct test_output *o, int fb)
{
	struct drm_mode_crtc mode;

	mode.crtc_id = o->crtc;
	if (drmIoctl(drm_fd, DRM_IOCTL_MODE_GETCRTC, &mode))
		return 0;

	return mode.mode_valid && mode.fb_id == fb;
}

static void flip_mode(struct test_output *o, int crtc, int duration)
{
	int ret;
	int bpp = 32, depth = 24;
	drmEventContext evctx;
	int width, height;
	struct timeval end;
	struct kmstest_fb fb_info[2];

	connector_find_preferred_mode(o, crtc);
	if (!o->mode_valid)
		return;

	fprintf(stdout, "Beginning page flipping on crtc %d, connector %d\n",
		crtc, o->id);

	width = o->mode.hdisplay;
	height = o->mode.vdisplay;

	o->fb_ids[0] = kmstest_create_fb(drm_fd, width, height, bpp, depth,
					 false, &fb_info[0],
					 paint_flip_mode, (void *)false);
	o->fb_ids[1] = kmstest_create_fb(drm_fd, width, height, bpp, depth,
					 false, &fb_info[1],
					 paint_flip_mode, (void *)true);
	if (!o->fb_ids[0] || !o->fb_ids[1]) {
		fprintf(stderr, "failed to create fbs\n");
		exit(3);
	}

	gem_close(drm_fd, fb_info[0].gem_handle);
	gem_close(drm_fd, fb_info[1].gem_handle);

	kmstest_dump_mode(&o->mode);
	if (drmModeSetCrtc(drm_fd, o->crtc, o->fb_ids[0], 0, 0,
			   &o->id, 1, &o->mode)) {
		fprintf(stderr, "failed to set mode (%dx%d@%dHz): %s\n",
			width, height, o->mode.vrefresh,
			strerror(errno));
		exit(3);
	}
	assert(fb_is_bound(o, o->fb_ids[0]));

	if (drmModePageFlip(drm_fd, o->crtc, o->fb_ids[1],
			      DRM_MODE_PAGE_FLIP_EVENT, o)) {
		fprintf(stderr, "failed to page flip: %s\n", strerror(errno));
		exit(4);
	}
	o->current_fb_id = 1;

	memset(&evctx, 0, sizeof evctx);
	evctx.version = DRM_EVENT_CONTEXT_VERSION;
	evctx.vblank_handler = NULL;
	evctx.page_flip_handler = page_flip_handler;

	gettimeofday(&end, NULL);
	end.tv_sec += duration;

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
			exit(1);
		} else if (FD_ISSET(0, &fds)) {
			fprintf(stderr, "no fds active, breaking\n");
			exit(2);
		}

		gettimeofday(&now, NULL);
		if (now.tv_sec > end.tv_sec ||
		    (now.tv_sec == end.tv_sec && now.tv_usec >= end.tv_usec)) {
			break;
		}

		drmHandleEvent(drm_fd, &evctx);
	}

	/* and drain the event queue */
	evctx.page_flip_handler = NULL;
	drmHandleEvent(drm_fd, &evctx);

	fprintf(stdout, "\npage flipping on crtc %d, connector %d: PASSED\n",
		crtc, o->id);

	drmModeFreeEncoder(o->encoder);
	drmModeFreeConnector(o->connector);
}

static int run_test(int duration, int flags)
{
	struct test_output o;
	int c, i;

	resources = drmModeGetResources(drm_fd);
	if (!resources) {
		fprintf(stderr, "drmModeGetResources failed: %s\n",
			strerror(errno));
		exit(5);
	}

	/* Find any connected displays */
	for (c = 0; c < resources->count_connectors; c++) {
		memset(&o, 0, sizeof(o));
		o.id = resources->connectors[c];
		o.flags = flags;
		for (i = 0; i < resources->count_crtcs; i++)
			flip_mode(&o, resources->crtcs[i], duration);
	}

	drmModeFreeResources(resources);
	return 1;
}

int main(int argc, char **argv)
{
	struct {
		int duration;
		int flags;
	} tests[] = {
		{ 5, 0 },
		{ 30, TEST_DPMS },
	};
	int i;

	drm_fd = drm_open_any();

	for (i = 0; i < sizeof(tests) / sizeof (tests[0]); i++)
		run_test(tests[i].duration, tests[i].flags);

	close(drm_fd);

	return 0;
}
