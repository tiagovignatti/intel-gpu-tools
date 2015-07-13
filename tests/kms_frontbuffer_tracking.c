/*
 * Copyright © 2015 Intel Corporation
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
 * Authors: Paulo Zanoni <paulo.r.zanoni@intel.com>
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>

#include "drmtest.h"
#include "igt_aux.h"
#include "igt_draw.h"
#include "igt_kms.h"
#include "igt_debugfs.h"
#include "intel_chipset.h"
#include "ioctl_wrappers.h"

IGT_TEST_DESCRIPTION("Test the Kernel's frontbuffer tracking mechanism and "
		     "its related features: FBC and PSR");

/*
 * One of the aspects of this test is that, for every subtest, we try different
 * combinations of the parameters defined by the struct below. Because of this,
 * a single addition of a new parameter or subtest function can lead to hundreds
 * of new subtests.
 *
 * In order to reduce the number combinations we cut the cases that don't make
 * sense, such as writing on the secondary screen when there is only a single
 * pipe, or flipping when the target is the offscreen buffer. We also hide some
 * combinations that are somewhat redundant and don't add much value to the
 * test. For example, since we already do the offscreen testing with a single
 * pipe enabled, there's no much value in doing it again with dual pipes. If you
 * still want to try these redundant tests, you need to use the --show-hidden
 * option.
 *
 * The most important hidden thing is the FEATURE_NONE set of tests. Whenever
 * you get a failure on any test, it is important to check whether the same test
 * fails with FEATURE_NONE - replace the feature name for "nop". If the nop test
 * also fails, then it's likely the problem will be on the IGT side instead of
 * the Kernel side. We don't expose this set of tests by default because (i)
 * they take a long time to test; and (ii) if the feature tests work, then it's
 * very likely that the nop tests will also work.
 */
struct test_mode {
	/* Are we going to enable just one monitor, or are we going to setup a
	 * dual screen environment for the test? */
	enum {
		PIPE_SINGLE = 0,
		PIPE_DUAL,
		PIPE_COUNT,
	} pipes;

	/* The primary screen is the one that's supposed to have the "feature"
	 * enabled on, but we have the option to draw on the secondary screen or
	 * on some offscreen buffer. We also only theck the CRC of the primary
	 * screen. */
	enum {
		SCREEN_PRIM = 0,
		SCREEN_SCND,
		SCREEN_OFFSCREEN,
		SCREEN_COUNT,
	} screen;

	/* When we draw, we can draw directly on the primary plane, on the
	 * cursor or on the sprite plane. */
	enum {
		PLANE_PRI = 0,
		PLANE_CUR,
		PLANE_SPR,
		PLANE_COUNT,
	} plane;

	/* We can organize the screens in a way that each screen has its own
	 * framebuffer, or in a way that all screens point to the same
	 * framebuffer, but on different places. This includes the offscreen
	 * screen. */
	enum {
		FBS_INDIVIDUAL = 0,
		FBS_SHARED,
		FBS_COUNT,
	} fbs;

	/* Which features are we going to test now? This is a mask! */
	enum {
		FEATURE_NONE  = 0,
		FEATURE_FBC   = 1,
		FEATURE_PSR   = 2,
		FEATURE_COUNT = 4,
	} feature;

	enum igt_draw_method method;
};

enum flip_type {
	FLIP_PAGEFLIP,
	FLIP_PAGEFLIP_EVENT,
	FLIP_MODESET,
};

struct rect {
	int x;
	int y;
	int w;
	int h;
	uint32_t color;
};

#define MAX_CONNECTORS 32
struct {
	int fd;
	drmModeResPtr res;
	drmModeConnectorPtr connectors[MAX_CONNECTORS];
	drmModePlaneResPtr planes;
	drm_intel_bufmgr *bufmgr;
} drm;

struct {
	int fd;
	bool can_test;

	bool supports_compressing;
	bool supports_last_action;

	struct timespec last_action;
} fbc = {
	.fd = -1,
	.can_test = false,
	.supports_last_action = false,
	.supports_compressing = false,
};

struct {
	int fd;
	bool can_test;
} psr = {
	.fd = -1,
	.can_test = false,
};


#define SINK_CRC_SIZE 12
typedef struct {
	char data[SINK_CRC_SIZE];
} sink_crc_t;

struct both_crcs {
	igt_crc_t pipe;
	sink_crc_t sink;
};

igt_pipe_crc_t *pipe_crc;
struct both_crcs blue_crc;
struct both_crcs *wanted_crc;

struct {
	int fd;
	bool supported;
} sink_crc = {
	.fd = -1,
	.supported = false,
};

/* The goal of this structure is to easily allow us to deal with cases where we
 * have a big framebuffer and the CRTC is just displaying a subregion of this
 * big FB. */
struct fb_region {
	struct igt_fb *fb;
	int x;
	int y;
	int w;
	int h;
};

struct draw_pattern_info {
	bool initialized;
	bool frames_stack;
	int n_rects;
	struct both_crcs *crcs;
	struct rect (*get_rect)(struct fb_region *fb, int r);
};

/* Draw big rectangles on the screen. */
struct draw_pattern_info pattern1;
/* 64x64 rectangles at x:0,y:0, just so we can draw on the cursor and sprite. */
struct draw_pattern_info pattern2;
/* 64x64 rectangles at different positions, same color, for the move test. */
struct draw_pattern_info pattern3;
/* Just a fullscreen green square. */
struct draw_pattern_info pattern4;

/* Command line parameters. */
struct {
	bool check_status;
	bool check_crc;
	bool fbc_check_compression;
	bool fbc_check_last_action;
	bool no_edp;
	bool small_modes;
	bool show_hidden;
	int step;
	int only_feature;
	int only_pipes;
} opt = {
	.check_status = true,
	.check_crc = true,
	.fbc_check_compression = true,
	.fbc_check_last_action = true,
	.no_edp = false,
	.small_modes = false,
	.show_hidden= false,
	.step = 0,
	.only_feature = FEATURE_COUNT,
	.only_pipes = PIPE_COUNT,
};

struct modeset_params {
	uint32_t crtc_id;
	uint32_t connector_id;
	uint32_t sprite_id;
	drmModeModeInfoPtr mode;
	struct fb_region fb;
	struct fb_region cursor;
	struct fb_region sprite;
};

struct modeset_params prim_mode_params;
struct modeset_params scnd_mode_params;
struct fb_region offscreen_fb;
struct {
	struct igt_fb prim_pri;
	struct igt_fb prim_cur;
	struct igt_fb prim_spr;

	struct igt_fb scnd_pri;
	struct igt_fb scnd_cur;
	struct igt_fb scnd_spr;

	struct igt_fb offscreen;
	struct igt_fb big;
} fbs;

struct {
	pthread_t thread;
	bool stop;

	uint32_t handle;
	uint32_t size;
	uint32_t stride;
	int width;
	int height;
} busy_thread = {
	.stop = true,
};

drmModeModeInfo std_1024_mode = {
	.clock = 65000,
	.hdisplay = 1024,
	.hsync_start = 1048,
	.hsync_end = 1184,
	.htotal = 1344,
	.vtotal = 806,
	.hskew = 0,
	.vdisplay = 768,
	.vsync_start = 771,
	.vsync_end = 777,
	.vtotal = 806,
	.vscan = 0,
	.vrefresh = 60,
	.flags = 0xA,
	.type = 0x40,
	.name = "Custom 1024x768",
};

static drmModeModeInfoPtr get_connector_smallest_mode(drmModeConnectorPtr c)
{
	int i;
	drmModeModeInfoPtr smallest = NULL;

	for (i = 0; i < c->count_modes; i++) {
		drmModeModeInfoPtr mode = &c->modes[i];

		if (!smallest)
			smallest = mode;

		if (mode->hdisplay * mode->vdisplay <
		    smallest->hdisplay * smallest->vdisplay)
			smallest = mode;
	}

	if (c->connector_type == DRM_MODE_CONNECTOR_eDP)
		smallest = &std_1024_mode;

	return smallest;
}

static drmModeConnectorPtr get_connector(uint32_t id)
{
	int i;

	for (i = 0; i < drm.res->count_connectors; i++)
		if (drm.res->connectors[i] == id)
			return drm.connectors[i];

	igt_assert(false);
}

static void print_mode_info(const char *screen, struct modeset_params *params)
{
	drmModeConnectorPtr c = get_connector(params->connector_id);

	igt_info("%s screen: %s %s\n",
		 screen,
		 kmstest_connector_type_str(c->connector_type),
		 params->mode->name);
}

static void init_mode_params(struct modeset_params *params, uint32_t crtc_id,
			     int crtc_index, uint32_t connector_id,
			     drmModeModeInfoPtr mode)
{
	uint32_t plane_id = 0;
	int i;

	for (i = 0; i < drm.planes->count_planes && plane_id == 0; i++) {
		drmModePlanePtr plane;

		plane = drmModeGetPlane(drm.fd, drm.planes->planes[i]);
		igt_assert(plane);

		if (plane->possible_crtcs & (1 << crtc_index))
			plane_id = plane->plane_id;

		drmModeFreePlane(plane);
	}
	igt_assert(plane_id);

	params->crtc_id = crtc_id;
	params->connector_id = connector_id;
	params->mode = mode;
	params->sprite_id = plane_id;

	params->fb.fb = NULL;
	params->fb.w = mode->hdisplay;
	params->fb.h = mode->vdisplay;

	params->cursor.fb = NULL;
	params->cursor.x = 0;
	params->cursor.y = 0;
	params->cursor.w = 64;
	params->cursor.h = 64;

	params->sprite.fb = NULL;
	params->sprite.x = 0;
	params->sprite.y = 0;
	params->sprite.w = 64;
	params->sprite.h = 64;
}

static bool connector_get_mode(drmModeConnectorPtr c, drmModeModeInfoPtr *mode)
{
	*mode = NULL;

	if (c->connection != DRM_MODE_CONNECTED || !c->count_modes)
		return false;

	if (c->connector_type == DRM_MODE_CONNECTOR_eDP && opt.no_edp)
		return false;

	if (opt.small_modes)
		*mode = get_connector_smallest_mode(c);
	else
		*mode = &c->modes[0];

	 /* On HSW the CRC WA is so awful that it makes you think everything is
	  * bugged. */
	if (IS_HASWELL(intel_get_drm_devid(drm.fd)) &&
	    c->connector_type == DRM_MODE_CONNECTOR_eDP)
		*mode = &std_1024_mode;

	return true;
}

