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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/kd.h>

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
#define TEST_VBLANK		(1 << 8)
#define TEST_VBLANK_BLOCK	(1 << 9)
#define TEST_VBLANK_ABSOLUTE	(1 << 10)
#define TEST_VBLANK_EXPIRED_SEQ	(1 << 11)
#define TEST_FB_RECREATE	(1 << 12)
#define TEST_RMFB		(1 << 13)
#define TEST_HANG		(1 << 14)
#define TEST_NOEVENT		(1 << 15)
#define TEST_FB_BAD_TILING	(1 << 16)
#define TEST_SINGLE_BUFFER	(1 << 17)
#define TEST_DPMS_OFF		(1 << 18)

#define EVENT_FLIP		(1 << 0)
#define EVENT_VBLANK		(1 << 1)

#ifndef DRM_CAP_TIMESTAMP_MONOTONIC
#define DRM_CAP_TIMESTAMP_MONOTONIC 6
#endif

drmModeRes *resources;
int drm_fd;
static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
uint32_t devid;
int test_time = 3;
static bool monotonic_timestamp;

static drmModeConnector *last_connector;

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
	unsigned int fb_ids[3];
	int bpp, depth;
	struct kmstest_fb fb_info[3];

	struct event_state flip_state;
	struct event_state vblank_state;
	unsigned int pending_events;
};


static unsigned long gettime_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

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

static int set_connector_dpms(drmModeConnector *connector, int mode)
{
	int i, dpms = 0;

	for (i = 0; i < connector->count_props; i++) {
		struct drm_mode_get_property prop;

		prop.prop_id = connector->props[i];
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
		fprintf(stderr, "DPMS property not found on %d\n",
			connector->connector_id);
		errno = ENOENT;
		return -1;
	}

	return drmModeConnectorSetProperty(drm_fd, connector->connector_id,
					   dpms, mode);
}

