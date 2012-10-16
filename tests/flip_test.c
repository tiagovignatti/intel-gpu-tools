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
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_gpu_tools.h"

#define TEST_DPMS		(1 << 0)
#define TEST_WITH_DUMMY_LOAD	(1 << 1)
#define TEST_PAN		(1 << 2)
#define TEST_MODESET		(1 << 3)
#define TEST_CHECK_TS		(1 << 4)
#define TEST_EBUSY		(1 << 5)
#define TEST_EINVAL		(1 << 6)
#define TEST_FLIP		(1 << 7)

#define EVENT_FLIP		(1 << 0)

drmModeRes *resources;
int drm_fd;
static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
uint32_t devid;
int test_time = 3;

uint32_t *fb_ptr;

struct type_name {
	int type;
	const char *name;
};

struct event_state {
	const char *name;

	/*
	 * Event data for the last event that has already passed our check.
	 * Updated using the below current_* vars in update_state().
	 */
	struct timeval last_ts;			/* kernel reported timestamp */
	struct timeval last_received_ts;	/* the moment we received it */
	unsigned int last_seq;			/* kernel reported seq. num */

	/*
	 * Event data for for the current event that we just received and
	 * going to check for validity. Set in event_handler().
	 */
	struct timeval current_ts;		/* kernel reported timestamp */
	struct timeval current_received_ts;	/* the moment we received it */
	unsigned int current_seq;		/* kernel reported seq. num */

	int count;				/* # of events of this type */

	/* Step between the current and next 'target' sequence number. */
	int seq_step;
};

struct test_output {
	const char *test_name;
	uint32_t id;
	int mode_valid;
	drmModeModeInfo mode;
	drmModeEncoder *encoder;
	drmModeConnector *connector;
	int crtc;
	int pipe;
	int flags;
	unsigned int current_fb_id;
	unsigned int fb_width;
	unsigned int fb_height;
	unsigned int fb_ids[2];
	struct kmstest_fb fb_info[2];

	struct event_state flip_state;
	unsigned int pending_events;
};