static bool init_modeset_cached_params(void)
{
	int i;
	uint32_t prim_connector_id = 0, scnd_connector_id = 0;
	drmModeModeInfoPtr prim_mode = NULL, scnd_mode = NULL;
	drmModeModeInfoPtr tmp_mode;

	/* First, try to find an eDP monitor since it's the only possible type
	 * for PSR.  */
	for (i = 0; i < drm.res->count_connectors; i++) {
		if (drm.connectors[i]->connector_type != DRM_MODE_CONNECTOR_eDP)
			continue;

		if (connector_get_mode(drm.connectors[i], &tmp_mode)) {
			prim_connector_id = drm.res->connectors[i];
			prim_mode = tmp_mode;
		}
	}
	for (i = 0; i < drm.res->count_connectors; i++) {
		/* Don't pick again what we just selected on the above loop. */
		if (drm.res->connectors[i] == prim_connector_id)
			continue;

		if (connector_get_mode(drm.connectors[i], &tmp_mode)) {
			if (!prim_connector_id) {
				prim_connector_id = drm.res->connectors[i];
				prim_mode = tmp_mode;
			} else if (!scnd_connector_id) {
				scnd_connector_id = drm.res->connectors[i];
				scnd_mode = tmp_mode;
				break;
			}
		}
	}

	if (!prim_connector_id)
		return false;

	init_mode_params(&prim_mode_params, drm.res->crtcs[0], 0,
			 prim_connector_id, prim_mode);
	print_mode_info("Primary", &prim_mode_params);

	if (!scnd_connector_id) {
		scnd_mode_params.connector_id = 0;
		return true;
	}

	igt_assert(drm.res->count_crtcs >= 2);
	init_mode_params(&scnd_mode_params, drm.res->crtcs[1], 1,
			 scnd_connector_id, scnd_mode);
	print_mode_info("Secondary", &scnd_mode_params);

	return true;
}

#define BIGFB_X_OFFSET 500
#define BIGFB_Y_OFFSET 500
/*
 * This is how the prim, scnd and offscreen FBs should be positioned inside the
 * big FB. The prim buffer starts at the X and Y offsets defined above, then
 * scnd starts at the same X pixel offset, right after prim ends on the Y axis,
 * then the offscreen fb starts after scnd ends. Just like the picture:
 *
 * +----------------------+--+
 * | big                  |  |
 * |   +------------------+  |
 * |   | prim             |  |
 * |   |                  |  |
 * |   |                  |  |
 * |   |                  |  |
 * |   +------------------+--+
 * |   | scnd                |
 * |   |                     |
 * |   |                     |
 * |   +---------------+-----+
 * |   | offscreen     |     |
 * |   |               |     |
 * |   |               |     |
 * +---+---------------+-----+
 *
 * We do it vertically instead of the more common horizontal case in order to
 * avoid super huge strides not supported by FBC.
 */
static void create_big_fb(void)
{
	int prim_w, prim_h, scnd_w, scnd_h, offs_w, offs_h, big_w, big_h;

	prim_w = prim_mode_params.mode->hdisplay;
	prim_h = prim_mode_params.mode->vdisplay;

	if (scnd_mode_params.connector_id) {
		scnd_w = scnd_mode_params.mode->hdisplay;
		scnd_h = scnd_mode_params.mode->vdisplay;
	} else {
		scnd_w = 0;
		scnd_h = 0;
	}
	offs_w = offscreen_fb.w;
	offs_h = offscreen_fb.h;

	big_w = prim_w;
	if (scnd_w > big_w)
		big_w = scnd_w;
	if (offs_w > big_w)
		big_w = offs_w;
	big_w += BIGFB_X_OFFSET;

	big_h = prim_h + scnd_h + offs_h + BIGFB_Y_OFFSET;

	igt_create_fb(drm.fd, big_w, big_h, DRM_FORMAT_XRGB8888,
		      LOCAL_I915_FORMAT_MOD_X_TILED, &fbs.big);
}

static void create_fbs(void)
{
	igt_create_fb(drm.fd, prim_mode_params.mode->hdisplay,
		      prim_mode_params.mode->vdisplay,
		      DRM_FORMAT_XRGB8888, LOCAL_I915_FORMAT_MOD_X_TILED,
		      &fbs.prim_pri);
	igt_create_fb(drm.fd, prim_mode_params.cursor.w,
		      prim_mode_params.cursor.h, DRM_FORMAT_ARGB8888,
		      LOCAL_DRM_FORMAT_MOD_NONE, &fbs.prim_cur);
	igt_create_fb(drm.fd, prim_mode_params.sprite.w,
		      prim_mode_params.sprite.h, DRM_FORMAT_XRGB8888,
		      LOCAL_I915_FORMAT_MOD_X_TILED, &fbs.prim_spr);

	igt_create_fb(drm.fd, offscreen_fb.w, offscreen_fb.h,
		      DRM_FORMAT_XRGB8888, LOCAL_I915_FORMAT_MOD_X_TILED,
		      &fbs.offscreen);

	create_big_fb();

	if (!scnd_mode_params.connector_id)
		return;

	igt_create_fb(drm.fd, scnd_mode_params.mode->hdisplay,
		      scnd_mode_params.mode->vdisplay,
		      DRM_FORMAT_XRGB8888, LOCAL_I915_FORMAT_MOD_X_TILED,
		      &fbs.scnd_pri);
	igt_create_fb(drm.fd, scnd_mode_params.cursor.w,
		      scnd_mode_params.cursor.h, DRM_FORMAT_ARGB8888,
		      LOCAL_DRM_FORMAT_MOD_NONE, &fbs.scnd_cur);
	igt_create_fb(drm.fd, scnd_mode_params.sprite.w,
		      scnd_mode_params.sprite.h, DRM_FORMAT_XRGB8888,
		      LOCAL_I915_FORMAT_MOD_X_TILED, &fbs.scnd_spr);
}

static bool set_mode_for_params(struct modeset_params *params)
{
	int rc;

	rc = drmModeSetCrtc(drm.fd, params->crtc_id, params->fb.fb->fb_id,
			    params->fb.x, params->fb.y,
			    &params->connector_id, 1, params->mode);
	return (rc == 0);
}

#define DEBUGFS_MSG_SIZE 256

static void get_debugfs_string(int fd, char *buf)
{
	ssize_t n_read;

	lseek(fd, 0, SEEK_SET);

	n_read = read(fd, buf, DEBUGFS_MSG_SIZE -1);
	igt_assert(n_read >= 0);
	buf[n_read] = '\0';
}

static bool fbc_is_enabled(void)
{
	char buf[DEBUGFS_MSG_SIZE];

	get_debugfs_string(fbc.fd, buf);

	return strstr(buf, "FBC enabled\n");
}

static bool psr_is_enabled(void)
{
	char buf[DEBUGFS_MSG_SIZE];

	get_debugfs_string(psr.fd, buf);

	return (strstr(buf, "\nActive: yes\n"));
}

static struct timespec fbc_get_last_action(void)
{
	struct timespec ret = { 0, 0 };
	char buf[DEBUGFS_MSG_SIZE];
	char *action;
	ssize_t n_read;

	get_debugfs_string(fbc.fd, buf);

	action = strstr(buf, "\nLast action:");
	igt_assert(action);

	n_read = sscanf(action, "Last action: %ld.%ld",
			&ret.tv_sec, &ret.tv_nsec);
	igt_assert(n_read == 2);

	return ret;
}

static bool fbc_last_action_changed(void)
{
	struct timespec t_new, t_old;

	t_old = fbc.last_action;
	t_new = fbc_get_last_action();

	fbc.last_action = t_new;

#if 0
	igt_info("old: %ld.%ld\n", t_old.tv_sec, t_old.tv_nsec);
	igt_info("new: %ld.%ld\n", t_new.tv_sec, t_new.tv_nsec);
#endif

	return t_old.tv_sec != t_new.tv_sec ||
	       t_old.tv_nsec != t_new.tv_nsec;
}

static void fbc_update_last_action(void)
{
	if (!fbc.supports_last_action)
		return;

	fbc.last_action = fbc_get_last_action();

#if 0
	igt_info("Last action: %ld.%ld\n",
		 fbc.last_action.tv_sec, fbc.last_action.tv_nsec);
#endif
}

static void fbc_setup_last_action(void)
{
	ssize_t n_read;
	char buf[DEBUGFS_MSG_SIZE];
	char *action;

	get_debugfs_string(fbc.fd, buf);

	action = strstr(buf, "\nLast action:");
	if (!action) {
		igt_info("FBC last action not supported\n");
		return;
	}

	fbc.supports_last_action = true;

	n_read = sscanf(action, "Last action: %ld.%ld",
			&fbc.last_action.tv_sec, &fbc.last_action.tv_nsec);
	igt_assert(n_read == 2);
}

static bool fbc_is_compressing(void)
{
	char buf[DEBUGFS_MSG_SIZE];

	get_debugfs_string(fbc.fd, buf);
	return strstr(buf, "\nCompressing: yes\n") != NULL;
}

static bool fbc_wait_for_compression(void)
{
	return igt_wait(fbc_is_compressing(), 5000, 1);
}

static void fbc_setup_compressing(void)
{
	char buf[DEBUGFS_MSG_SIZE];

	get_debugfs_string(fbc.fd, buf);

	if (strstr(buf, "\nCompressing:"))
		fbc.supports_compressing = true;
	else
		igt_info("FBC compression information not supported\n");
}

static bool fbc_wait_until_enabled(void)
{
	return igt_wait(fbc_is_enabled(), 5000, 1);
}

static bool psr_wait_until_enabled(void)
{
	return igt_wait(psr_is_enabled(), 5000, 1);
}

#define fbc_enable() igt_set_module_param_int("enable_fbc", 1)
#define fbc_disable() igt_set_module_param_int("enable_fbc", 0)
#define psr_enable() igt_set_module_param_int("enable_psr", 1)
#define psr_disable() igt_set_module_param_int("enable_psr", 0)

static void get_sink_crc(sink_crc_t *crc)
{
	lseek(sink_crc.fd, 0, SEEK_SET);

	igt_assert(read(sink_crc.fd, crc->data, SINK_CRC_SIZE) ==
		   SINK_CRC_SIZE);
}