static int set_dpms(struct test_output *o, int mode)
{
	return set_connector_dpms(o->connector, mode);
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

static int do_page_flip(struct test_output *o, int fb_id, bool event)
{
	int ret;

	ret = drmModePageFlip(drm_fd, o->crtc, fb_id, event ? DRM_MODE_PAGE_FLIP_EVENT : 0,
				event ? o : NULL);
	if (ret == 0 && event)
		set_flag(&o->pending_events, EVENT_FLIP);

	return ret;
}

struct vblank_reply {
	unsigned int sequence;
	struct timeval ts;
};

static int __wait_for_vblank(unsigned int flags, int crtc_idx,
			      int target_seq, unsigned long ret_data,
			      struct vblank_reply *reply)
{
	drmVBlank wait_vbl;
	int ret;
	unsigned crtc_idx_mask;
	bool event = !(flags & TEST_VBLANK_BLOCK);

	memset(&wait_vbl, 0, sizeof(wait_vbl));

	crtc_idx_mask = crtc_idx << DRM_VBLANK_HIGH_CRTC_SHIFT;
	assert(!(crtc_idx_mask & ~DRM_VBLANK_HIGH_CRTC_MASK));

	wait_vbl.request.type = crtc_idx_mask;
	if (flags & TEST_VBLANK_ABSOLUTE)
		wait_vbl.request.type |= DRM_VBLANK_ABSOLUTE;
	else
		wait_vbl.request.type |= DRM_VBLANK_RELATIVE;
	if (event) {
		wait_vbl.request.type |= DRM_VBLANK_EVENT;
		wait_vbl.request.signal = ret_data;
	}
	wait_vbl.request.sequence = target_seq;

	ret = drmWaitVBlank(drm_fd, &wait_vbl);

	if (ret == 0) {
		reply->ts.tv_sec = wait_vbl.reply.tval_sec;
		reply->ts.tv_usec = wait_vbl.reply.tval_usec;
		reply->sequence = wait_vbl.reply.sequence;
	} else
		ret = -errno;

	return ret;
}

static int do_wait_for_vblank(struct test_output *o, int pipe_id,
			      int target_seq, struct vblank_reply *reply)
{
	int ret;

	ret = __wait_for_vblank(o->flags, pipe_id, target_seq, (unsigned long)o,
				reply);
	if (ret == 0 && !(o->flags & TEST_VBLANK_BLOCK))
		set_flag(&o->pending_events, EVENT_VBLANK);

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
	struct timeval now;

	if (monotonic_timestamp) {
		struct timespec ts;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		now.tv_sec = ts.tv_sec;
		now.tv_usec = ts.tv_nsec / 1000;
	} else {
		gettimeofday(&now, NULL);
	}
	es->current_received_ts = now;

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

static double frame_time(struct test_output *o)
{
	return 1000.0 * 1000.0 / o->mode.vrefresh;
}

static void fixup_premature_vblank_ts(struct test_output *o,
				      struct event_state *es)
{
	/*
	 * In case a power off event preempts the completion of a
	 * wait-for-vblank event the kernel will return a wf-vblank event with
	 * a zeroed-out timestamp. In order that check_state() doesn't
	 * complain replace this ts with a valid ts. As we can't calculate the
	 * exact timestamp, just use the time we received the event.
	 */
	struct timeval tv;

	if (!(o->flags & (TEST_DPMS | TEST_MODESET)))
		return;

	if (o->vblank_state.current_ts.tv_sec != 0 ||
	    o->vblank_state.current_ts.tv_usec != 0)
		return;

	tv.tv_sec = 0;
	tv.tv_usec = 1;
	timersub(&es->current_received_ts, &tv, &es->current_ts);
}

static void vblank_handler(int fd, unsigned int frame, unsigned int sec,
			      unsigned int usec, void *data)
{
	struct test_output *o = data;

	clear_flag(&o->pending_events, EVENT_VBLANK);
	event_handler(&o->vblank_state, frame, sec, usec);
	fixup_premature_vblank_ts(o, &o->vblank_state);
}

static void check_state(struct test_output *o, struct event_state *es)
{
	struct timeval diff;
	double usec_interflip;

	timersub(&es->current_ts, &es->current_received_ts, &diff);
	if ((!analog_tv_connector(o)) &&
	    (diff.tv_sec > 0 || (diff.tv_sec == 0 && diff.tv_usec > 2000))) {
		fprintf(stderr, "%s ts delayed for too long: %is, %iusec\n",
			es->name, (int)diff.tv_sec, (int)diff.tv_usec);
		exit(5);
	}

	if (es->count == 0)
		return;

	if (!timercmp(&es->last_received_ts, &es->current_ts, <)) {
		fprintf(stderr, "%s ts before the %s was issued!\n",
				es->name, es->name);

		timersub(&es->current_ts, &es->last_received_ts, &diff);
		fprintf(stderr, "timerdiff %is, %ius\n",
			(int) diff.tv_sec, (int) diff.tv_usec);
		exit(6);
	}

	/* This bounding matches the one in DRM_IOCTL_WAIT_VBLANK. */
	if (!(o->flags & (TEST_DPMS | TEST_MODESET))) {
		/* check only valid if no modeset happens in between, that
		 * increments by (1 << 23) on each step. */
		if (es->current_seq - (es->last_seq + es->seq_step) > 1UL << 23) {
			fprintf(stderr, "unexpected %s seq %u, should be >= %u\n",
				es->name, es->current_seq, es->last_seq + es->seq_step);
			exit(10);
		}
	}

	if ((o->flags & TEST_CHECK_TS) && (!analog_tv_connector(o))) {
		timersub(&es->current_ts, &es->last_ts, &diff);
		usec_interflip = (double)es->seq_step * frame_time(o);
		if (fabs((((double) diff.tv_usec) - usec_interflip) /
		    usec_interflip) > 0.005) {
			fprintf(stderr, "inter-%s ts jitter: %is, %ius\n",
				es->name,
				(int) diff.tv_sec, (int) diff.tv_usec);
			exit(9);
		}

		if (es->current_seq != es->last_seq + es->seq_step) {
			fprintf(stderr, "unexpected %s seq %u, expected %u\n",
					es->name, es->current_seq,
					es->last_seq + es->seq_step);
			exit(9);
		}
	}
}

static void check_state_correlation(struct test_output *o,
				    struct event_state *es1,
				    struct event_state *es2)
{
	struct timeval tv_diff;
	double ftime;
	double usec_diff;
	int seq_diff;

	if (es1->count == 0 || es2->count == 0)
		return;

	timersub(&es2->current_ts, &es1->current_ts, &tv_diff);
	usec_diff = tv_diff.tv_sec * 1000 * 1000 + tv_diff.tv_usec;

	seq_diff = es2->current_seq - es1->current_seq;
	ftime = frame_time(o);
	usec_diff -= seq_diff * ftime;

	if (fabs(usec_diff) / ftime > 0.005) {
		fprintf(stderr,
			"timestamp mismatch between %s and %s (diff %.4f sec)\n",
			es1->name, es2->name, usec_diff / 1000 / 1000);
		exit(14);
	}
}

static void check_all_state(struct test_output *o,
			    unsigned int completed_events)
{
	bool flip, vblank;

	flip = completed_events & EVENT_FLIP;
	vblank = completed_events & EVENT_VBLANK;

	if (flip)
		check_state(o, &o->flip_state);
	if (vblank)
		check_state(o, &o->vblank_state);

	if (flip && vblank)
		check_state_correlation(o, &o->flip_state, &o->vblank_state);
}

static void recreate_fb(struct test_output *o)
{
	drmModeFBPtr r;
	struct kmstest_fb *fb_info = &o->fb_info[o->current_fb_id];
	uint32_t new_fb_id;

	/* Call rmfb/getfb/addfb to ensure those don't introduce stalls */
	r = drmModeGetFB(drm_fd, fb_info->fb_id);
	assert(r);

	do_or_die(drmModeAddFB(drm_fd, o->fb_width, o->fb_height, o->depth,
			       o->bpp, fb_info->stride,
			       r->handle, &new_fb_id));

	gem_close(drm_fd, r->handle);
	drmFree(r);
	do_or_die(drmModeRmFB(drm_fd, fb_info->fb_id));

	o->fb_ids[o->current_fb_id] = new_fb_id;
	o->fb_info[o->current_fb_id].fb_id = new_fb_id;
}

static void set_y_tiling(struct test_output *o, int fb_idx)
{
	drmModeFBPtr r;
	struct kmstest_fb *fb_info = &o->fb_info[fb_idx];

	/* Call rmfb/getfb/addfb to ensure those don't introduce stalls */
	r = drmModeGetFB(drm_fd, fb_info->fb_id);
	assert(r);
	gem_set_tiling(drm_fd, r->handle, I915_TILING_Y, fb_info->stride);
	gem_close(drm_fd, r->handle);
	drmFree(r);
}


static int exec_nop(int fd, uint32_t handle)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];
	uint32_t b[2] = {MI_BATCH_BUFFER_END};
	int r;

	gem_write(fd, handle, 0, b, sizeof(b));

	gem_exec[0].handle = handle;
	gem_exec[0].relocation_count = 0;
	gem_exec[0].relocs_ptr = 0;
	gem_exec[0].alignment = 0;
	gem_exec[0].offset = 0;
	gem_exec[0].flags = 0;
	gem_exec[0].rsvd1 = 0;
	gem_exec[0].rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = 8;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags =  I915_EXEC_RENDER;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	r = drmIoctl(fd,
			DRM_IOCTL_I915_GEM_EXECBUFFER2,
			&execbuf);
	if (r)
		fprintf(stderr, "failed to exec: %s\n",
			strerror(errno));
	return r;
}