static void emit_dummy_load(struct test_output *o)
{
	int i, limit;
	drm_intel_bo *dummy_bo, *target_bo, *tmp_bo;
	struct kmstest_fb *fb_info = &o->fb_info[o->current_fb_id];
	unsigned pitch = fb_info->stride;

	limit = intel_gen(devid) < 6 ? 500 : 5000;

	dummy_bo = drm_intel_bo_alloc(bufmgr, "dummy_bo", fb_info->size, 4096);
	assert(dummy_bo);
	target_bo = gem_handle_to_libdrm_bo(bufmgr, drm_fd, "imported", fb_info->gem_handle);
	assert(target_bo);

	for (i = 0; i < limit; i++) {
		BEGIN_BATCH(8);
		OUT_BATCH(XY_SRC_COPY_BLT_CMD |
			  XY_SRC_COPY_BLT_WRITE_ALPHA |
			  XY_SRC_COPY_BLT_WRITE_RGB);
		OUT_BATCH((3 << 24) | /* 32 bits */
			  (0xcc << 16) | /* copy ROP */
			  pitch);
		OUT_BATCH(0 << 16 | 0);
		OUT_BATCH((o->mode.vdisplay) << 16 | (o->mode.hdisplay));
		OUT_RELOC_FENCED(dummy_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
		OUT_BATCH(0 << 16 | 0);
		OUT_BATCH(pitch);
		OUT_RELOC_FENCED(target_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
		ADVANCE_BATCH();

		if (IS_GEN6(devid) || IS_GEN7(devid)) {
			BEGIN_BATCH(3);
			OUT_BATCH(XY_SETUP_CLIP_BLT_CMD);
			OUT_BATCH(0);
			OUT_BATCH(0);
			ADVANCE_BATCH();
		}

		tmp_bo = dummy_bo;
		dummy_bo = target_bo;
		target_bo = tmp_bo;
	}
	intel_batchbuffer_flush(batch);

	drm_intel_bo_unreference(dummy_bo);
	drm_intel_bo_unreference(target_bo);
}

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

static void set_flag(unsigned int *v, unsigned int flag)
{
	assert(!(*v & flag));
	*v |= flag;
}

static void clear_flag(unsigned int *v, unsigned int flag)
{
	assert(*v & flag);
	*v &= ~flag;
}

static int do_page_flip(struct test_output *o, int fb_id)
{
	int ret;

	ret = drmModePageFlip(drm_fd, o->crtc, fb_id, DRM_MODE_PAGE_FLIP_EVENT,
				o);
	if (ret == 0)
		set_flag(&o->pending_events, EVENT_FLIP);

	return ret;
}

static bool
analog_tv_connector(struct test_output *o)
{
	uint32_t connector_type = o->connector->connector_type;

	return connector_type == DRM_MODE_CONNECTOR_TV ||
		connector_type == DRM_MODE_CONNECTOR_9PinDIN ||
		connector_type == DRM_MODE_CONNECTOR_SVIDEO ||
		connector_type == DRM_MODE_CONNECTOR_Composite;
}

static void event_handler(struct event_state *es, unsigned int frame,
			  unsigned int sec, unsigned int usec)
{
	gettimeofday(&es->current_received_ts, NULL);
	es->current_ts.tv_sec = sec;
	es->current_ts.tv_usec = usec;
	es->current_seq = frame;
}

static void page_flip_handler(int fd, unsigned int frame, unsigned int sec,
			      unsigned int usec, void *data)
{
	struct test_output *o = data;

	clear_flag(&o->pending_events, EVENT_FLIP);
	event_handler(&o->flip_state, frame, sec, usec);
}

static void check_state(struct test_output *o, struct event_state *es)
{
	struct timeval diff;
	double usec_interflip;

	timersub(&es->current_ts, &es->current_received_ts, &diff);
	if (diff.tv_sec > 0 || (diff.tv_sec == 0 && diff.tv_usec > 2000)) {
		fprintf(stderr, "%s ts delayed for too long: %is, %iusec\n",
			es->name, (int)diff.tv_sec, (int)diff.tv_usec);
		exit(5);
	}

	if (!timercmp(&es->last_received_ts, &es->current_ts, <)) {
		fprintf(stderr, "%s ts before the %s was issued!\n",
				es->name, es->name);

		timersub(&es->current_ts, &es->last_received_ts, &diff);
		fprintf(stderr, "timerdiff %is, %ius\n",
			(int) diff.tv_sec, (int) diff.tv_usec);
		exit(6);
	}

	if (es->count == 0)
		return;

	/* This bounding matches the one in DRM_IOCTL_WAIT_VBLANK. */
	if (es->current_seq - (es->last_seq + es->seq_step) > 1UL << 23) {
		fprintf(stderr, "unexpected %s seq %u, should be >= %u\n",
			es->name, es->current_seq, es->last_seq + es->seq_step);
		exit(10);
	}

	if ((o->flags & TEST_CHECK_TS) && (!analog_tv_connector(o))) {
		timersub(&es->current_ts, &es->last_ts, &diff);
		usec_interflip = (double)es->seq_step /
				 ((double)o->mode.vrefresh) * 1000.0 * 1000.0;
		if (fabs((((double) diff.tv_usec) - usec_interflip) /
		    usec_interflip) > 0.005) {
			fprintf(stderr, "inter-%s ts jitter: %is, %ius\n",
				es->name,
				(int) diff.tv_sec, (int) diff.tv_usec);
			/* atm this is way too easy to hit, thanks to the hpd
			 * poll helper :( hence make it non-fatal for now */
			//exit(9);
		}

		if (es->current_seq != es->last_seq + es->seq_step)
			fprintf(stderr, "unexpected %s seq %u, expected %u\n",
					es->name, es->current_seq,
					es->last_seq + es->seq_step);
			/* no exit, due to the same reason as above */
	}
}

static void check_all_state(struct test_output *o,
			    unsigned int completed_events)
{
	if (completed_events & EVENT_FLIP)
		check_state(o, &o->flip_state);
}

/* Return mask of completed events. */
static unsigned int run_test_step(struct test_output *o)
{
	unsigned int new_fb_id;
	/* for funny reasons page_flip returns -EBUSY on disabled crtcs ... */
	int expected_einval = o->flags & TEST_MODESET ? -EBUSY : -EINVAL;
	unsigned int completed_events = 0;
	bool do_flip;

	do_flip = (o->flags & TEST_FLIP) && !(o->pending_events & EVENT_FLIP);

	if (o->flags & TEST_WITH_DUMMY_LOAD)
		emit_dummy_load(o);


	o->current_fb_id = !o->current_fb_id;
	new_fb_id = o->fb_ids[o->current_fb_id];

	if (do_flip && (o->flags & TEST_EINVAL) && o->flip_state.count > 0)
		assert(do_page_flip(o, new_fb_id) == expected_einval);

	if (o->flags & TEST_MODESET) {
		if (drmModeSetCrtc(drm_fd, o->crtc,
				   o->fb_ids[o->current_fb_id],
				   0, 0,
				   &o->id, 1, &o->mode)) {
			fprintf(stderr, "failed to restore output mode: %s\n",
				strerror(errno));
			exit(7);
		}
	}

	if (o->flags & TEST_DPMS)
		do_or_die(set_dpms(o, DRM_MODE_DPMS_ON));

	printf("."); fflush(stdout);

	if (do_flip)
		do_or_die(do_page_flip(o, new_fb_id));

	if (do_flip && (o->flags & TEST_EBUSY))
		assert(do_page_flip(o, new_fb_id) == -EBUSY);

	/* pan before the flip completes */
	if (o->flags & TEST_PAN) {
		int count = o->flip_state.count;
		int x_ofs = count * 10 > o->mode.hdisplay ?
			    o->mode.hdisplay : count * 10;

		if (drmModeSetCrtc(drm_fd, o->crtc, o->fb_ids[o->current_fb_id],
				   x_ofs, 0, &o->id, 1, &o->mode)) {
			fprintf(stderr, "failed to pan (%dx%d@%dHz): %s\n",
				o->fb_width, o->fb_height,
				o->mode.vrefresh, strerror(errno));
			exit(7);
		}
	}

	if (o->flags & TEST_DPMS)
		do_or_die(set_dpms(o, DRM_MODE_DPMS_OFF));

	if (o->flags & TEST_MODESET) {
		if (drmModeSetCrtc(drm_fd, o->crtc,
				   0, /* no fb */
				   0, 0,
				   NULL, 0, NULL)) {
			fprintf(stderr, "failed to disable output: %s\n",
				strerror(errno));
			exit(7);
		}
	}

	if (do_flip && (o->flags & TEST_EINVAL))
		assert(do_page_flip(o, new_fb_id) == expected_einval);

	return completed_events;
}

static void update_state(struct event_state *es)
{
	es->last_received_ts = es->current_received_ts;
	es->last_ts = es->current_ts;
	es->last_seq = es->current_seq;
	es->count++;
}

static void update_all_state(struct test_output *o,
			     unsigned int completed_events)
{
	if (completed_events & EVENT_FLIP)
		update_state(&o->flip_state);
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

static void check_final_state(struct test_output *o, struct event_state *es,
			      unsigned int ellapsed)
{
	/* Verify we drop no frames, but only if it's not a TV encoder, since
	 * those use some funny fake timings behind userspace's back. */
	if (o->flags & TEST_CHECK_TS && !analog_tv_connector(o)) {
		int expected;
		int count = es->count;

		count *= es->seq_step;
		expected = ellapsed * o->mode.vrefresh / (1000 * 1000);
		if (count < expected * 99/100) {
			fprintf(stderr, "dropped frames, expected %d, counted %d, encoder type %d\n",
				expected, count, o->encoder->encoder_type);
			exit(3);
		}
	}
}

/*
 * Wait until at least one pending event completes. Return mask of completed
 * events.
 */
static unsigned int wait_for_events(struct test_output *o)
{
	drmEventContext evctx;
	struct timeval timeout = { .tv_sec = 3, .tv_usec = 0 };
	fd_set fds;
	unsigned int event_mask;
	int ret;

	event_mask = o->pending_events;
	assert(event_mask);

	memset(&evctx, 0, sizeof evctx);
	evctx.version = DRM_EVENT_CONTEXT_VERSION;
	evctx.vblank_handler = NULL;
	evctx.page_flip_handler = page_flip_handler;

	/* make timeout lax with the dummy load */
	if (o->flags & TEST_WITH_DUMMY_LOAD)
		timeout.tv_sec *= 10;

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

	do_or_die(drmHandleEvent(drm_fd, &evctx));

	event_mask ^= o->pending_events;
	assert(event_mask);

	return event_mask;
}

/* Returned the ellapsed time in us */
static unsigned event_loop(struct test_output *o, unsigned duration_sec)
{
	struct timeval start, end;
	struct timeval tv_dur;

	gettimeofday(&start, NULL);
	end.tv_sec = start.tv_sec + duration_sec;
	end.tv_usec = start.tv_usec;

	while (1) {
		struct timeval now;
		unsigned int completed_events;

		completed_events = run_test_step(o);
		if (o->pending_events)
			completed_events |= wait_for_events(o);
		check_all_state(o, completed_events);
		update_all_state(o, completed_events);

		gettimeofday(&now, NULL);
		if (!timercmp(&now, &end, <))
			break;
	}

	gettimeofday(&end, NULL);
	timersub(&end, &start, &tv_dur);

	return tv_dur.tv_sec * 1000 * 1000 + tv_dur.tv_usec;
}

static void flip_mode(struct test_output *o, int crtc, int duration)
{
	int bpp = 32, depth = 24;
	unsigned ellapsed;

	connector_find_preferred_mode(o, crtc);
	if (!o->mode_valid)
		return;

	fprintf(stdout, "Beginning %s on crtc %d, connector %d\n",
		o->test_name, crtc, o->id);

	o->fb_width = o->mode.hdisplay;
	o->fb_height = o->mode.vdisplay;

	if (o->flags & TEST_PAN)
		o->fb_width *= 2;

	o->fb_ids[0] = kmstest_create_fb(drm_fd, o->fb_width, o->fb_height, bpp,
					 depth, false, &o->fb_info[0],
					 paint_flip_mode, (void *)false);
	o->fb_ids[1] = kmstest_create_fb(drm_fd, o->fb_width, o->fb_height, bpp,
					 depth, false, &o->fb_info[1],
					 paint_flip_mode, (void *)true);

	if (!o->fb_ids[0] || !o->fb_ids[1]) {
		fprintf(stderr, "failed to create fbs\n");
		exit(3);
	}

	kmstest_dump_mode(&o->mode);
	if (drmModeSetCrtc(drm_fd, o->crtc, o->fb_ids[0], 0, 0,
			   &o->id, 1, &o->mode)) {
		fprintf(stderr, "failed to set mode (%dx%d@%dHz): %s\n",
			o->fb_width, o->fb_height, o->mode.vrefresh,
			strerror(errno));
		exit(3);
	}
	assert(fb_is_bound(o, o->fb_ids[0]));

	/* quiescent the hw a bit so ensure we don't miss a single frame */
	if (o->flags & TEST_CHECK_TS)
		sleep(1);

	gettimeofday(&o->flip_state.last_ts, NULL);

	if (do_page_flip(o, o->fb_ids[1])) {
		fprintf(stderr, "failed to page flip: %s\n", strerror(errno));
		exit(4);
	}
	wait_for_events(o);

	o->current_fb_id = 1;
	o->flip_state.seq_step = 1;

	ellapsed = event_loop(o, duration);

	if (o->flags & TEST_FLIP)
		check_final_state(o, &o->flip_state, ellapsed);

	fprintf(stdout, "\n%s on crtc %d, connector %d: PASSED\n\n",
		o->test_name, crtc, o->id);

	kmstest_remove_fb(drm_fd, o->fb_ids[1]);
	kmstest_remove_fb(drm_fd, o->fb_ids[0]);

	drmModeFreeEncoder(o->encoder);
	drmModeFreeConnector(o->connector);
}

static int get_pipe_from_crtc_id(int crtc_id)
{
	struct drm_i915_get_pipe_from_crtc_id pfci;
	int ret;

	memset(&pfci, 0, sizeof(pfci));
	pfci.crtc_id = crtc_id;
	ret = drmIoctl(drm_fd, DRM_IOCTL_I915_GET_PIPE_FROM_CRTC_ID, &pfci);
	assert(ret == 0);

	return pfci.pipe;
}

static int run_test(int duration, int flags, const char *test_name)
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
		for (i = 0; i < resources->count_crtcs; i++) {
			int crtc;

			memset(&o, 0, sizeof(o));
			o.test_name = test_name;
			o.id = resources->connectors[c];
			o.flags = flags;
			o.flip_state.name = "flip";
			crtc = resources->crtcs[i];
			o.pipe = get_pipe_from_crtc_id(crtc);

			flip_mode(&o, crtc, duration);
		}
	}

	drmModeFreeResources(resources);
	return 1;
}

int main(int argc, char **argv)
{
	struct {
		int duration;
		int flags;
		const char *name;
	} tests[] = {
		{ 15, TEST_FLIP | TEST_CHECK_TS | TEST_EBUSY , "plain flip" },
		{ 30, TEST_FLIP | TEST_DPMS | TEST_EINVAL, "flip vs dpms" },
		{ 30, TEST_FLIP | TEST_DPMS | TEST_WITH_DUMMY_LOAD, "delayed flip vs dpms" },
		{ 5,  TEST_FLIP | TEST_PAN, "flip vs panning" },
		{ 30, TEST_FLIP | TEST_PAN | TEST_WITH_DUMMY_LOAD, "delayed flip vs panning" },
		{ 30, TEST_FLIP | TEST_MODESET | TEST_EINVAL, "flip vs modeset" },
		{ 30, TEST_FLIP | TEST_MODESET | TEST_WITH_DUMMY_LOAD, "delayed flip vs modeset" },
	};
	int i;

	drm_fd = drm_open_any();

	bufmgr = drm_intel_bufmgr_gem_init(drm_fd, 4096);
	devid = intel_get_drm_devid(drm_fd);
	batch = intel_batchbuffer_alloc(bufmgr, devid);

	for (i = 0; i < sizeof(tests) / sizeof (tests[0]); i++) {
		printf("running testcase: %s\n", tests[i].name);
		run_test(tests[i].duration, tests[i].flags, tests[i].name);
	}

	close(drm_fd);

	return 0;
}