static bool sink_crc_equal(sink_crc_t *a, sink_crc_t *b)
{
	return (memcmp(a->data, b->data, SINK_CRC_SIZE) == 0);
}

#define assert_sink_crc_equal(a, b) igt_assert(sink_crc_equal(a, b))

static struct rect pat1_get_rect(struct fb_region *fb, int r)
{
	struct rect rect;

	switch (r) {
	case 0:
		rect.x = 0;
		rect.y = 0;
		rect.w = fb->w / 8;
		rect.h = fb->h / 8;
		rect.color = 0x00FF00;
		break;
	case 1:
		rect.x = fb->w / 8 * 4;
		rect.y = fb->h / 8 * 4;
		rect.w = fb->w / 8 * 2;
		rect.h = fb->h / 8 * 2;
		rect.color = 0xFF0000;
		break;
	case 2:
		rect.x = fb->w / 16 + 1;
		rect.y = fb->h / 16 + 1;
		rect.w = fb->w / 8 + 1;
		rect.h = fb->h / 8 + 1;
		rect.color = 0xFF00FF;
		break;
	case 3:
		rect.x = fb->w - 64;
		rect.y = fb->h - 64;
		rect.w = 64;
		rect.h = 64;
		rect.color = 0x00FFFF;
		break;
	default:
		igt_assert(false);
	}

	return rect;
}

static struct rect pat2_get_rect(struct fb_region *fb, int r)
{
	struct rect rect;

	rect.x = 0;
	rect.y = 0;
	rect.w = 64;
	rect.h = 64;

	switch (r) {
	case 0:
		rect.color = 0xFF00FF00;
		break;
	case 1:
		rect.x = 31;
		rect.y = 31;
		rect.w = 31;
		rect.h = 31;
		rect.color = 0xFFFF0000;
		break;
	case 2:
		rect.x = 16;
		rect.y = 16;
		rect.w = 32;
		rect.h = 32;
		rect.color = 0xFFFF00FF;
		break;
	case 3:
		rect.color = 0xFF00FFFF;
		break;
	default:
		igt_assert(false);
	}

	return rect;
}

static struct rect pat3_get_rect(struct fb_region *fb, int r)
{
	struct rect rect;

	rect.w = 64;
	rect.h = 64;
	rect.color = 0xFF00FF00;

	switch (r) {
	case 0:
		rect.x = 0;
		rect.y = 0;
		break;
	case 1:
		rect.x = 64;
		rect.y = 64;
		break;
	case 2:
		rect.x = 1;
		rect.y = 1;
		break;
	case 3:
		rect.x = fb->w - 64;
		rect.y = fb->h - 64;
		break;
	case 4:
		rect.x = fb->w / 2 - 32;
		rect.y = fb->h / 2 - 32;
		break;
	default:
		igt_assert(false);
	}

	return rect;
}

static struct rect pat4_get_rect(struct fb_region *fb, int r)
{
	struct rect rect;

	igt_assert(r == 0);

	rect.x = 0;
	rect.y = 0;
	rect.w = fb->w;
	rect.h = fb->h;
	rect.color = 0xFF00FF00;

	return rect;
}

static void draw_rect(struct draw_pattern_info *pattern, struct fb_region *fb,
		      enum igt_draw_method method, int r)
{
	struct rect rect = pattern->get_rect(fb, r);

	igt_draw_rect_fb(drm.fd, drm.bufmgr, NULL, fb->fb, method,
			 fb->x + rect.x, fb->y + rect.y,
			 rect.w, rect.h, rect.color);
}

static void draw_rect_igt_fb(struct draw_pattern_info *pattern,
			     struct igt_fb *fb, enum igt_draw_method method,
			     int r)
{
	struct fb_region region = {
		.fb = fb,
		.x = 0,
		.y = 0,
		.w = fb->width,
		.h = fb->height,
	};

	draw_rect(pattern, &region, method, r);
}

static void fill_fb_region(struct fb_region *region, uint32_t color)
{
	igt_draw_rect_fb(drm.fd, NULL, NULL, region->fb, IGT_DRAW_MMAP_CPU,
			 region->x, region->y, region->w, region->h,
			 color);
}

static void unset_all_crtcs(void)
{
	int i, rc;

	for (i = 0; i < drm.res->count_crtcs; i++) {
		rc = drmModeSetCrtc(drm.fd, drm.res->crtcs[i], -1, 0, 0, NULL,
				    0, NULL);
		igt_assert(rc == 0);

		rc = drmModeSetCursor(drm.fd, drm.res->crtcs[i], 0, 0, 0);
		igt_assert(rc == 0);
	}

	for (i = 0; i < drm.planes->count_planes; i++) {
		rc = drmModeSetPlane(drm.fd, drm.planes->planes[i], 0, 0, 0, 0,
				     0, 0, 0, 0, 0, 0, 0);
		igt_assert(rc == 0);
	}
}

static void disable_features(void)
{
	fbc_disable();
	psr_disable();
}

static void *busy_thread_func(void *data)
{
	while (!busy_thread.stop)
		igt_draw_rect(drm.fd, drm.bufmgr, NULL, busy_thread.handle,
			      busy_thread.size, busy_thread.stride,
			      IGT_DRAW_BLT, 0, 0, busy_thread.width,
			      busy_thread.height, 0xFF);

	pthread_exit(0);
}

static void start_busy_thread(struct igt_fb *fb)
{
	int rc;

	igt_assert(busy_thread.stop == true);
	busy_thread.stop = false;
	busy_thread.handle = fb->gem_handle;
	busy_thread.size = fb->size;
	busy_thread.stride = fb->stride;
	busy_thread.width = fb->width;
	busy_thread.height = fb->height;

	rc = pthread_create(&busy_thread.thread, NULL, busy_thread_func, NULL);
	igt_assert(rc == 0);
}

static void stop_busy_thread(void)
{
	if (!busy_thread.stop) {
		busy_thread.stop = true;
		igt_assert(pthread_join(busy_thread.thread, NULL) == 0);
	}
}

static void print_crc(const char *str, struct both_crcs *crc)
{
	int i;
	char *pipe_str;

	pipe_str = igt_crc_to_string(&crc->pipe);

	igt_debug("%s pipe:[%s] sink:[", str, pipe_str);
	for (i = 0; i < SINK_CRC_SIZE; i++)
		igt_debug("%c", crc->sink.data[i]);
	igt_debug("]\n");

	free(pipe_str);
}

static void collect_crcs(struct both_crcs *crcs)
{
	igt_pipe_crc_collect_crc(pipe_crc, &crcs->pipe);

	if (sink_crc.supported)
		get_sink_crc(&crcs->sink);
	else
		memcpy(&crcs->sink, "unsupported!", SINK_CRC_SIZE);
}

static void init_blue_crc(void)
{
	struct igt_fb blue;
	int rc;

	disable_features();
	unset_all_crtcs();

	igt_create_fb(drm.fd, prim_mode_params.mode->hdisplay,
		      prim_mode_params.mode->vdisplay, DRM_FORMAT_XRGB8888,
		      LOCAL_I915_FORMAT_MOD_X_TILED, &blue);

	igt_draw_fill_fb(drm.fd, &blue, 0xFF);

	rc = drmModeSetCrtc(drm.fd, prim_mode_params.crtc_id,
			    blue.fb_id, 0, 0, &prim_mode_params.connector_id, 1,
			    prim_mode_params.mode);
	igt_assert(rc == 0);
	collect_crcs(&blue_crc);

	print_crc("Blue CRC:  ", &blue_crc);

	igt_remove_fb(drm.fd, &blue);
}

static void init_crcs(struct draw_pattern_info *pattern)
{
	int r, r_, rc;
	struct igt_fb tmp_fbs[pattern->n_rects];

	if (pattern->initialized)
		return;

	pattern->crcs = calloc(pattern->n_rects, sizeof(*(pattern->crcs)));

	for (r = 0; r < pattern->n_rects; r++)
		igt_create_fb(drm.fd, prim_mode_params.mode->hdisplay,
			      prim_mode_params.mode->vdisplay,
			      DRM_FORMAT_XRGB8888,
			      LOCAL_I915_FORMAT_MOD_X_TILED, &tmp_fbs[r]);

	for (r = 0; r < pattern->n_rects; r++)
		igt_draw_fill_fb(drm.fd, &tmp_fbs[r], 0xFF);

	if (pattern->frames_stack) {
		for (r = 0; r < pattern->n_rects; r++)
			for (r_ = 0; r_ <= r; r_++)
				draw_rect_igt_fb(pattern, &tmp_fbs[r],
						 IGT_DRAW_PWRITE, r_);
	} else {
		for (r = 0; r < pattern->n_rects; r++)
			draw_rect_igt_fb(pattern, &tmp_fbs[r], IGT_DRAW_PWRITE,
					 r);
	}

	for (r = 0; r < pattern->n_rects; r++) {
		rc = drmModeSetCrtc(drm.fd, prim_mode_params.crtc_id,
				   tmp_fbs[r].fb_id, 0, 0,
				   &prim_mode_params.connector_id, 1,
				   prim_mode_params.mode);
		igt_assert(rc == 0);
		collect_crcs(&pattern->crcs[r]);
	}

	for (r = 0; r < pattern->n_rects; r++) {
		igt_debug("Rect %d CRC:", r);
		print_crc("", &pattern->crcs[r]);
	}

	unset_all_crtcs();

	for (r = 0; r < pattern->n_rects; r++)
		igt_remove_fb(drm.fd, &tmp_fbs[r]);

	pattern->initialized = true;
}

static void setup_drm(void)
{
	int i;

	drm.fd = drm_open_any_master();

	drm.res = drmModeGetResources(drm.fd);
	igt_assert(drm.res->count_connectors <= MAX_CONNECTORS);

	for (i = 0; i < drm.res->count_connectors; i++)
		drm.connectors[i] = drmModeGetConnector(drm.fd,
						drm.res->connectors[i]);

	drm.planes = drmModeGetPlaneResources(drm.fd);

	drm.bufmgr = drm_intel_bufmgr_gem_init(drm.fd, 4096);
	igt_assert(drm.bufmgr);
	drm_intel_bufmgr_gem_enable_reuse(drm.bufmgr);
}

static void teardown_drm(void)
{
	int i;

	drm_intel_bufmgr_destroy(drm.bufmgr);

	drmModeFreePlaneResources(drm.planes);

	for (i = 0; i < drm.res->count_connectors; i++)
		drmModeFreeConnector(drm.connectors[i]);

	drmModeFreeResources(drm.res);
	close(drm.fd);
}