static void eat_error_state(struct test_output *o)
{
	static const char dfs_base[] = "/sys/kernel/debug/dri";
	static const char dfs_entry_error[] = "i915_error_state";
	static const char dfs_entry_stop[] = "i915_ring_stop";
	static const char data[] = "";
	static char tmp[128];
	char fname[FILENAME_MAX];
	int card_index = drm_get_card(0);
	int fd;
	ssize_t r;

	assert(card_index != -1);

	/* clear the error state */
	snprintf(fname, FILENAME_MAX, "%s/%i/%s",
		 dfs_base, card_index, dfs_entry_error);

	fd = open(fname, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "failed to open '%s': %s\n",
			fname, strerror(errno));
		return;
	}

	r = write(fd, data, sizeof data);
	if (r < 0)
		fprintf(stderr, "failed to write '%s': %s\n",
			fname, strerror(errno));
	close(fd);

	/* and check whether stop_rings is not reset, i.e. the hang has indeed
	 * happened */
	snprintf(fname, FILENAME_MAX, "%s/%i/%s",
		 dfs_base, card_index, dfs_entry_stop);

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "failed to open '%s': %s\n",
			fname, strerror(errno));
		return;
	}

	r = read(fd, tmp, sizeof tmp);
	if (r < 0)
		fprintf(stderr, "failed to read '%s': %s\n",
			fname, strerror(errno));

	if (atoi(tmp) != 0) {
		fprintf(stderr, "no gpu hang detected, stop_rings is still %s\n", tmp);
		exit(20);
	}

	close(fd);
}