static void setup_modeset(void)
{
	igt_require(init_modeset_cached_params());
	offscreen_fb.fb = NULL;
	offscreen_fb.w = 1024;
	offscreen_fb.h = 1024;
	create_fbs();
	kmstest_set_vt_graphics_mode();
}

static void teardown_modeset(void)
{
	if (scnd_mode_params.connector_id) {
		igt_remove_fb(drm.fd, &fbs.scnd_pri);
		igt_remove_fb(drm.fd, &fbs.scnd_cur);
		igt_remove_fb(drm.fd, &fbs.scnd_spr);
	}
	igt_remove_fb(drm.fd, &fbs.prim_pri);
	igt_remove_fb(drm.fd, &fbs.prim_cur);
	igt_remove_fb(drm.fd, &fbs.prim_spr);
	igt_remove_fb(drm.fd, &fbs.offscreen);
	igt_remove_fb(drm.fd, &fbs.big);
}

static void setup_sink_crc(void)
{
	ssize_t rc;
	sink_crc_t crc;
	int errno_;
	drmModeConnectorPtr c;

	c = get_connector(prim_mode_params.connector_id);
	if (c->connector_type != DRM_MODE_CONNECTOR_eDP) {
		igt_info("Sink CRC not supported: primary screen is not eDP\n");
		return;
	}

	/* We need to make sure there's a mode set on the eDP screen and it's
	 * not on DPMS state, otherwise we fall into the "Unexpected sink CRC
	 * error" case. */
	prim_mode_params.fb.fb = &fbs.prim_pri;
	prim_mode_params.fb.x = prim_mode_params.fb.y = 0;
	fill_fb_region(&prim_mode_params.fb, 0xFF);
	unset_all_crtcs();
	set_mode_for_params(&prim_mode_params);

	sink_crc.fd = igt_debugfs_open("i915_sink_crc_eDP1", O_RDONLY);
	igt_assert(sink_crc.fd >= 0);

	rc = read(sink_crc.fd, crc.data, SINK_CRC_SIZE);
	errno_ = errno;
	if (rc == -1 && errno_ == ENOTTY)
		igt_info("Sink CRC not supported: panel doesn't support it\n");
	else if (rc == SINK_CRC_SIZE)
		sink_crc.supported = true;
	else
		igt_info("Unexpected sink CRC error, rc=:%ld errno:%d %s\n",
			 rc, errno_, strerror(errno_));
}

static void setup_crcs(void)
{
	pipe_crc = igt_pipe_crc_new(0, INTEL_PIPE_CRC_SOURCE_AUTO);

	setup_sink_crc();

	init_blue_crc();

	pattern1.initialized = false;
	pattern1.frames_stack = true;
	pattern1.n_rects = 4;
	pattern1.crcs = NULL;
	pattern1.get_rect = pat1_get_rect;

	pattern2.initialized = false;
	pattern2.frames_stack = true;
	pattern2.n_rects = 4;
	pattern2.crcs = NULL;
	pattern2.get_rect = pat2_get_rect;

	pattern3.initialized = false;
	pattern3.frames_stack = false;
	pattern3.n_rects = 5;
	pattern3.crcs = NULL;
	pattern3.get_rect = pat3_get_rect;

	pattern4.initialized = false;
	pattern4.frames_stack = false;
	pattern4.n_rects = 1;
	pattern4.crcs = NULL;
	pattern4.get_rect = pat4_get_rect;
}

static void teardown_crcs(void)
{
	if (pattern1.crcs)
		free(pattern1.crcs);
	if (pattern2.crcs)
		free(pattern2.crcs);
	if (pattern3.crcs)
		free(pattern3.crcs);
	if (pattern4.crcs)
		free(pattern4.crcs);

	if (sink_crc.fd != -1)
		close(sink_crc.fd);

	igt_pipe_crc_free(pipe_crc);
}

static bool fbc_supported_on_chipset(void)
{
	char buf[DEBUGFS_MSG_SIZE];

	get_debugfs_string(fbc.fd, buf);

	return !strstr(buf, "FBC unsupported on this chipset\n");
}

static void setup_fbc(void)
{
	fbc.fd = igt_debugfs_open("i915_fbc_status", O_RDONLY);
	igt_assert(fbc.fd >= 0);

	if (!fbc_supported_on_chipset()) {
		igt_info("Can't test FBC: not supported on this chipset\n");
		return;
	}
	fbc.can_test = true;

	fbc_setup_last_action();
	fbc_setup_compressing();
}

static void teardown_fbc(void)
{
	if (fbc.fd != -1)
		close(fbc.fd);
}

static bool psr_sink_has_support(void)
{
	char buf[DEBUGFS_MSG_SIZE];

	get_debugfs_string(psr.fd, buf);

	return strstr(buf, "Sink_Support: yes\n");
}

static void setup_psr(void)
{
	if (get_connector(prim_mode_params.connector_id)->connector_type !=
	    DRM_MODE_CONNECTOR_eDP) {
		igt_info("Can't test PSR: no usable eDP screen.\n");
		return;
	}

	psr.fd = igt_debugfs_open("i915_edp_psr_status", O_RDONLY);
	igt_assert(psr.fd >= 0);

	if (!psr_sink_has_support()) {
		igt_info("Can't test PSR: not supported by sink.\n");
		return;
	}
	psr.can_test = true;
}

static void teardown_psr(void)
{
	if (psr.fd != -1)
		close(psr.fd);
}

static void setup_environment(void)
{
	setup_drm();
	setup_modeset();

	setup_fbc();
	setup_psr();

	setup_crcs();
}

static void teardown_environment(void)
{
	stop_busy_thread();

	teardown_crcs();
	teardown_psr();
	teardown_fbc();
	teardown_modeset();
	teardown_drm();
}

static void wait_user(int step, const char *msg)
{
	if (opt.step < step)
		return;

	igt_info("%s Press enter...\n", msg);
	while (getchar() != '\n')
		;
}

static struct modeset_params *pick_params(const struct test_mode *t)
{
	switch (t->screen) {
	case SCREEN_PRIM:
		return &prim_mode_params;
	case SCREEN_SCND:
		return &scnd_mode_params;
	case SCREEN_OFFSCREEN:
		return NULL;
	default:
		igt_assert(false);
	}
}

static struct fb_region *pick_target(const struct test_mode *t,
				     struct modeset_params *params)
{
	if (!params)
		return &offscreen_fb;

	switch (t->plane) {
	case PLANE_PRI:
		return &params->fb;
	case PLANE_CUR:
		return &params->cursor;
	case PLANE_SPR:
		return &params->sprite;
	default:
		igt_assert(false);
	}
}

static void do_flush(const struct test_mode *t)
{
	struct modeset_params *params = pick_params(t);
	struct fb_region *target = pick_target(t, params);

	gem_set_domain(drm.fd, target->fb->gem_handle, I915_GEM_DOMAIN_GTT, 0);
}

#define DONT_ASSERT_CRC			(1 << 0)

#define FBC_ASSERT_FLAGS		(0xF << 1)
#define ASSERT_FBC_ENABLED		(1 << 1)
#define ASSERT_FBC_DISABLED		(1 << 2)
#define ASSERT_LAST_ACTION_CHANGED	(1 << 3)
#define ASSERT_NO_ACTION_CHANGE		(1 << 4)

#define PSR_ASSERT_FLAGS		(3 << 5)
#define ASSERT_PSR_ENABLED		(1 << 5)
#define ASSERT_PSR_DISABLED		(1 << 6)

static int adjust_assertion_flags(const struct test_mode *t, int flags)
{
	if (!(flags & ASSERT_FBC_DISABLED))
		flags |= ASSERT_FBC_ENABLED;
	if (!(flags & ASSERT_PSR_DISABLED))
		flags |= ASSERT_PSR_ENABLED;

	if ((t->feature & FEATURE_FBC) == 0)
		flags &= ~FBC_ASSERT_FLAGS;
	if ((t->feature & FEATURE_PSR) == 0)
		flags &= ~PSR_ASSERT_FLAGS;

	return flags;
}

#define do_crc_assertions(flags) do {					\
	int flags__ = (flags);						\
	struct both_crcs crc_;						\
									\
	if (!opt.check_crc || (flags__ & DONT_ASSERT_CRC))		\
		break;							\
									\
	collect_crcs(&crc_);						\
	print_crc("Calculated CRC:", &crc_);				\
									\
	igt_assert(wanted_crc);						\
	igt_assert_crc_equal(&crc_.pipe, &wanted_crc->pipe);		\
	assert_sink_crc_equal(&crc_.sink, &wanted_crc->sink);		\
} while (0)

#define do_assertions(flags) do {					\
	int flags_ = adjust_assertion_flags(t, (flags));		\
									\
	wait_user(2, "Paused before assertions.");			\
									\
	/* Check the CRC to make sure the drawing operations work	\
	 * immediately, independently of the features being enabled. */	\
	do_crc_assertions(flags_);					\
									\
	/* Now we can flush things to make the test faster. */		\
	do_flush(t);							\
									\
	if (opt.check_status) {						\
		if (flags_ & ASSERT_FBC_ENABLED) {			\
			igt_assert(fbc_wait_until_enabled());		\
									\
			if (fbc.supports_compressing && 		\
			    opt.fbc_check_compression)			\
				igt_assert(fbc_wait_for_compression());	\
		} else if (flags_ & ASSERT_FBC_DISABLED) {		\
			igt_assert(!fbc_wait_until_enabled());		\
		}							\
									\
		if (flags_ & ASSERT_PSR_ENABLED)			\
			igt_assert(psr_wait_until_enabled());		\
		else if (flags_ & ASSERT_PSR_DISABLED)			\
			igt_assert(!psr_wait_until_enabled());		\
	} else {							\
		/* Make sure we settle before continuing. */		\
		sleep(1);						\
	}								\
									\
	/* Check CRC again to make sure the compressed screen is ok,	\
	 * except if we're not drawing on the primary screen. On this	\
	 * case, the first check should be enough and a new CRC check	\
	 * would only delay the test suite while adding no value to the	\
	 * test suite. */						\
	if (t->screen == SCREEN_PRIM)					\
		do_crc_assertions(flags_);				\
									\
	if (fbc.supports_last_action && opt.fbc_check_last_action) {	\
		if (flags_ & ASSERT_LAST_ACTION_CHANGED)		\
			igt_assert(fbc_last_action_changed());		\
		else if (flags_ & ASSERT_NO_ACTION_CHANGE)		\
			igt_assert(!fbc_last_action_changed());		\
	}								\
									\
	wait_user(1, "Paused after assertions.");			\
} while (0)

static void enable_prim_screen_and_wait(const struct test_mode *t)
{
	fill_fb_region(&prim_mode_params.fb, 0xFF);
	set_mode_for_params(&prim_mode_params);

	wanted_crc = &blue_crc;
	fbc_update_last_action();

	do_assertions(ASSERT_NO_ACTION_CHANGE);
}

static void enable_scnd_screen_and_wait(const struct test_mode *t)
{
	fill_fb_region(&scnd_mode_params.fb, 0x80);
	set_mode_for_params(&scnd_mode_params);

	do_assertions(ASSERT_NO_ACTION_CHANGE);
}

static void set_cursor_for_test(const struct test_mode *t,
				struct modeset_params *params)
{
	int rc;

	fill_fb_region(&params->cursor, 0xFF0000FF);

	rc = drmModeMoveCursor(drm.fd, params->crtc_id, 0, 0);
	igt_assert(rc == 0);

	rc = drmModeSetCursor(drm.fd, params->crtc_id,
			      params->cursor.fb->gem_handle,
			      params->cursor.w,
			      params->cursor.h);
	igt_assert(rc == 0);

	do_assertions(ASSERT_NO_ACTION_CHANGE);
}

static void set_sprite_for_test(const struct test_mode *t,
				struct modeset_params *params)
{
	int rc;

	fill_fb_region(&params->sprite, 0xFF0000FF);

	rc = drmModeSetPlane(drm.fd, params->sprite_id, params->crtc_id,
			     params->sprite.fb->fb_id, 0, 0, 0,
			     params->sprite.w, params->sprite.h,
			     0, 0, params->sprite.w << 16,
			     params->sprite.h << 16);
	igt_assert(rc == 0);

	do_assertions(ASSERT_NO_ACTION_CHANGE);
}

static void enable_features_for_test(const struct test_mode *t)
{
	if (t->feature & FEATURE_FBC)
		fbc_enable();
	if (t->feature & FEATURE_PSR)
		psr_enable();
}

static void check_test_requirements(const struct test_mode *t)
{
	if (t->pipes == PIPE_DUAL)
		igt_require_f(scnd_mode_params.connector_id,
			    "Can't test dual pipes with the current outputs\n");

	if (t->feature & FEATURE_FBC)
		igt_require_f(fbc.can_test,
			      "Can't test FBC with this chipset\n");

	if (t->feature & FEATURE_PSR) {
		igt_require_f(psr.can_test,
			      "Can't test PSR with the current outputs\n");
		igt_require_f(sink_crc.supported,
			      "Can't test PSR without sink CRCs\n");
	}

	if (opt.only_feature != FEATURE_COUNT)
		igt_require(t->feature == opt.only_feature);

	if (opt.only_pipes != PIPE_COUNT)
		igt_require(t->pipes == opt.only_pipes);
}

static void set_crtc_fbs(const struct test_mode *t)
{
	switch (t->fbs) {
	case FBS_INDIVIDUAL:
		prim_mode_params.fb.fb = &fbs.prim_pri;
		scnd_mode_params.fb.fb = &fbs.scnd_pri;
		offscreen_fb.fb = &fbs.offscreen;

		prim_mode_params.fb.x = 0;
		scnd_mode_params.fb.x = 0;
		offscreen_fb.x = 0;

		prim_mode_params.fb.y = 0;
		scnd_mode_params.fb.y = 0;
		offscreen_fb.y = 0;
		break;
	case FBS_SHARED:
		/* Please see the comment at the top of create_big_fb(). */
		prim_mode_params.fb.fb = &fbs.big;
		scnd_mode_params.fb.fb = &fbs.big;
		offscreen_fb.fb = &fbs.big;

		prim_mode_params.fb.x = BIGFB_X_OFFSET;
		scnd_mode_params.fb.x = BIGFB_X_OFFSET;
		offscreen_fb.x = BIGFB_X_OFFSET;

		prim_mode_params.fb.y = BIGFB_Y_OFFSET;
		scnd_mode_params.fb.y = prim_mode_params.fb.y +
					prim_mode_params.fb.h;
		offscreen_fb.y = scnd_mode_params.fb.y + scnd_mode_params.fb.h;
		break;
	default:
		igt_assert(false);
	}

	prim_mode_params.cursor.fb = &fbs.prim_cur;
	prim_mode_params.sprite.fb = &fbs.prim_spr;
	scnd_mode_params.cursor.fb = &fbs.scnd_cur;
	scnd_mode_params.sprite.fb = &fbs.scnd_spr;
}

static void prepare_subtest(const struct test_mode *t,
			    struct draw_pattern_info *pattern)
{
	check_test_requirements(t);

	stop_busy_thread();

	disable_features();
	set_crtc_fbs(t);

	if (t->screen == SCREEN_OFFSCREEN)
		fill_fb_region(&offscreen_fb, 0x80);

	unset_all_crtcs();
	init_crcs(pattern);
	enable_features_for_test(t);

	enable_prim_screen_and_wait(t);
	if (t->screen == SCREEN_PRIM) {
		if (t->plane == PLANE_CUR)
			set_cursor_for_test(t, &prim_mode_params);
		if (t->plane == PLANE_SPR)
			set_sprite_for_test(t, &prim_mode_params);
	}

	if (t->pipes == PIPE_SINGLE)
		return;

	enable_scnd_screen_and_wait(t);
	if (t->screen == SCREEN_SCND) {
		if (t->plane == PLANE_CUR)
			set_cursor_for_test(t, &scnd_mode_params);
		if (t->plane == PLANE_SPR)
			set_sprite_for_test(t, &scnd_mode_params);
	}
}

/*
 * rte - the basic sanity test
 *
 * METHOD
 *   Just disable all screens, assert everything is disabled, then enable all
 *   screens - including primary, cursor and sprite planes - and assert that
 *   the tested feature is enabled.
 *
 * EXPECTED RESULTS
 *   Blue screens and t->feature enabled.
 *
 * FAILURES
 *   A failure here means that every other subtest will probably fail too. It
 *   probably means that the Kernel is just not enabling the feature we want.
 */
static void rte_subtest(const struct test_mode *t)
{
	check_test_requirements(t);

	disable_features();
	set_crtc_fbs(t);

	enable_features_for_test(t);
	unset_all_crtcs();
	do_assertions(ASSERT_FBC_DISABLED | ASSERT_PSR_DISABLED |
		      DONT_ASSERT_CRC);

	enable_prim_screen_and_wait(t);
	set_cursor_for_test(t, &prim_mode_params);
	set_sprite_for_test(t, &prim_mode_params);

	if (t->pipes == PIPE_SINGLE)
		return;

	enable_scnd_screen_and_wait(t);
	set_cursor_for_test(t, &scnd_mode_params);
	set_sprite_for_test(t, &scnd_mode_params);
}

static void update_wanted_crc(const struct test_mode *t, struct both_crcs *crc)
{
	if (t->screen == SCREEN_PRIM)
		wanted_crc = crc;
}

static bool op_disables_psr(const struct test_mode *t,
			    enum igt_draw_method method)
{
	if (method != IGT_DRAW_MMAP_GTT && method != IGT_DRAW_MMAP_WC)
		return false;
	if (t->screen == SCREEN_PRIM)
		return true;
	/* On FBS_SHARED, even if the target is not the PSR screen
	 * (SCREEN_PRIM), all primary planes share the same frontbuffer, so a
	 * write to the second screen primary plane - or offscreen plane - will
	 * touch the framebuffer that's also used by the primary screen. */
	if (t->fbs == FBS_SHARED && t->plane == PLANE_PRI)
		return true;

	return false;
}

/*
 * draw - draw a set of rectangles on the screen using the provided method
 *
 * METHOD
 *   Just set the screens as appropriate and then start drawing a series of
 *   rectangles on the target screen. The important guy here is the drawing
 *   method used.
 *
 * EXPECTED RESULTS
 *   The feature either stays enabled or gets reenabled after the oprations. You
 *   will also see the rectangles on the target screen.
 *
 * FAILURES
 *   A failure here indicates a problem somewhere between the Kernel's
 *   frontbuffer tracking infrastructure or the feature itself. You need to pay
 *   attention to which drawing method is being used.
 */
static void draw_subtest(const struct test_mode *t)
{
	int r;
	int assertions = 0;
	struct draw_pattern_info *pattern;
	struct modeset_params *params = pick_params(t);
	struct fb_region *target;

	switch (t->screen) {
	case SCREEN_PRIM:
		if (t->method != IGT_DRAW_MMAP_GTT && t->plane == PLANE_PRI)
			assertions |= ASSERT_LAST_ACTION_CHANGED;
		else
			assertions |= ASSERT_NO_ACTION_CHANGE;
		break;
	case SCREEN_SCND:
	case SCREEN_OFFSCREEN:
		assertions |= ASSERT_NO_ACTION_CHANGE;
		break;
	default:
		igt_assert(false);
	}

	switch (t->plane) {
	case PLANE_PRI:
		pattern = &pattern1;
		break;
	case PLANE_CUR:
	case PLANE_SPR:
		pattern = &pattern2;
		break;
	default:
		igt_assert(false);
	}

	if (op_disables_psr(t, t->method))
		assertions |= ASSERT_PSR_DISABLED;

	prepare_subtest(t, pattern);
	target = pick_target(t, params);

	for (r = 0; r < pattern->n_rects; r++) {
		igt_debug("Drawing rect %d\n", r);
		draw_rect(pattern, target, t->method, r);
		update_wanted_crc(t, &pattern->crcs[r]);
		do_assertions(assertions);
	}
}

/*
 * multidraw - draw a set of rectangles on the screen using alternated drawing
 *             methods
 *
 * METHOD
 *   This is just like the draw subtest, but now we keep alternating between two
 *   drawing methods. Each time we run multidraw_subtest we will test all the
 *   possible pairs containing t->method.
 *
 * EXPECTED RESULTS
 *   The same as the draw subtest.
 *
 * FAILURES
 *   If you get a failure here, first you need to check whether you also get
 *   failures on the individual draw subtests. If yes, then go fix every single
 *   draw subtest first. If all the draw subtests pass but this one fails, then
 *   you have to study how one drawing method is stopping the other from
 *   properly working.
 */