static void hang_gpu(struct test_output *o)
{
	static const char dfs_base[] = "/sys/kernel/debug/dri";
	static const char dfs_entry[] = "i915_ring_stop";
	static const char data[] = "0xf";
	char fname[FILENAME_MAX];
	int card_index = drm_get_card(0);
	int fd;
	ssize_t r;

	assert(card_index != -1);

	snprintf(fname, FILENAME_MAX, "%s/%i/%s",
		 dfs_base, card_index, dfs_entry);

	fd = open(fname, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "failed to open '%s': %s\n",
			fname, strerror(errno));
		return;
	}

	r = write(fd, data, sizeof data);
	if (r < 0)
		fprintf(stderr, "failed to write '%s': %s\n",
			fname, strerror(errno));

	close(fd);
}

/* Return mask of completed events. */
static unsigned int run_test_step(struct test_output *o)
{
	unsigned int new_fb_id;
	/* for funny reasons page_flip returns -EBUSY on disabled crtcs ... */
	int expected_einval = o->flags & TEST_MODESET ? -EBUSY : -EINVAL;
	unsigned int completed_events = 0;
	bool do_flip;
	bool do_vblank;
	struct vblank_reply vbl_reply;
	unsigned int target_seq;
	uint32_t handle = 0;	/* Suppress GCC warning */

	target_seq = o->vblank_state.seq_step;
	if (o->flags & TEST_VBLANK_ABSOLUTE)
		target_seq += o->vblank_state.last_seq;

	/*
	 * It's possible that we don't have a pending flip here, in case both
	 * wf-vblank and flip were scheduled and the wf-vblank event was
	 * delivered earlier. The same applies to vblank events w.r.t flip.
	 */
	do_flip = (o->flags & TEST_FLIP) && !(o->pending_events & EVENT_FLIP);
	do_vblank = (o->flags & TEST_VBLANK) &&
		    !(o->pending_events & EVENT_VBLANK);

	if (o->flags & TEST_WITH_DUMMY_LOAD)
		emit_dummy_load(o);


	if (!(o->flags & TEST_SINGLE_BUFFER))
		o->current_fb_id = !o->current_fb_id;

	if (o->flags & TEST_FB_RECREATE)
		recreate_fb(o);
	new_fb_id = o->fb_ids[o->current_fb_id];

	if (o->flags & TEST_FB_BAD_TILING)
		new_fb_id = o->fb_ids[2];

	if ((o->flags & TEST_VBLANK_EXPIRED_SEQ) &&
	    !(o->pending_events & EVENT_VBLANK) && o->flip_state.count > 0) {
		struct vblank_reply reply;
		unsigned int exp_seq;
		unsigned long start;

		exp_seq = o->flip_state.current_seq;
		start = gettime_us();
		do_or_die(__wait_for_vblank(TEST_VBLANK_ABSOLUTE |
					    TEST_VBLANK_BLOCK, o->pipe, exp_seq,
					    0, &reply));
		assert(gettime_us() - start < 500);
		assert(reply.sequence == exp_seq);
		assert(timercmp(&reply.ts, &o->flip_state.last_ts, ==));
	}

	if (do_flip && (o->flags & TEST_EINVAL) && o->flip_state.count > 0)
		assert(do_page_flip(o, new_fb_id, true) == expected_einval);

	if (o->flags & TEST_FB_BAD_TILING)
		new_fb_id = o->fb_ids[o->current_fb_id];

	if (do_vblank && (o->flags & TEST_EINVAL) && o->vblank_state.count > 0)
		assert(do_wait_for_vblank(o, o->pipe, target_seq, &vbl_reply)
		       == -EINVAL);

	if (o->flags & TEST_DPMS_OFF)
		do_or_die(set_dpms(o, DRM_MODE_DPMS_OFF));

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

	if (do_flip && (o->flags & TEST_HANG)) {
		handle = gem_create(drm_fd, 4096);
		hang_gpu(o);
		exec_nop(drm_fd, handle);
	}

	if (do_flip)
		do_or_die(do_page_flip(o, new_fb_id, !(o->flags & TEST_NOEVENT)));

	if (do_vblank) {
		do_or_die(do_wait_for_vblank(o, o->pipe, target_seq,
					     &vbl_reply));
		if (o->flags & TEST_VBLANK_BLOCK) {
			event_handler(&o->vblank_state, vbl_reply.sequence,
				      vbl_reply.ts.tv_sec,
				      vbl_reply.ts.tv_usec);
			completed_events = EVENT_VBLANK;
		}
	}

	if (do_flip && (o->flags & TEST_EBUSY))
		assert(do_page_flip(o, new_fb_id, true) == -EBUSY);

	if (do_flip && (o->flags & TEST_RMFB))
		recreate_fb(o);

	/* pan before the flip completes */
	if (o->flags & TEST_PAN) {
		int count = do_flip ?
			o->flip_state.count : o->vblank_state.count;
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

	if (o->flags & TEST_MODESET && !(o->flags & TEST_RMFB)) {
		if (drmModeSetCrtc(drm_fd, o->crtc,
				   0, /* no fb */
				   0, 0,
				   NULL, 0, NULL)) {
			fprintf(stderr, "failed to disable output: %s\n",
				strerror(errno));
			exit(7);
		}
	}

	if (do_vblank && (o->flags & TEST_EINVAL) && o->vblank_state.count > 0)
		assert(do_wait_for_vblank(o, o->pipe, target_seq, &vbl_reply)
		       == -EINVAL);

	if (do_flip && (o->flags & TEST_EINVAL) && !(o->flags & TEST_FB_BAD_TILING))
		assert(do_page_flip(o, new_fb_id, true) == expected_einval);

	if (do_flip && (o->flags & TEST_HANG)) {
		gem_sync(drm_fd, handle);
		gem_close(drm_fd, handle);
		eat_error_state(o);
	}

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

	if (completed_events & EVENT_VBLANK)
		update_state(&o->vblank_state);
}

static void connector_find_preferred_mode(uint32_t connector_id, int crtc_idx,
					  struct test_output *o)
{
	struct kmstest_connector_config config;

	if (kmstest_get_connector_config(drm_fd, connector_id, 1 << crtc_idx,
					 &config) < 0) {
		o->mode_valid = 0;
		return;
	}

	o->connector = config.connector;
	o->encoder = config.encoder;
	o->crtc = config.crtc->crtc_id;
	o->pipe = config.pipe;
	o->mode = config.default_mode;
	o->mode_valid = 1;
}

static void paint_flip_mode(struct kmstest_fb *fb, bool odd_frame)
{
	cairo_t *cr = kmstest_get_cairo_ctx(drm_fd, fb);
	int width = fb->width;
	int height = fb->height;

	kmstest_paint_test_pattern(cr, width, height);

	if (odd_frame)
		cairo_rectangle(cr, width/4, height/2, width/4, height/8);
	else
		cairo_rectangle(cr, width/2, height/2, width/4, height/8);

	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_fill(cr);

	assert(!cairo_status(cr));
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
	if (es->count == 0) {
		fprintf(stderr, "no %s event received\n", es->name);
		exit(12);
	}

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
	evctx.vblank_handler = vblank_handler;
	evctx.page_flip_handler = page_flip_handler;

	/* make timeout lax with the dummy load */
	if (o->flags & TEST_WITH_DUMMY_LOAD)
		timeout.tv_sec *= 10;

	FD_ZERO(&fds);
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
	unsigned long start, end;

	start = gettime_us();

	while (1) {
		unsigned int completed_events;

		completed_events = run_test_step(o);
		if (o->pending_events)
			completed_events |= wait_for_events(o);
		check_all_state(o, completed_events);
		update_all_state(o, completed_events);

		if ((gettime_us() - start) / 1000000 >= duration_sec)
			break;
	}

	end = gettime_us();

	/* Flush any remaining events */
	if (o->pending_events)
		wait_for_events(o);

	return end - start;
}

static void run_test_on_crtc(struct test_output *o, int crtc_idx, int duration)
{
	unsigned ellapsed;

	o->bpp = 32;
	o->depth = 24;

	connector_find_preferred_mode(o->id, crtc_idx, o);
	if (!o->mode_valid)
		return;

	last_connector = o->connector;

	fprintf(stdout, "Beginning %s on crtc %d, connector %d\n",
		o->test_name, o->crtc, o->id);

	o->fb_width = o->mode.hdisplay;
	o->fb_height = o->mode.vdisplay;

	if (o->flags & TEST_PAN)
		o->fb_width *= 2;

	o->fb_ids[0] = kmstest_create_fb(drm_fd, o->fb_width, o->fb_height,
					 o->bpp, o->depth, false, &o->fb_info[0]);
	o->fb_ids[1] = kmstest_create_fb(drm_fd, o->fb_width, o->fb_height,
					 o->bpp, o->depth, false, &o->fb_info[1]);
	o->fb_ids[2] = kmstest_create_fb(drm_fd, o->fb_width, o->fb_height,
					 o->bpp, o->depth, true, &o->fb_info[2]);

	if (!o->fb_ids[0] || !o->fb_ids[1] || !o->fb_ids[2]) {
		fprintf(stderr, "failed to create fbs\n");
		exit(3);
	}

	paint_flip_mode(&o->fb_info[0], false);
	paint_flip_mode(&o->fb_info[1], true);
	paint_flip_mode(&o->fb_info[2], true);

	set_y_tiling(o, 2);

	kmstest_dump_mode(&o->mode);
	if (drmModeSetCrtc(drm_fd, o->crtc, o->fb_ids[0], 0, 0,
			   &o->id, 1, &o->mode)) {
		/* We may fail to apply the mode if there are hidden
		 * constraints, such as bandwidth on the third pipe.
		 */
		if (0) {
			fprintf(stderr, "failed to set mode (%dx%d@%dHz): %s\n",
				o->fb_width, o->fb_height, o->mode.vrefresh,
				strerror(errno));
		}
		goto out;
	}
	assert(fb_is_bound(o, o->fb_ids[0]));

	/* quiescent the hw a bit so ensure we don't miss a single frame */
	if (o->flags & TEST_CHECK_TS)
		sleep(1);

	if (do_page_flip(o, o->fb_ids[1], true)) {
		fprintf(stderr, "failed to page flip: %s\n", strerror(errno));
		exit(4);
	}
	wait_for_events(o);

	o->current_fb_id = 1;
	o->flip_state.seq_step = 1;
	if (o->flags & TEST_VBLANK_ABSOLUTE)
		o->vblank_state.seq_step = 5;
	else
		o->vblank_state.seq_step = 1;

	ellapsed = event_loop(o, duration);

	if (o->flags & TEST_FLIP && !(o->flags & TEST_NOEVENT))
		check_final_state(o, &o->flip_state, ellapsed);
	if (o->flags & TEST_VBLANK)
		check_final_state(o, &o->vblank_state, ellapsed);

	fprintf(stdout, "\n%s on crtc %d, connector %d: PASSED\n\n",
		o->test_name, o->crtc, o->id);

out:
	kmstest_remove_fb(drm_fd, &o->fb_info[2]);
	kmstest_remove_fb(drm_fd, &o->fb_info[1]);
	kmstest_remove_fb(drm_fd, &o->fb_info[0]);

	last_connector = NULL;

	drmModeFreeEncoder(o->encoder);
	drmModeFreeConnector(o->connector);
}

static int run_test(int duration, int flags, const char *test_name)
{
	struct test_output o;
	int c;
	int crtc_idx;

	resources = drmModeGetResources(drm_fd);
	if (!resources) {
		fprintf(stderr, "drmModeGetResources failed: %s\n",
			strerror(errno));
		exit(5);
	}

	/* Find any connected displays */
	for (c = 0; c < resources->count_connectors; c++) {
		for (crtc_idx = 0; crtc_idx < resources->count_crtcs; crtc_idx++) {
			memset(&o, 0, sizeof(o));
			o.test_name = test_name;
			o.id = resources->connectors[c];
			o.flags = flags;
			o.flip_state.name = "flip";
			o.vblank_state.name = "vblank";

			run_test_on_crtc(&o, crtc_idx, duration);
		}
	}

	drmModeFreeResources(resources);
	return 1;
}

static void get_timestamp_format(void)
{
	uint64_t cap_mono;
	int ret;

	ret = drmGetCap(drm_fd, DRM_CAP_TIMESTAMP_MONOTONIC, &cap_mono);
	assert(ret == 0 || errno == EINVAL);
	monotonic_timestamp = ret == 0 && cap_mono == 1;
	printf("Using %s timestamps\n",
		monotonic_timestamp ? "monotonic" : "real");
}

static void kms_flip_exit_handler(int sig)
{
	if (last_connector)
		set_connector_dpms(last_connector, DRM_MODE_DPMS_ON);
}

int main(int argc, char **argv)
{
	struct {
		int duration;
		int flags;
		const char *name;
	} tests[] = {
		{ 15, TEST_VBLANK, "wf_vblank" },
		{ 15, TEST_VBLANK | TEST_CHECK_TS, "wf_vblank-ts-check" },
		{ 15, TEST_VBLANK | TEST_VBLANK_BLOCK | TEST_CHECK_TS,
					"blocking-wf_vblank" },
		{ 5,  TEST_VBLANK | TEST_VBLANK_ABSOLUTE,
					"absolute-wf_vblank" },
		{ 5,  TEST_VBLANK | TEST_VBLANK_BLOCK | TEST_VBLANK_ABSOLUTE,
					"blocking-absolute-wf_vblank" },
		{ 30,  TEST_VBLANK | TEST_DPMS | TEST_EINVAL, "wf_vblank-vs-dpms" },
		{ 30,  TEST_VBLANK | TEST_DPMS | TEST_WITH_DUMMY_LOAD,
					"delayed-wf_vblank-vs-dpms" },
		{ 30,  TEST_VBLANK | TEST_MODESET | TEST_EINVAL, "wf_vblank-vs-modeset" },
		{ 30,  TEST_VBLANK | TEST_MODESET | TEST_WITH_DUMMY_LOAD,
					"delayed-wf_vblank-vs-modeset" },

		{ 15, TEST_FLIP | TEST_EBUSY , "plain-flip" },
		{ 15, TEST_FLIP | TEST_CHECK_TS | TEST_EBUSY , "plain-flip-ts-check" },
		{ 15, TEST_FLIP | TEST_CHECK_TS | TEST_EBUSY | TEST_FB_RECREATE,
			"plain-flip-fb-recreate" },
		{ 15, TEST_FLIP | TEST_EBUSY | TEST_RMFB | TEST_MODESET , "flip-vs-rmfb" },
		{ 30, TEST_FLIP | TEST_DPMS | TEST_EINVAL, "flip-vs-dpms" },
		{ 30, TEST_FLIP | TEST_DPMS | TEST_WITH_DUMMY_LOAD, "delayed-flip-vs-dpms" },
		{ 5,  TEST_FLIP | TEST_PAN, "flip-vs-panning" },
		{ 30, TEST_FLIP | TEST_PAN | TEST_WITH_DUMMY_LOAD, "delayed-flip-vs-panning" },
		{ 30, TEST_FLIP | TEST_MODESET | TEST_EINVAL, "flip-vs-modeset" },
		{ 30, TEST_FLIP | TEST_MODESET | TEST_WITH_DUMMY_LOAD, "delayed-flip-vs-modeset" },
		{ 5,  TEST_FLIP | TEST_VBLANK_EXPIRED_SEQ,
					"flip-vs-expired-vblank" },

		{ 15, TEST_FLIP | TEST_VBLANK | TEST_VBLANK_ABSOLUTE |
		      TEST_CHECK_TS, "flip-vs-absolute-wf_vblank" },
		{ 15, TEST_FLIP | TEST_VBLANK | TEST_CHECK_TS,
					"flip-vs-wf_vblank" },
		{ 15, TEST_FLIP | TEST_VBLANK | TEST_VBLANK_BLOCK |
			TEST_CHECK_TS, "flip-vs-blocking-wf-vblank" },
		{ 15, TEST_FLIP | TEST_MODESET | TEST_HANG | TEST_NOEVENT, "flip-vs-modeset-vs-hang" },
		{ 15, TEST_FLIP | TEST_PAN | TEST_HANG, "flip-vs-panning-vs-hang" },
		{ 1, TEST_FLIP | TEST_EINVAL | TEST_FB_BAD_TILING, "flip-vs-bad-tiling" },

		{ 1, TEST_DPMS_OFF | TEST_MODESET | TEST_FLIP,
					"flip-vs-dpms-off-vs-modeset" },
		{ 1, TEST_DPMS_OFF | TEST_MODESET | TEST_FLIP | TEST_SINGLE_BUFFER,
					"single-buffer-flip-vs-dpms-off-vs-modeset" },
	};
	int i;

	drmtest_subtest_init(argc, argv);

	drm_fd = drm_open_any();

	if (!drmtest_only_list_subtests()) {
		do_or_die(drmtest_set_vt_graphics_mode());
		do_or_die(drmtest_install_exit_handler(kms_flip_exit_handler));
		get_timestamp_format();
	}

	bufmgr = drm_intel_bufmgr_gem_init(drm_fd, 4096);
	devid = intel_get_drm_devid(drm_fd);
	batch = intel_batchbuffer_alloc(bufmgr, devid);

	for (i = 0; i < sizeof(tests) / sizeof (tests[0]); i++) {
		if (drmtest_run_subtest(tests[i].name)) {
			printf("running testcase: %s\n", tests[i].name);
			run_test(tests[i].duration, tests[i].flags, tests[i].name);
		}
	}

	close(drm_fd);

	return 0;
}