static void multidraw_subtest(const struct test_mode *t)
{
	int r;
	int assertions = 0;
	struct draw_pattern_info *pattern;
	struct modeset_params *params = pick_params(t);
	struct fb_region *target;
	enum igt_draw_method m, used_method;
	uint32_t color;

	switch (t->plane) {
	case PLANE_PRI:
		pattern = &pattern1;
		break;
	case PLANE_CUR:
	case PLANE_SPR:
		pattern = &pattern2;
		break;
	default:
		igt_assert(false);
	}

	prepare_subtest(t, pattern);
	target = pick_target(t, params);

	for (m = 0; m < IGT_DRAW_METHOD_COUNT; m++) {
		if (m == t->method)
			continue;

		igt_debug("Method %s\n", igt_draw_get_method_name(m));
		for (r = 0; r < pattern->n_rects; r++) {
			used_method = (r % 2 == 0) ? t->method : m;

			igt_debug("Used method %s\n",
				  igt_draw_get_method_name(used_method));

			draw_rect(pattern, target, used_method, r);
			update_wanted_crc(t, &pattern->crcs[r]);

			assertions = used_method != IGT_DRAW_MMAP_GTT ?
				     ASSERT_LAST_ACTION_CHANGED :
				     ASSERT_NO_ACTION_CHANGE;
			if (op_disables_psr(t, used_method))
				assertions |= ASSERT_PSR_DISABLED;

			do_assertions(assertions);
		}

		switch (t->plane) {
		case PLANE_PRI:
			color = 0xFF;
			break;
		case PLANE_CUR:
		case PLANE_SPR:
			color = 0xFF0000FF;
			break;
		default:
			igt_assert(false);
		}
		fill_fb_region(target, color);

		update_wanted_crc(t, &blue_crc);
		do_assertions(ASSERT_NO_ACTION_CHANGE);
	}
}

static void flip_handler(int fd, unsigned int sequence, unsigned int tv_sec,
			 unsigned int tv_usec, void *data)
{
	igt_debug("Flip event received.\n");
}

static void wait_flip_event(void)
{
	int rc;
	drmEventContext evctx;
	struct pollfd pfd;

	evctx.version = DRM_EVENT_CONTEXT_VERSION;
	evctx.vblank_handler = NULL;
	evctx.page_flip_handler = flip_handler;

	pfd.fd = drm.fd;
	pfd.events = POLLIN;
	pfd.revents = 0;

	rc = poll(&pfd, 1, 1000);
	switch (rc) {
	case 0:
		igt_assert_f(false, "Poll timeout\n");
		break;
	case 1:
		rc = drmHandleEvent(drm.fd, &evctx);
		igt_assert(rc == 0);
		break;
	default:
		igt_assert_f(false, "Unexpected poll rc %d\n", rc);
		break;
	}
}

static void page_flip_for_params(struct modeset_params *params,
				 enum flip_type type)
{
	int rc;

	switch (type) {
	case FLIP_PAGEFLIP:
		rc = drmModePageFlip(drm.fd, params->crtc_id,
				     params->fb.fb->fb_id, 0, NULL);
		igt_assert(rc == 0);
		break;
	case FLIP_PAGEFLIP_EVENT:
		rc = drmModePageFlip(drm.fd, params->crtc_id,
				     params->fb.fb->fb_id,
				     DRM_MODE_PAGE_FLIP_EVENT, NULL);
		igt_assert(rc == 0);
		wait_flip_event();
		break;
	case FLIP_MODESET:
		set_mode_for_params(params);
		break;
	default:
		igt_assert(false);
	}
}

/*
 * flip - just exercise page flips with the patterns we have
 *
 * METHOD
 *   We draw the pattern on a backbuffer using the provided method, then we
 *   flip, making this the frontbuffer. We can flip both using the dedicated
 *   pageflip IOCTL or the modeset IOCTL.
 *
 * EXPECTED RESULTS
 *   Everything works as expected, screen contents are properly updated.
 *
 * FAILURES
 *   On a failure here you need to go directly to the Kernel's flip code and see
 *   how it interacts with the feature being tested.
 */
static void flip_subtest(const struct test_mode *t, enum flip_type type)
{
	int r;
	int assertions = 0;
	struct igt_fb fb2, *orig_fb;
	struct modeset_params *params = pick_params(t);
	struct draw_pattern_info *pattern = &pattern1;
	uint32_t bg_color;

	switch (t->screen) {
	case SCREEN_PRIM:
		assertions |= ASSERT_LAST_ACTION_CHANGED;
		bg_color = 0xFF;
		break;
	case SCREEN_SCND:
		assertions |= ASSERT_NO_ACTION_CHANGE;
		bg_color = 0x80;
		break;
	default:
		igt_assert(false);
	}

	prepare_subtest(t, pattern);

	igt_create_fb(drm.fd, params->fb.fb->width, params->fb.fb->height,
		      DRM_FORMAT_XRGB8888, LOCAL_I915_FORMAT_MOD_X_TILED, &fb2);
	igt_draw_fill_fb(drm.fd, &fb2, bg_color);
	orig_fb = params->fb.fb;

	for (r = 0; r < pattern->n_rects; r++) {
		params->fb.fb = (r % 2 == 0) ? &fb2 : orig_fb;

		if (r != 0)
			draw_rect(pattern, &params->fb, t->method, r - 1);
		draw_rect(pattern, &params->fb, t->method, r);
		update_wanted_crc(t, &pattern->crcs[r]);

		page_flip_for_params(params, type);

		do_assertions(assertions);
	}

	igt_remove_fb(drm.fd, &fb2);
}

/*
 * move - just move the sprite or cursor around
 *
 * METHOD
 *   Move the surface around, following the defined pattern.
 *
 * EXPECTED RESULTS
 *   The move operations are properly detected by the Kernel, and the screen is
 *   properly updated every time.
 *
 * FAILURES
 *   If you get a failure here, check how the Kernel is enabling or disabling
 *   your feature when it moves the planes around.
 */
static void move_subtest(const struct test_mode *t)
{
	int r, rc;
	int assertions = ASSERT_NO_ACTION_CHANGE;
	struct modeset_params *params = pick_params(t);
	struct draw_pattern_info *pattern = &pattern3;
	bool repeat = false;

	prepare_subtest(t, pattern);

	/* Just paint the right color since we start at 0x0. */
	draw_rect(pattern, pick_target(t, params), t->method, 0);
	update_wanted_crc(t, &pattern->crcs[0]);

	do_assertions(assertions);

	for (r = 1; r < pattern->n_rects; r++) {
		struct rect rect = pattern->get_rect(&params->fb, r);

		switch (t->plane) {
		case PLANE_CUR:
			rc = drmModeMoveCursor(drm.fd, params->crtc_id, rect.x,
					       rect.y);
			igt_assert(rc == 0);
			break;
		case PLANE_SPR:
			rc = drmModeSetPlane(drm.fd, params->sprite_id,
					     params->crtc_id,
					     params->sprite.fb->fb_id, 0,
					     rect.x, rect.y, rect.w,
					     rect.h, 0, 0, rect.w << 16,
					     rect.h << 16);
			igt_assert(rc == 0);
			break;
		default:
			igt_assert(false);
		}
		update_wanted_crc(t, &pattern->crcs[r]);

		do_assertions(assertions);

		/* "Move" the last rect to the same position just to make sure
		 * this works too. */
		if (r+1 == pattern->n_rects && !repeat) {
			repeat = true;
			r--;
		}
	}
}

/*
 * onoff - just enable and disable the sprite or cursor plane a few times
 *
 * METHOD
 *   Just enable and disable the desired plane a few times.
 *
 * EXPECTED RESULTS
 *   Everything is properly detected by the Kernel and the screen contents are
 *   accurate.
 *
 * FAILURES
 *   As usual, if you get a failure here you need to check how the feature is
 *   being handled when the planes are enabled or disabled.
 */
static void onoff_subtest(const struct test_mode *t)
{
	int r, rc;
	int assertions = ASSERT_NO_ACTION_CHANGE;
	struct modeset_params *params = pick_params(t);
	struct draw_pattern_info *pattern = &pattern3;

	prepare_subtest(t, pattern);

	/* Just paint the right color since we start at 0x0. */
	draw_rect(pattern, pick_target(t, params), t->method, 0);
	update_wanted_crc(t, &pattern->crcs[0]);
	do_assertions(assertions);

	for (r = 0; r < 4; r++) {
		if (r % 2 == 0) {
			switch (t->plane) {
			case PLANE_CUR:
				rc = drmModeSetCursor(drm.fd, params->crtc_id,
						      0, 0, 0);
				igt_assert(rc == 0);
				break;
			case PLANE_SPR:
				rc = drmModeSetPlane(drm.fd, params->sprite_id,
						     0, 0, 0, 0, 0, 0, 0, 0, 0,
						     0, 0);
				igt_assert(rc == 0);
				break;
			default:
				igt_assert(false);
			}
			update_wanted_crc(t, &blue_crc);

		} else {
			switch (t->plane) {
			case PLANE_CUR:
				rc = drmModeSetCursor(drm.fd, params->crtc_id,
						  params->cursor.fb->gem_handle,
						  params->cursor.w,
						  params->cursor.h);
				igt_assert(rc == 0);
				break;
			case PLANE_SPR:
				rc = drmModeSetPlane(drm.fd, params->sprite_id,
						     params->crtc_id,
						     params->sprite.fb->fb_id,
						     0, 0, 0, params->sprite.w,
						     params->sprite.h, 0,
						     0,
						     params->sprite.w << 16,
						     params->sprite.h << 16);
				igt_assert(rc == 0);
				break;
			default:
				igt_assert(false);
			}
			update_wanted_crc(t, &pattern->crcs[0]);

		}

		do_assertions(assertions);
	}
}

static bool plane_is_primary(uint32_t plane_id)
{
	int i;
	bool found, is_primary;
	uint64_t prop_value;
	drmModePropertyPtr prop;
	const char *enum_name = NULL;

	found = kmstest_get_property(drm.fd, plane_id, DRM_MODE_OBJECT_PLANE,
				     "type", NULL, &prop_value, &prop);
	if (!found) {
		igt_debug("Property not found\n");
		return false;
	}
	if (!(prop->flags & DRM_MODE_PROP_ENUM)) {
		igt_debug("Property is not an enum\n");
		return false;
	}
	if (prop_value >= prop->count_enums) {
		igt_debug("Bad property value\n");
		return false;
	}

	for (i = 0; i < prop->count_enums; i++) {
		if (prop->enums[i].value == prop_value) {
			enum_name = prop->enums[i].name;
			break;
		}
	}
	if (!enum_name) {
		igt_debug("Enum name not found\n");
		return false;
	}

	is_primary = (strcmp(enum_name, "Primary") == 0);
	drmModeFreeProperty(prop);
	return is_primary;
}

static bool prim_plane_disabled(void)
{
	int i, rc;
	bool disabled, found = false;
	drmModePlaneResPtr planes;

	rc = drmSetClientCap(drm.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	igt_assert(rc == 0);

	planes = drmModeGetPlaneResources(drm.fd);
	for (i = 0; i < planes->count_planes; i++) {
		drmModePlanePtr plane;

		plane = drmModeGetPlane(drm.fd, planes->planes[i]);
		if (!plane) {
			igt_debug("Failed to get plane\n");
			goto fail;
		}

		/* We just pick the first CRTC for the primary plane. */
		if ((plane->possible_crtcs & 0x1) &&
		    plane_is_primary(plane->plane_id)) {
			found = true;
			disabled = (plane->crtc_id == 0);
		}
		drmModeFreePlane(plane);
	}
	drmModeFreePlaneResources(planes);

	if (!found) {
		igt_debug("Primary plane not found\n");
		goto fail;
	}

	rc = drmSetClientCap(drm.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 0);
	igt_assert(rc == 0);

	return disabled;

fail:
	/* Make sure we do this before failing any assertions so we don't mess
	 * the other subtests. */
	rc = drmSetClientCap(drm.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 0);
	igt_assert(rc == 0);
	igt_assert(false);
	return false;
}

/*
 * fullscreen_plane - put a fullscreen plane covering the whole screen
 *
 * METHOD
 *   As simple as the description above.
 *
 * EXPECTED RESULTS
 *   It depends on the feature being tested. FBC gets disabled, but PSR doesn't.
 *
 * FAILURES
 *   Again, if you get failures here you need to dig into the Kernel code, see
 *   how it is handling your feature on this specific case.
 */
static void fullscreen_plane_subtest(const struct test_mode *t)
{
	struct draw_pattern_info *pattern = &pattern4;
	struct igt_fb fullscreen_fb;
	struct rect rect;
	struct modeset_params *params = pick_params(t);
	int assertions;
	int rc;

	prepare_subtest(t, pattern);

	rect = pattern->get_rect(&params->fb, 0);
	igt_create_fb(drm.fd, rect.w, rect.h, DRM_FORMAT_XRGB8888,
		      LOCAL_I915_FORMAT_MOD_X_TILED, &fullscreen_fb);
	igt_draw_fill_fb(drm.fd, &fullscreen_fb, rect.color);

	rc = drmModeSetPlane(drm.fd, params->sprite_id, params->crtc_id,
			     fullscreen_fb.fb_id, 0, 0, 0, fullscreen_fb.width,
			     fullscreen_fb.height, 0, 0,
			     fullscreen_fb.width << 16,
			     fullscreen_fb.height << 16);
	igt_assert(rc == 0);
	update_wanted_crc(t, &pattern->crcs[0]);

	switch (t->screen) {
	case SCREEN_PRIM:
		assertions = ASSERT_LAST_ACTION_CHANGED;

		if (prim_plane_disabled())
			assertions |= ASSERT_FBC_DISABLED;
		break;
	case SCREEN_SCND:
		assertions = ASSERT_NO_ACTION_CHANGE;
		break;
	default:
		igt_assert(false);
	}
	do_assertions(assertions);

	rc = drmModeSetPlane(drm.fd, params->sprite_id, 0, 0, 0, 0, 0, 0, 0, 0,
			     0, 0, 0);
	igt_assert(rc == 0);

	if (t->screen == SCREEN_PRIM)
		assertions = ASSERT_LAST_ACTION_CHANGED;
	update_wanted_crc(t, &blue_crc);
	do_assertions(assertions);

	igt_remove_fb(drm.fd, &fullscreen_fb);
}

/**
 * modesetfrombusy - modeset from a busy buffer to a non-busy buffer
 *
 * METHOD
 *   Set a mode, make the frontbuffer busy using BLT writes, do a modeset to a
 *   non-busy buffer, then check if the features are enabled. The goal of this
 *   test is to exercise a bug we had on the frontbuffer tracking infrastructure
 *   code.
 *
 * EXPECTED RESULTS
 *   No assertions fail.
 *
 * FAILURES
 *   If you're failing this test, then you probably need "drm/i915: Clear
 *   fb_tracking.busy_bits also for synchronous flips" or any other patch that
 *   properly updates dev_priv->fb_tracking.busy_bits when we're alternating
 *   between buffers with different busyness.
 */
static void modesetfrombusy_subtest(const struct test_mode *t)
{
	struct draw_pattern_info *pattern = &pattern1;
	struct modeset_params *params = pick_params(t);
	struct igt_fb fb2;

	prepare_subtest(t, pattern);

	igt_create_fb(drm.fd, params->fb.fb->width, params->fb.fb->height,
		      DRM_FORMAT_XRGB8888, LOCAL_I915_FORMAT_MOD_X_TILED, &fb2);
	igt_draw_fill_fb(drm.fd, &fb2, 0xFF);

	start_busy_thread(params->fb.fb);
	usleep(10000);

	unset_all_crtcs();
	params->fb.fb = &fb2;
	set_mode_for_params(params);

	do_assertions(0);

	stop_busy_thread();

	igt_remove_fb(drm.fd, &fb2);
}

/**
 * farfromfence - test drawing as far from the fence start as possible
 *
 * METHOD
 *   One of the possible problems with FBC is that if the mode being displayed
 *   is very far away from the fence we might setup the hardware frontbuffer
 *   tracking in the wrong way. So this test tries to set a really tall FB,
 *   makes the CRTC point to the bottom of that FB, then it tries to exercise
 *   the hardware frontbuffer tracking through GTT mmap operations.
 *
 * EXPECTED RESULTS
 *   Everything succeeds.
 *
 * FAILURES
 *   If you're getting wrong CRC calulations, then the hardware tracking might
 *   be misconfigured and needs to be checked. If we're failing because FBC is
 *   disabled and the reason is that there's not enough stolen memory, then the
 *   Kernel might be calculating the amount of stolen memory needed based on the
 *   whole framebuffer size, and not just on the needed size: in this case, you
 *   need a newer Kernel.
 */
static void farfromfence_subtest(const struct test_mode *t)
{
	int r;
	struct igt_fb tall_fb;
	struct modeset_params *params = pick_params(t);
	struct draw_pattern_info *pattern = &pattern1;
	struct fb_region *target;
	int max_height;

	switch (intel_gen(intel_get_drm_devid(drm.fd))) {
	case 2:
		max_height = 2048;
		break;
	case 3:
		max_height = 4096;
		break;
	default:
		max_height = 8192;
		break;
	}

	prepare_subtest(t, pattern);
	target = pick_target(t, params);

	igt_create_fb(drm.fd, params->mode->hdisplay, max_height,
		      DRM_FORMAT_XRGB8888, LOCAL_I915_FORMAT_MOD_X_TILED,
		      &tall_fb);

	igt_draw_fill_fb(drm.fd, &tall_fb, 0xFF);

	params->fb.fb = &tall_fb;
	params->fb.x = 0;
	params->fb.y = max_height - params->mode->vdisplay;
	set_mode_for_params(params);
	do_assertions(0);

	for (r = 0; r < pattern->n_rects; r++) {
		draw_rect(pattern, target, t->method, r);
		update_wanted_crc(t, &pattern->crcs[r]);
		do_assertions(0);
	}

	igt_remove_fb(drm.fd, &tall_fb);
}

static void try_invalid_strides(void)
{
	uint32_t gem_handle;
	int rc;

	/* Sizes that the Kernel shouldn't even allow for tiled */
	gem_handle = gem_create(drm.fd, 2048);

	/* Smaller than 512, yet still 64-byte aligned. */
	rc = __gem_set_tiling(drm.fd, gem_handle, I915_TILING_X, 448);
	igt_assert(rc == -EINVAL);

	/* Bigger than 512, but not 64-byte aligned. */
	rc = __gem_set_tiling(drm.fd, gem_handle, I915_TILING_X, 1022);
	igt_assert(rc == -EINVAL);

	/* Just make sure something actually works. */
	rc = __gem_set_tiling(drm.fd, gem_handle, I915_TILING_X, 1024);
	igt_assert(rc == 0);

	gem_close(drm.fd, gem_handle);
}

/**
 * badstride - try to use buffers with strides that are not supported
 *
 * METHOD
 *   First we try to create buffers with strides that are not allowed for tiled
 *   surfaces and assert the Kernel rejects them. Then we create buffers with
 *   strides that are allowed by the Kernel, but that are incompatible with FBC
 *   and we assert that FBC stays disabled after we set a mode on those buffers.
 *
 * EXPECTED RESULTS
 *   The invalid strides are rejected, and the valid strides that are
 *   incompatible with FBC result in FBC disabled.
 *
 * FAILURES
 *   There are two possible places where the Kernel can be broken: either the
 *   code that checks valid strides for tiled buffers or the code that checks
 *   the valid strides for FBC.
 */
static void badstride_subtest(const struct test_mode *t)
{
	struct igt_fb wide_fb;
	struct modeset_params *params = pick_params(t);

	try_invalid_strides();

	prepare_subtest(t, &pattern4);

	igt_create_fb(drm.fd, params->fb.fb->width + 4096,
		      params->fb.fb->height, DRM_FORMAT_XRGB8888,
		      LOCAL_I915_FORMAT_MOD_X_TILED, &wide_fb);
	igt_assert(wide_fb.stride > 16384);

	igt_draw_fill_fb(drm.fd, &wide_fb, 0xFF);

	params->fb.fb = &wide_fb;
	set_mode_for_params(params);

	do_assertions(ASSERT_FBC_DISABLED);

	igt_remove_fb(drm.fd, &wide_fb);
}

static int opt_handler(int option, int option_index, void *data)
{
	switch (option) {
	case 's':
		opt.check_status = false;
		break;
	case 'c':
		opt.check_crc = false;
		break;
	case 'o':
		opt.fbc_check_compression = false;
		break;
	case 'a':
		opt.fbc_check_last_action = false;
		break;
	case 'e':
		opt.no_edp = true;
		break;
	case 'm':
		opt.small_modes = true;
		break;
	case 'i':
		opt.show_hidden = true;
		break;
	case 't':
		opt.step++;
		break;
	case 'n':
		igt_assert(opt.only_feature == FEATURE_COUNT);
		opt.only_feature = FEATURE_NONE;
		break;
	case 'f':
		igt_assert(opt.only_feature == FEATURE_COUNT);
		opt.only_feature = FEATURE_FBC;
		break;
	case 'p':
		igt_assert(opt.only_feature == FEATURE_COUNT);
		opt.only_feature = FEATURE_PSR;
		break;
	case '1':
		igt_assert(opt.only_pipes == PIPE_COUNT);
		opt.only_pipes = PIPE_SINGLE;
		break;
	case '2':
		igt_assert(opt.only_pipes == PIPE_COUNT);
		opt.only_pipes = PIPE_DUAL;
		break;
	default:
		igt_assert(false);
	}

	return 0;
}

const char *help_str =
"  --no-status-check           Don't check for enable/disable status\n"
"  --no-crc-check              Don't check for CRC values\n"
"  --no-fbc-compression-check  Don't check for the FBC compression status\n"
"  --no-fbc-action-check       Don't check for the FBC last action\n"
"  --no-edp                    Don't use eDP monitors\n"
"  --use-small-modes           Use smaller resolutions for the modes\n"
"  --show-hidden               Show hidden subtests\n"
"  --step                      Stop on each step so you can check the screen\n"
"  --nop-only                  Only run the \"nop\" feature subtests\n"
"  --fbc-only                  Only run the \"fbc\" feature subtests\n"
"  --psr-only                  Only run the \"psr\" feature subtests\n"
"  --1p-only                   Only run subtests that use 1 pipe\n"
"  --2p-only                   Only run subtests that use 2 pipes\n";

static const char *pipes_str(int pipes)
{
	switch (pipes) {
	case PIPE_SINGLE:
		return "1p";
	case PIPE_DUAL:
		return "2p";
	default:
		igt_assert(false);
	}
}

static const char *screen_str(int screen)
{
	switch (screen) {
	case SCREEN_PRIM:
		return "primscrn";
	case SCREEN_SCND:
		return "scndscrn";
	case SCREEN_OFFSCREEN:
		return "offscren";
	default:
		igt_assert(false);
	}
}

static const char *plane_str(int plane)
{
	switch (plane) {
	case PLANE_PRI:
		return "pri";
	case PLANE_CUR:
		return "cur";
	case PLANE_SPR:
		return "spr";
	default:
		igt_assert(false);
	}
}

static const char *fbs_str(int fb)
{
	switch (fb) {
	case FBS_INDIVIDUAL:
		return "indfb";
	case FBS_SHARED:
		return "shrfb";
	default:
		igt_assert(false);
	}
}

static const char *feature_str(int feature)
{
	switch (feature) {
	case FEATURE_NONE:
		return "nop";
	case FEATURE_FBC:
		return "fbc";
	case FEATURE_PSR:
		return "psr";
	case FEATURE_FBC | FEATURE_PSR:
		return "fbcpsr";
	default:
		igt_assert(false);
	}
}

#define TEST_MODE_ITER_BEGIN(t) \
	for (t.feature = 0; t.feature < FEATURE_COUNT; t.feature++) {	   \
	for (t.pipes = 0; t.pipes < PIPE_COUNT; t.pipes++) {		   \
	for (t.screen = 0; t.screen < SCREEN_COUNT; t.screen++) {	   \
	for (t.plane = 0; t.plane < PLANE_COUNT; t.plane++) {		   \
	for (t.fbs = 0; t.fbs < FBS_COUNT; t.fbs++) {			   \
	for (t.method = 0; t.method < IGT_DRAW_METHOD_COUNT; t.method++) { \
		if (t.pipes == PIPE_SINGLE && t.screen == SCREEN_SCND)	   \
			continue;					   \
		if (t.screen == SCREEN_OFFSCREEN && t.plane != PLANE_PRI)  \
			continue;					   \
		if (!opt.show_hidden && t.pipes == PIPE_DUAL &&		   \
		    t.screen == SCREEN_OFFSCREEN)			   \
			continue;					   \
		if ((!opt.show_hidden && opt.only_feature != FEATURE_NONE) \
		    && t.feature == FEATURE_NONE)			   \
			continue;

#define TEST_MODE_ITER_END } } } } } }

int main(int argc, char *argv[])
{
	struct test_mode t;
	struct option long_options[] = {
		{ "no-status-check",          0, 0, 's'},
		{ "no-crc-check",             0, 0, 'c'},
		{ "no-fbc-compression-check", 0, 0, 'o'},
		{ "no-fbc-action-check",      0, 0, 'a'},
		{ "no-edp",                   0, 0, 'e'},
		{ "use-small-modes",          0, 0, 'm'},
		{ "show-hidden",              0, 0, 'i'},
		{ "step",                     0, 0, 't'},
		{ "nop-only",                 0, 0, 'n'},
		{ "fbc-only",                 0, 0, 'f'},
		{ "psr-only",                 0, 0, 'p'},
		{ "1p-only",                  0, 0, '1'},
		{ "2p-only",                  0, 0, '2'},
		{ 0, 0, 0, 0 }
	};

	igt_subtest_init_parse_opts(&argc, argv, "", long_options, help_str,
				    opt_handler, NULL);

	igt_fixture
		setup_environment();

	for (t.feature = 0; t.feature < FEATURE_COUNT; t.feature++) {
		if ((!opt.show_hidden && opt.only_feature != FEATURE_NONE)
		    && t.feature == FEATURE_NONE)
			continue;
		for (t.pipes = 0; t.pipes < PIPE_COUNT; t.pipes++) {
			t.screen = SCREEN_PRIM;
			t.plane = PLANE_PRI;
			t.fbs = FBS_INDIVIDUAL;
			/* Make sure nothing is using this value. */
			t.method = -1;

			igt_subtest_f("%s-%s-rte",
				      feature_str(t.feature),
				      pipes_str(t.pipes))
				rte_subtest(&t);
		}
	}

	TEST_MODE_ITER_BEGIN(t)
		igt_subtest_f("%s-%s-%s-%s-%s-draw-%s",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      screen_str(t.screen),
			      plane_str(t.plane),
			      fbs_str(t.fbs),
			      igt_draw_get_method_name(t.method))
			draw_subtest(&t);
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.plane != PLANE_PRI)
			continue;
		if (t.screen == SCREEN_OFFSCREEN)
			continue;
		if (!opt.show_hidden && t.method != IGT_DRAW_BLT)
			continue;

		igt_subtest_f("%s-%s-%s-%s-flip-%s",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      screen_str(t.screen),
			      fbs_str(t.fbs),
			      igt_draw_get_method_name(t.method))
			flip_subtest(&t, FLIP_PAGEFLIP);

		igt_subtest_f("%s-%s-%s-%s-evflip-%s",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      screen_str(t.screen),
			      fbs_str(t.fbs),
			      igt_draw_get_method_name(t.method))
			flip_subtest(&t, FLIP_PAGEFLIP_EVENT);

		igt_subtest_f("%s-%s-%s-%s-msflip-%s",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      screen_str(t.screen),
			      fbs_str(t.fbs),
			      igt_draw_get_method_name(t.method))
			flip_subtest(&t, FLIP_MODESET);
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.screen == SCREEN_OFFSCREEN)
			continue;
		if (t.method != IGT_DRAW_BLT)
			continue;
		if (t.plane == PLANE_PRI)
			continue;

		igt_subtest_f("%s-%s-%s-%s-%s-move",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      screen_str(t.screen),
			      plane_str(t.plane),
			      fbs_str(t.fbs))
			move_subtest(&t);

		igt_subtest_f("%s-%s-%s-%s-%s-onoff",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      screen_str(t.screen),
			      plane_str(t.plane),
			      fbs_str(t.fbs))
			onoff_subtest(&t);
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.screen == SCREEN_OFFSCREEN)
			continue;
		if (t.method != IGT_DRAW_BLT)
			continue;
		if (t.plane != PLANE_SPR)
			continue;

		igt_subtest_f("%s-%s-%s-%s-%s-fullscreen",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      screen_str(t.screen),
			      plane_str(t.plane),
			      fbs_str(t.fbs))
			fullscreen_plane_subtest(&t);
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.screen != SCREEN_PRIM)
			continue;
		if (!opt.show_hidden && t.fbs != FBS_INDIVIDUAL)
			continue;

		igt_subtest_f("%s-%s-%s-%s-multidraw-%s",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      plane_str(t.plane),
			      fbs_str(t.fbs),
			      igt_draw_get_method_name(t.method))
			multidraw_subtest(&t);
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.pipes != PIPE_SINGLE)
			continue;
		if (t.screen != SCREEN_PRIM)
			continue;
		if (t.plane != PLANE_PRI)
			continue;
		if (t.fbs != FBS_INDIVIDUAL)
			continue;
		if (t.method != IGT_DRAW_MMAP_GTT)
			continue;

		igt_subtest_f("%s-farfromfence", feature_str(t.feature))
			farfromfence_subtest(&t);
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.pipes != PIPE_SINGLE)
			continue;
		if (t.screen != SCREEN_PRIM)
			continue;
		if (t.plane != PLANE_PRI)
			continue;
		if (t.fbs != FBS_INDIVIDUAL)
			continue;
		if (t.method != IGT_DRAW_MMAP_CPU)
			continue;

		igt_subtest_f("%s-modesetfrombusy", feature_str(t.feature))
			modesetfrombusy_subtest(&t);

		if (t.feature & FEATURE_FBC)
			igt_subtest_f("%s-badstride", feature_str(t.feature))
				badstride_subtest(&t);
	TEST_MODE_ITER_END

	/*
	 * TODO: ideas for subtests:
	 * - Add a new enum to struct test_mode that allows us to specify the
	 *   BPP/depth configuration.
	 */

	igt_fixture
		teardown_environment();

	igt_exit();
}
