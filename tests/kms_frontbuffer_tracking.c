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

#include "igt.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>


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

	/* Which features are we going to test now? This is a mask!
	 * FEATURE_DEFAULT is a special value which instruct the test to just
	 * keep what's already enabled by default in the Kernel. */
	enum {
		FEATURE_NONE  = 0,
		FEATURE_FBC   = 1,
		FEATURE_PSR   = 2,
		FEATURE_COUNT = 4,
		FEATURE_DEFAULT = 4,
	} feature;

	/* Possible pixel formats. We just use FORMAT_DEFAULT for most tests and
	 * only test a few things on the other formats. */
	enum pixel_format {
		FORMAT_RGB888 = 0,
		FORMAT_RGB565,
		FORMAT_RGB101010,
		FORMAT_COUNT,
		FORMAT_DEFAULT = FORMAT_RGB888,
	} format;

	/* There are multiple APIs where we can do the equivalent of a page flip
	 * and they exercise slightly different codepaths inside the Kernel. */
	enum flip_type {
		FLIP_PAGEFLIP,
		FLIP_PAGEFLIP_EVENT,
		FLIP_MODESET,
		FLIP_PLANES,
		FLIP_COUNT,
	} flip;

	enum igt_draw_method method;
};

enum color {
	COLOR_RED,
	COLOR_GREEN,
	COLOR_BLUE,
	COLOR_MAGENTA,
	COLOR_CYAN,
	COLOR_SCND_BG,
	COLOR_PRIM_BG = COLOR_BLUE,
	COLOR_OFFSCREEN_BG = COLOR_SCND_BG,
};

struct rect {
	int x;
	int y;
	int w;
	int h;
	uint32_t color;
};

#define MAX_CONNECTORS 32
#define MAX_PLANES 32
struct {
	int fd;
	drmModeResPtr res;
	drmModeConnectorPtr connectors[MAX_CONNECTORS];
	drmModePlaneResPtr plane_res;
	drmModePlanePtr planes[MAX_PLANES];
	uint64_t plane_types[MAX_PLANES];
	drm_intel_bufmgr *bufmgr;
} drm;

struct {
	bool can_test;

	bool supports_compressing;
	bool supports_last_action;

	struct timespec last_action;
} fbc = {
	.can_test = false,
	.supports_last_action = false,
	.supports_compressing = false,
};

struct {
	bool can_test;
} psr = {
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
struct {
	bool initialized;
	struct both_crcs crc;
} blue_crcs[FORMAT_COUNT];
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
	bool frames_stack;
	int n_rects;
	struct rect (*get_rect)(struct fb_region *fb, int r);

	bool initialized[FORMAT_COUNT];
	struct both_crcs *crcs[FORMAT_COUNT];
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
	int only_pipes;
	int shared_fb_x_offset;
	int shared_fb_y_offset;
} opt = {
	.check_status = true,
	.check_crc = true,
	.fbc_check_compression = true,
	.fbc_check_last_action = true,
	.no_edp = false,
	.small_modes = false,
	.show_hidden= false,
	.step = 0,
	.only_pipes = PIPE_COUNT,
	.shared_fb_x_offset = 500,
	.shared_fb_y_offset = 500,
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
struct screen_fbs {
	bool initialized;

	struct igt_fb prim_pri;
	struct igt_fb prim_cur;
	struct igt_fb prim_spr;

	struct igt_fb scnd_pri;
	struct igt_fb scnd_cur;
	struct igt_fb scnd_spr;

	struct igt_fb offscreen;
	struct igt_fb big;
} fbs[FORMAT_COUNT];

struct {
	pthread_t thread;
	bool stop;

	uint32_t handle;
	uint32_t size;
	uint32_t stride;
	int width;
	int height;
	uint32_t color;
	int bpp;
} busy_thread = {
	.stop = true,
};

drmModeModeInfo std_1024_mode = {
	.clock = 65000,
	.hdisplay = 1024,
	.hsync_start = 1048,
	.hsync_end = 1184,
	.htotal = 1344,
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

	for (i = 0; i < drm.plane_res->count_planes && plane_id == 0; i++)
		if ((drm.planes[i]->possible_crtcs & (1 << crtc_index)) &&
		    drm.plane_types[i] == DRM_PLANE_TYPE_OVERLAY)
			plane_id = drm.planes[i]->plane_id;

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

static void create_fb(enum pixel_format pformat, int width, int height,
		      uint64_t tiling, int plane, struct igt_fb *fb)
{
	uint32_t format;
	unsigned int size, stride;
	int bpp;
	uint64_t tiling_for_size;

	switch (pformat) {
	case FORMAT_RGB888:
		if (plane == PLANE_CUR)
			format = DRM_FORMAT_ARGB8888;
		else
			format = DRM_FORMAT_XRGB8888;
		break;
	case FORMAT_RGB565:
		/* Only the primary plane supports 16bpp! */
		if (plane == PLANE_PRI)
			format = DRM_FORMAT_RGB565;
		else if (plane == PLANE_CUR)
			format = DRM_FORMAT_ARGB8888;
		else
			format = DRM_FORMAT_XRGB8888;
		break;
	case FORMAT_RGB101010:
		if (plane == PLANE_PRI)
			format = DRM_FORMAT_XRGB2101010;
		else if (plane == PLANE_CUR)
			format = DRM_FORMAT_ARGB8888;
		else
			format = DRM_FORMAT_XRGB8888;
		break;
	default:
		igt_assert(false);
	}

	/* We want all frontbuffers with the same width/height/format to have
	 * the same size regardless of tiling since we want to properly exercise
	 * the Kernel's specific tiling-checking code paths without accidentally
	 * hitting size-checking ones first. */
	bpp = igt_drm_format_to_bpp(format);
	if (plane == PLANE_CUR)
		tiling_for_size = LOCAL_DRM_FORMAT_MOD_NONE;
	else
		tiling_for_size = LOCAL_I915_FORMAT_MOD_X_TILED;

	igt_calc_fb_size(drm.fd, width, height, bpp, tiling_for_size, &size,
			 &stride);

	igt_create_fb_with_bo_size(drm.fd, width, height, format, tiling, fb,
				   size, stride);
}

static uint32_t pick_color(struct igt_fb *fb, enum color ecolor)
{
	uint32_t color, r, g, b, b2, a;
	bool alpha = false;

	switch (fb->drm_format) {
	case DRM_FORMAT_RGB565:
		a =  0x0;
		r =  0x1F << 11;
		g =  0x3F << 5;
		b =  0x1F;
		b2 = 0x10;
		break;
	case DRM_FORMAT_ARGB8888:
		alpha = true;
	case DRM_FORMAT_XRGB8888:
		a =  0xFF << 24;
		r =  0xFF << 16;
		g =  0xFF << 8;
		b =  0xFF;
		b2 = 0x80;
		break;
	case DRM_FORMAT_ARGB2101010:
		alpha = true;
	case DRM_FORMAT_XRGB2101010:
		a = 0x3 << 30;
		r = 0x3FF << 20;
		g = 0x3FF << 10;
		b = 0x3FF;
		b2 = 0x200;
		break;
	default:
		igt_assert(false);
	}

	switch (ecolor) {
	case COLOR_RED:
		color = r;
		break;
	case COLOR_GREEN:
		color = g;
		break;
	case COLOR_BLUE:
		color = b;
		break;
	case COLOR_MAGENTA:
		color = r | b;
		break;
	case COLOR_CYAN:
		color = g | b;
		break;
	case COLOR_SCND_BG:
		color = b2;
		break;
	default:
		igt_assert(false);
	}

	if (alpha)
		color |= a;

	return color;
}

static void fill_fb(struct igt_fb *fb, enum color ecolor)
{
	igt_draw_fill_fb(drm.fd, fb, pick_color(fb, ecolor));
}

/*
 * This is how the prim, scnd and offscreen FBs should be positioned inside the
 * shared FB. The prim buffer starts at the X and Y offsets defined by
 * opt.shared_fb_{x,y}_offset, then scnd starts at the same X pixel offset,
 * right after prim ends on the Y axis, then the offscreen fb starts after scnd
 * ends. Just like the picture:
 *
 * +-------------------------+
 * | shared fb               |
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
static void create_shared_fb(enum pixel_format format)
{
	int prim_w, prim_h, scnd_w, scnd_h, offs_w, offs_h, big_w, big_h;
	struct screen_fbs *s = &fbs[format];

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
	big_w += opt.shared_fb_x_offset;

	big_h = prim_h + scnd_h + offs_h + opt.shared_fb_y_offset;

	create_fb(format, big_w, big_h, LOCAL_I915_FORMAT_MOD_X_TILED,
		  PLANE_PRI, &s->big);
}

static void create_fbs(enum pixel_format format)
{
	struct screen_fbs *s = &fbs[format];

	if (s->initialized)
		return;

	s->initialized = true;

	create_fb(format, prim_mode_params.mode->hdisplay,
		  prim_mode_params.mode->vdisplay,
		  LOCAL_I915_FORMAT_MOD_X_TILED, PLANE_PRI, &s->prim_pri);
	create_fb(format, prim_mode_params.cursor.w,
		  prim_mode_params.cursor.h, LOCAL_DRM_FORMAT_MOD_NONE,
		  PLANE_CUR, &s->prim_cur);
	create_fb(format, prim_mode_params.sprite.w,
		  prim_mode_params.sprite.h, LOCAL_I915_FORMAT_MOD_X_TILED,
		  PLANE_SPR, &s->prim_spr);

	create_fb(format, offscreen_fb.w, offscreen_fb.h,
		  LOCAL_I915_FORMAT_MOD_X_TILED, PLANE_PRI, &s->offscreen);

	create_shared_fb(format);

	if (!scnd_mode_params.connector_id)
		return;

	create_fb(format, scnd_mode_params.mode->hdisplay,
		  scnd_mode_params.mode->vdisplay,
		  LOCAL_I915_FORMAT_MOD_X_TILED, PLANE_PRI, &s->scnd_pri);
	create_fb(format, scnd_mode_params.cursor.w, scnd_mode_params.cursor.h,
		  LOCAL_DRM_FORMAT_MOD_NONE, PLANE_CUR, &s->scnd_cur);
	create_fb(format, scnd_mode_params.sprite.w, scnd_mode_params.sprite.h,
		  LOCAL_I915_FORMAT_MOD_X_TILED, PLANE_SPR, &s->scnd_spr);
}

static void destroy_fbs(enum pixel_format format)
{
	struct screen_fbs *s = &fbs[format];

	if (!s->initialized)
		return;

	if (scnd_mode_params.connector_id) {
		igt_remove_fb(drm.fd, &s->scnd_pri);
		igt_remove_fb(drm.fd, &s->scnd_cur);
		igt_remove_fb(drm.fd, &s->scnd_spr);
	}
	igt_remove_fb(drm.fd, &s->prim_pri);
	igt_remove_fb(drm.fd, &s->prim_cur);
	igt_remove_fb(drm.fd, &s->prim_spr);
	igt_remove_fb(drm.fd, &s->offscreen);
	igt_remove_fb(drm.fd, &s->big);
}

static bool set_mode_for_params(struct modeset_params *params)
{
	int rc;

	rc = drmModeSetCrtc(drm.fd, params->crtc_id, params->fb.fb->fb_id,
			    params->fb.x, params->fb.y,
			    &params->connector_id, 1, params->mode);
	return (rc == 0);
}

static bool fbc_is_enabled(void)
{
	char buf[128];

	igt_debugfs_read("i915_fbc_status", buf);
	return strstr(buf, "FBC enabled\n");
}

static void fbc_print_status(void)
{
	char buf[128];

	igt_debugfs_read("i915_fbc_status", buf);
	igt_info("FBC status:\n%s\n", buf);
}

static bool psr_is_enabled(void)
{
	char buf[256];

	igt_debugfs_read("i915_edp_psr_status", buf);
	return strstr(buf, "\nActive: yes\n") &&
	       strstr(buf, "\nHW Enabled & Active bit: yes\n");
}

static void psr_print_status(void)
{
	char buf[256];

	igt_debugfs_read("i915_edp_psr_status", buf);
	igt_info("PSR status:\n%s\n", buf);
}

static struct timespec fbc_get_last_action(void)
{
	struct timespec ret = { 0, 0 };
	char buf[128];
	char *action;
	ssize_t n_read;

	igt_debugfs_read("i915_fbc_status", buf);

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
	char buf[128];
	char *action;

	igt_debugfs_read("i915_fbc_status", buf);

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
	char buf[128];

	igt_debugfs_read("i915_fbc_status", buf);
	return strstr(buf, "\nCompressing: yes\n") != NULL;
}

static bool fbc_wait_for_compression(void)
{
	return igt_wait(fbc_is_compressing(), 2000, 1);
}

static void fbc_setup_compressing(void)
{
	char buf[128];

	igt_debugfs_read("i915_fbc_status", buf);

	if (strstr(buf, "\nCompressing:"))
		fbc.supports_compressing = true;
	else
		igt_info("FBC compression information not supported\n");
}

static bool fbc_not_enough_stolen(void)
{
	char buf[128];

	igt_debugfs_read("i915_fbc_status", buf);
	return strstr(buf, "FBC disabled: not enough stolen memory\n");
}

static bool fbc_wait_until_enabled(void)
{
	return igt_wait(fbc_is_enabled(), 2000, 1);
}

static bool psr_wait_until_enabled(void)
{
	return igt_wait(psr_is_enabled(), 5000, 1);
}

#define fbc_enable() igt_set_module_param_int("enable_fbc", 1)
#define fbc_disable() igt_set_module_param_int("enable_fbc", 0)
#define psr_enable() igt_set_module_param_int("enable_psr", 1)
#define psr_disable() igt_set_module_param_int("enable_psr", 0)

static void get_sink_crc(sink_crc_t *crc, bool mandatory)
{
	int rc, errno_;

	lseek(sink_crc.fd, 0, SEEK_SET);

	rc = read(sink_crc.fd, crc->data, SINK_CRC_SIZE);
	errno_ = errno;

	if (rc == -1 && errno_ == ETIMEDOUT) {
		if (mandatory)
			igt_skip("Sink CRC is unreliable on this machine. Try running this test again individually\n");
		else
			igt_info("Sink CRC is unreliable on this machine. Try running this test again individually\n");
	}
	igt_assert(rc == SINK_CRC_SIZE);
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
		rect.color = pick_color(fb->fb, COLOR_GREEN);
		break;
	case 1:
		rect.x = fb->w / 8 * 4;
		rect.y = fb->h / 8 * 4;
		rect.w = fb->w / 8 * 2;
		rect.h = fb->h / 8 * 2;
		rect.color = pick_color(fb->fb, COLOR_RED);
		break;
	case 2:
		rect.x = fb->w / 16 + 1;
		rect.y = fb->h / 16 + 1;
		rect.w = fb->w / 8 + 1;
		rect.h = fb->h / 8 + 1;
		rect.color = pick_color(fb->fb, COLOR_MAGENTA);
		break;
	case 3:
		rect.x = fb->w - 1;
		rect.y = fb->h - 1;
		rect.w = 1;
		rect.h = 1;
		rect.color = pick_color(fb->fb, COLOR_CYAN);
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
		rect.color = pick_color(fb->fb, COLOR_GREEN);
		break;
	case 1:
		rect.x = 31;
		rect.y = 31;
		rect.w = 31;
		rect.h = 31;
		rect.color = pick_color(fb->fb, COLOR_RED);
		break;
	case 2:
		rect.x = 16;
		rect.y = 16;
		rect.w = 32;
		rect.h = 32;
		rect.color = pick_color(fb->fb, COLOR_MAGENTA);
		break;
	case 3:
		rect.color = pick_color(fb->fb, COLOR_CYAN);
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
	rect.color = pick_color(fb->fb, COLOR_GREEN);

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

	igt_assert_eq(r, 0);

	rect.x = 0;
	rect.y = 0;
	rect.w = fb->w;
	rect.h = fb->h;
	rect.color = pick_color(fb->fb, COLOR_GREEN);

	return rect;
}

static void fb_dirty_ioctl(struct fb_region *fb, struct rect *rect)
{
	int rc;
	drmModeClip clip = {
		.x1 = rect->x,
		.x2 = rect->x + rect->w,
		.y1 = rect->y,
		.y2 = rect->y + rect->h,
	};

	rc = drmModeDirtyFB(drm.fd, fb->fb->fb_id, &clip, 1);

	igt_assert(rc == 0 || rc == -ENOSYS);
}

static void draw_rect(struct draw_pattern_info *pattern, struct fb_region *fb,
		      enum igt_draw_method method, int r)
{
	struct rect rect = pattern->get_rect(fb, r);

	igt_draw_rect_fb(drm.fd, drm.bufmgr, NULL, fb->fb, method,
			 fb->x + rect.x, fb->y + rect.y,
			 rect.w, rect.h, rect.color);

	if (method == IGT_DRAW_MMAP_WC)
		fb_dirty_ioctl(fb, &rect);
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

static void fill_fb_region(struct fb_region *region, enum color ecolor)
{
	uint32_t color = pick_color(region->fb, ecolor);

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
		igt_assert_eq(rc, 0);

		rc = drmModeSetCursor(drm.fd, drm.res->crtcs[i], 0, 0, 0);
		igt_assert_eq(rc, 0);
	}

	for (i = 0; i < drm.plane_res->count_planes; i++) {
		rc = drmModeSetPlane(drm.fd, drm.plane_res->planes[i], 0, 0, 0,
				     0, 0, 0, 0, 0, 0, 0, 0);
		igt_assert_eq(rc, 0);
	}
}

static void disable_features(const struct test_mode *t)
{
	if (t->feature == FEATURE_DEFAULT)
		return;

	fbc_disable();
	psr_disable();
}

static void *busy_thread_func(void *data)
{
	while (!busy_thread.stop)
		igt_draw_rect(drm.fd, drm.bufmgr, NULL, busy_thread.handle,
			      busy_thread.size, busy_thread.stride,
			      IGT_DRAW_BLT, 0, 0, busy_thread.width,
			      busy_thread.height, busy_thread.color,
			      busy_thread.bpp);

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
	busy_thread.color = pick_color(fb, COLOR_PRIM_BG);
	busy_thread.bpp = igt_drm_format_to_bpp(fb->drm_format);

	rc = pthread_create(&busy_thread.thread, NULL, busy_thread_func, NULL);
	igt_assert_eq(rc, 0);
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

static void collect_crcs(struct both_crcs *crcs, bool mandatory_sink_crc)
{
	igt_pipe_crc_collect_crc(pipe_crc, &crcs->pipe);

	if (sink_crc.supported)
		get_sink_crc(&crcs->sink, mandatory_sink_crc);
	else
		memcpy(&crcs->sink, "unsupported!", SINK_CRC_SIZE);
}

static void init_blue_crc(enum pixel_format format, bool mandatory_sink_crc)
{
	struct igt_fb blue;
	int rc;

	if (blue_crcs[format].initialized)
		return;

	create_fb(format, prim_mode_params.mode->hdisplay,
		  prim_mode_params.mode->vdisplay,
		  LOCAL_I915_FORMAT_MOD_X_TILED, PLANE_PRI, &blue);

	fill_fb(&blue, COLOR_PRIM_BG);

	rc = drmModeSetCrtc(drm.fd, prim_mode_params.crtc_id,
			    blue.fb_id, 0, 0, &prim_mode_params.connector_id, 1,
			    prim_mode_params.mode);
	igt_assert_eq(rc, 0);
	collect_crcs(&blue_crcs[format].crc, mandatory_sink_crc);

	print_crc("Blue CRC:  ", &blue_crcs[format].crc);

	unset_all_crtcs();

	igt_remove_fb(drm.fd, &blue);

	blue_crcs[format].initialized = true;
}

static void init_crcs(enum pixel_format format,
		      struct draw_pattern_info *pattern,
		      bool mandatory_sink_crc)
{
	int r, r_, rc;
	struct igt_fb tmp_fbs[pattern->n_rects];

	if (pattern->initialized[format])
		return;

	pattern->crcs[format] = calloc(pattern->n_rects,
				       sizeof(*(pattern->crcs[format])));

	for (r = 0; r < pattern->n_rects; r++)
		create_fb(format, prim_mode_params.mode->hdisplay,
			  prim_mode_params.mode->vdisplay,
			  LOCAL_I915_FORMAT_MOD_X_TILED, PLANE_PRI, &tmp_fbs[r]);

	for (r = 0; r < pattern->n_rects; r++)
		fill_fb(&tmp_fbs[r], COLOR_PRIM_BG);

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
		igt_assert_eq(rc, 0);
		collect_crcs(&pattern->crcs[format][r], mandatory_sink_crc);
	}

	for (r = 0; r < pattern->n_rects; r++) {
		igt_debug("Rect %d CRC:", r);
		print_crc("", &pattern->crcs[format][r]);
	}

	unset_all_crtcs();

	for (r = 0; r < pattern->n_rects; r++)
		igt_remove_fb(drm.fd, &tmp_fbs[r]);

	pattern->initialized[format] = true;
}

static uint64_t get_plane_type(uint32_t plane_id)
{
	bool found;
	uint64_t prop_value;
	drmModePropertyPtr prop;

	found = kmstest_get_property(drm.fd, plane_id, DRM_MODE_OBJECT_PLANE,
				     "type", NULL, &prop_value, &prop);
	igt_assert(found);
	igt_assert(prop->flags & DRM_MODE_PROP_ENUM);
	igt_assert(prop_value < prop->count_enums);

	drmModeFreeProperty(prop);
	return prop_value;
}

static void setup_drm(void)
{
	int i, rc;

	drm.fd = drm_open_driver_master(DRIVER_INTEL);

	drm.res = drmModeGetResources(drm.fd);
	igt_assert(drm.res->count_connectors <= MAX_CONNECTORS);

	for (i = 0; i < drm.res->count_connectors; i++)
		drm.connectors[i] = drmModeGetConnectorCurrent(drm.fd,
						drm.res->connectors[i]);

	rc = drmSetClientCap(drm.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	igt_require(rc == 0);

	drm.plane_res = drmModeGetPlaneResources(drm.fd);
	igt_assert(drm.plane_res->count_planes <= MAX_PLANES);

	for (i = 0; i < drm.plane_res->count_planes; i++) {
		drm.planes[i] = drmModeGetPlane(drm.fd, drm.plane_res->planes[i]);
		drm.plane_types[i] = get_plane_type(drm.plane_res->planes[i]);
	}

	drm.bufmgr = drm_intel_bufmgr_gem_init(drm.fd, 4096);
	igt_assert(drm.bufmgr);
	drm_intel_bufmgr_gem_enable_reuse(drm.bufmgr);
}

static void teardown_drm(void)
{
	int i;

	drm_intel_bufmgr_destroy(drm.bufmgr);

	for (i = 0; i < drm.plane_res->count_planes; i++)
		drmModeFreePlane(drm.planes[i]);
	drmModeFreePlaneResources(drm.plane_res);

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
	create_fbs(FORMAT_DEFAULT);
	kmstest_set_vt_graphics_mode();
}

static void teardown_modeset(void)
{
	destroy_fbs(FORMAT_DEFAULT);
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
	prim_mode_params.fb.fb = &fbs[FORMAT_DEFAULT].prim_pri;
	prim_mode_params.fb.x = prim_mode_params.fb.y = 0;
	fill_fb_region(&prim_mode_params.fb, COLOR_PRIM_BG);
	set_mode_for_params(&prim_mode_params);

	sink_crc.fd = igt_debugfs_open("i915_sink_crc_eDP1", O_RDONLY);
	igt_assert_lte(0, sink_crc.fd);

	rc = read(sink_crc.fd, crc.data, SINK_CRC_SIZE);
	errno_ = errno;
	if (rc == -1 && errno_ == ENOTTY)
		igt_info("Sink CRC not supported: panel doesn't support it\n");
	if (rc == -1 && errno_ == ETIMEDOUT)
		igt_info("Sink CRC not reliable on this panel: skipping it\n");
	else if (rc == SINK_CRC_SIZE)
		sink_crc.supported = true;
	else
		igt_info("Unexpected sink CRC error, rc=:%zd errno:%d %s\n",
			 rc, errno_, strerror(errno_));
}

static void setup_crcs(void)
{
	enum pixel_format f;

	pipe_crc = igt_pipe_crc_new(0, INTEL_PIPE_CRC_SOURCE_AUTO);

	setup_sink_crc();

	for (f = 0; f < FORMAT_COUNT; f++)
		blue_crcs[f].initialized = false;

	pattern1.frames_stack = true;
	pattern1.n_rects = 4;
	pattern1.get_rect = pat1_get_rect;
	for (f = 0; f < FORMAT_COUNT; f++) {
		pattern1.initialized[f] = false;
		pattern1.crcs[f] = NULL;
	}

	pattern2.frames_stack = true;
	pattern2.n_rects = 4;
	pattern2.get_rect = pat2_get_rect;
	for (f = 0; f < FORMAT_COUNT; f++) {
		pattern2.initialized[f] = false;
		pattern2.crcs[f] = NULL;
	}

	pattern3.frames_stack = false;
	pattern3.n_rects = 5;
	pattern3.get_rect = pat3_get_rect;
	for (f = 0; f < FORMAT_COUNT; f++) {
		pattern3.initialized[f] = false;
		pattern3.crcs[f] = NULL;
	}

	pattern4.frames_stack = false;
	pattern4.n_rects = 1;
	pattern4.get_rect = pat4_get_rect;
	for (f = 0; f < FORMAT_COUNT; f++) {
		pattern4.initialized[f] = false;
		pattern4.crcs[f] = NULL;
	}
}

static void teardown_crcs(void)
{
	enum pixel_format f;

	for (f = 0; f < FORMAT_COUNT; f++) {
		if (pattern1.crcs[f])
			free(pattern1.crcs[f]);
		if (pattern2.crcs[f])
			free(pattern2.crcs[f]);
		if (pattern3.crcs[f])
			free(pattern3.crcs[f]);
		if (pattern4.crcs[f])
			free(pattern4.crcs[f]);
	}

	if (sink_crc.fd != -1)
		close(sink_crc.fd);

	igt_pipe_crc_free(pipe_crc);
}

static bool fbc_supported_on_chipset(void)
{
	char buf[128];

	igt_debugfs_read("i915_fbc_status", buf);
	return !strstr(buf, "FBC unsupported on this chipset\n");
}

static void setup_fbc(void)
{
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
}

static bool psr_sink_has_support(void)
{
	char buf[256];

	igt_debugfs_read("i915_edp_psr_status", buf);
	return strstr(buf, "Sink_Support: yes\n");
}

static void setup_psr(void)
{
	if (get_connector(prim_mode_params.connector_id)->connector_type !=
	    DRM_MODE_CONNECTOR_eDP) {
		igt_info("Can't test PSR: no usable eDP screen.\n");
		return;
	}

	if (!psr_sink_has_support()) {
		igt_info("Can't test PSR: not supported by sink.\n");
		return;
	}
	psr.can_test = true;
}

static void teardown_psr(void)
{
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
#define DONT_ASSERT_FEATURE_STATUS	(1 << 1)

#define FBC_ASSERT_FLAGS		(0xF << 2)
#define ASSERT_FBC_ENABLED		(1 << 2)
#define ASSERT_FBC_DISABLED		(1 << 3)
#define ASSERT_LAST_ACTION_CHANGED	(1 << 4)
#define ASSERT_NO_ACTION_CHANGE		(1 << 5)

#define PSR_ASSERT_FLAGS		(3 << 6)
#define ASSERT_PSR_ENABLED		(1 << 6)
#define ASSERT_PSR_DISABLED		(1 << 7)

static int adjust_assertion_flags(const struct test_mode *t, int flags)
{
	if (!(flags & DONT_ASSERT_FEATURE_STATUS)) {
		if (!(flags & ASSERT_FBC_DISABLED))
			flags |= ASSERT_FBC_ENABLED;
		if (!(flags & ASSERT_PSR_DISABLED))
			flags |= ASSERT_PSR_ENABLED;
	}

	if ((t->feature & FEATURE_FBC) == 0)
		flags &= ~FBC_ASSERT_FLAGS;
	if ((t->feature & FEATURE_PSR) == 0)
		flags &= ~PSR_ASSERT_FLAGS;

	return flags;
}

#define do_crc_assertions(flags, mandatory_sink_crc) do {		\
	int flags__ = (flags);						\
	struct both_crcs crc_;						\
									\
	if (!opt.check_crc || (flags__ & DONT_ASSERT_CRC))		\
		break;							\
									\
	collect_crcs(&crc_, mandatory_sink_crc);			\
	print_crc("Calculated CRC:", &crc_);				\
									\
	igt_assert(wanted_crc);						\
	igt_assert_crc_equal(&crc_.pipe, &wanted_crc->pipe);		\
	if (mandatory_sink_crc)						\
		assert_sink_crc_equal(&crc_.sink, &wanted_crc->sink);	\
	else								\
		if (!sink_crc_equal(&crc_.sink, &wanted_crc->sink))	\
			igt_info("Sink CRC differ, but not required\n"); \
} while (0)

#define do_status_assertions(flags_) do {				\
	if (!opt.check_status) {					\
		/* Make sure we settle before continuing. */		\
		sleep(1);						\
		break;							\
	}								\
									\
	if (flags_ & ASSERT_FBC_ENABLED) {				\
		igt_require(!fbc_not_enough_stolen());			\
		if (!fbc_wait_until_enabled()) {			\
			fbc_print_status();				\
			igt_assert_f(false, "FBC disabled\n");		\
		}							\
									\
		if (fbc.supports_compressing && 			\
		    opt.fbc_check_compression)				\
			igt_assert(fbc_wait_for_compression());		\
	} else if (flags_ & ASSERT_FBC_DISABLED) {			\
		igt_assert(!fbc_wait_until_enabled());			\
	}								\
									\
	if (flags_ & ASSERT_PSR_ENABLED) {				\
		if (!psr_wait_until_enabled()) {			\
			psr_print_status();				\
			igt_assert_f(false, "PSR disabled\n");		\
		}							\
	} else if (flags_ & ASSERT_PSR_DISABLED) {			\
		igt_assert(!psr_wait_until_enabled());			\
	}								\
} while (0)

#define do_assertions(flags) do {					\
	int flags_ = adjust_assertion_flags(t, (flags));		\
	bool mandatory_sink_crc = t->feature & FEATURE_PSR;		\
									\
	wait_user(2, "Paused before assertions.");			\
									\
	/* Check the CRC to make sure the drawing operations work	\
	 * immediately, independently of the features being enabled. */	\
	do_crc_assertions(flags_, mandatory_sink_crc);			\
									\
	/* Now we can flush things to make the test faster. */		\
	do_flush(t);							\
									\
	do_status_assertions(flags_);					\
									\
	/* Check CRC again to make sure the compressed screen is ok,	\
	 * except if we're not drawing on the primary screen. On this	\
	 * case, the first check should be enough and a new CRC check	\
	 * would only delay the test suite while adding no value to the	\
	 * test suite. */						\
	if (t->screen == SCREEN_PRIM)					\
		do_crc_assertions(flags_, mandatory_sink_crc);		\
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
	fill_fb_region(&prim_mode_params.fb, COLOR_PRIM_BG);
	set_mode_for_params(&prim_mode_params);

	wanted_crc = &blue_crcs[t->format].crc;
	fbc_update_last_action();

	do_assertions(ASSERT_NO_ACTION_CHANGE);
}

static void enable_scnd_screen_and_wait(const struct test_mode *t)
{
	fill_fb_region(&scnd_mode_params.fb, COLOR_SCND_BG);
	set_mode_for_params(&scnd_mode_params);

	do_assertions(ASSERT_NO_ACTION_CHANGE);
}

static void set_cursor_for_test(const struct test_mode *t,
				struct modeset_params *params)
{
	int rc;

	fill_fb_region(&params->cursor, COLOR_PRIM_BG);

	rc = drmModeMoveCursor(drm.fd, params->crtc_id, 0, 0);
	igt_assert_eq(rc, 0);

	rc = drmModeSetCursor(drm.fd, params->crtc_id,
			      params->cursor.fb->gem_handle,
			      params->cursor.w,
			      params->cursor.h);
	igt_assert_eq(rc, 0);

	do_assertions(ASSERT_NO_ACTION_CHANGE);
}

static void set_sprite_for_test(const struct test_mode *t,
				struct modeset_params *params)
{
	int rc;

	fill_fb_region(&params->sprite, COLOR_PRIM_BG);

	rc = drmModeSetPlane(drm.fd, params->sprite_id, params->crtc_id,
			     params->sprite.fb->fb_id, 0, 0, 0,
			     params->sprite.w, params->sprite.h,
			     0, 0, params->sprite.w << 16,
			     params->sprite.h << 16);
	igt_assert_eq(rc, 0);

	do_assertions(ASSERT_NO_ACTION_CHANGE);
}

static void enable_features_for_test(const struct test_mode *t)
{
	if (t->feature == FEATURE_DEFAULT)
		return;

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

	if (opt.only_pipes != PIPE_COUNT)
		igt_require(t->pipes == opt.only_pipes);
}

static void set_crtc_fbs(const struct test_mode *t)
{
	struct screen_fbs *s = &fbs[t->format];

	create_fbs(t->format);

	switch (t->fbs) {
	case FBS_INDIVIDUAL:
		prim_mode_params.fb.fb = &s->prim_pri;
		scnd_mode_params.fb.fb = &s->scnd_pri;
		offscreen_fb.fb = &s->offscreen;

		prim_mode_params.fb.x = 0;
		scnd_mode_params.fb.x = 0;
		offscreen_fb.x = 0;

		prim_mode_params.fb.y = 0;
		scnd_mode_params.fb.y = 0;
		offscreen_fb.y = 0;
		break;
	case FBS_SHARED:
		/* Please see the comment at the top of create_shared_fb(). */
		prim_mode_params.fb.fb = &s->big;
		scnd_mode_params.fb.fb = &s->big;
		offscreen_fb.fb = &s->big;

		prim_mode_params.fb.x = opt.shared_fb_x_offset;
		scnd_mode_params.fb.x = opt.shared_fb_x_offset;
		offscreen_fb.x = opt.shared_fb_x_offset;

		prim_mode_params.fb.y = opt.shared_fb_y_offset;
		scnd_mode_params.fb.y = prim_mode_params.fb.y +
					prim_mode_params.fb.h;
		offscreen_fb.y = scnd_mode_params.fb.y + scnd_mode_params.fb.h;
		break;
	default:
		igt_assert(false);
	}

	prim_mode_params.cursor.fb = &s->prim_cur;
	prim_mode_params.sprite.fb = &s->prim_spr;
	scnd_mode_params.cursor.fb = &s->scnd_cur;
	scnd_mode_params.sprite.fb = &s->scnd_spr;
}

static void prepare_subtest_data(const struct test_mode *t,
				 struct draw_pattern_info *pattern)
{
	check_test_requirements(t);

	stop_busy_thread();

	disable_features(t);
	set_crtc_fbs(t);

	if (t->screen == SCREEN_OFFSCREEN)
		fill_fb_region(&offscreen_fb, COLOR_OFFSCREEN_BG);

	unset_all_crtcs();

	init_blue_crc(t->format, t->feature & FEATURE_PSR);
	if (pattern)
		init_crcs(t->format, pattern, t->feature & FEATURE_PSR);

	enable_features_for_test(t);
}

static void prepare_subtest_screens(const struct test_mode *t)
{
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

static void prepare_subtest(const struct test_mode *t,
			    struct draw_pattern_info *pattern)
{
	prepare_subtest_data(t, pattern);
	prepare_subtest_screens(t);
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
	prepare_subtest_data(t, NULL);

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
	if (method != IGT_DRAW_MMAP_GTT)
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
		update_wanted_crc(t, &pattern->crcs[t->format][r]);
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
 *   possible pairs of drawing methods.
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
	enum igt_draw_method m1, m2, used_method;

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

	for (m1 = 0; m1 < IGT_DRAW_METHOD_COUNT; m1++) {
		for (m2 = m1 + 1; m2 < IGT_DRAW_METHOD_COUNT; m2++) {

			igt_debug("Methods %s and %s\n",
				  igt_draw_get_method_name(m1),
				  igt_draw_get_method_name(m2));
			for (r = 0; r < pattern->n_rects; r++) {
				used_method = (r % 2 == 0) ? m1 : m2;

				igt_debug("Used method %s\n",
					igt_draw_get_method_name(used_method));

				draw_rect(pattern, target, used_method, r);
				update_wanted_crc(t,
						  &pattern->crcs[t->format][r]);

				assertions = used_method != IGT_DRAW_MMAP_GTT ?
					     ASSERT_LAST_ACTION_CHANGED :
					     ASSERT_NO_ACTION_CHANGE;
				if (op_disables_psr(t, used_method))
					assertions |= ASSERT_PSR_DISABLED;

				do_assertions(assertions);
			}

			fill_fb_region(target, COLOR_PRIM_BG);

			update_wanted_crc(t, &blue_crcs[t->format].crc);
			do_assertions(ASSERT_NO_ACTION_CHANGE);
		}
	}
}

static bool format_is_valid(int feature_flags,
			    enum pixel_format format)
{
	int devid = intel_get_drm_devid(drm.fd);

	if (!(feature_flags & FEATURE_FBC))
		return true;

	switch (format) {
	case FORMAT_RGB888:
		return true;
	case FORMAT_RGB565:
		if (IS_GEN2(devid) || IS_G4X(devid))
			return false;
		return true;
	case FORMAT_RGB101010:
		return false;
	default:
		igt_assert(false);
	}
}

/*
 * badformat - test pixel formats that are not supported by at least one feature
 *
 * METHOD
 *   We just do a modeset on a buffer with the given pixel format and check the
 *   status of the relevant features.
 *
 * EXPECTED RESULTS
 *   No assertion failures :)
 *
 * FAILURES
 *   If you get a feature enabled/disabled assertion failure, then you should
 *   probably check the Kernel code for the feature that checks the pixel
 *   formats. If you get a CRC assertion failure, then you should use the
 *   appropriate command line arguments that will allow you to look at the
 *   screen, then judge what to do based on what you see.
 */
static void badformat_subtest(const struct test_mode *t)
{
	bool fbc_valid = format_is_valid(FEATURE_FBC, t->format);
	bool psr_valid = format_is_valid(FEATURE_PSR, t->format);
	int assertions = ASSERT_NO_ACTION_CHANGE;

	prepare_subtest_data(t, NULL);

	fill_fb_region(&prim_mode_params.fb, COLOR_PRIM_BG);
	set_mode_for_params(&prim_mode_params);

	wanted_crc = &blue_crcs[t->format].crc;

	if (!fbc_valid)
		assertions |= ASSERT_FBC_DISABLED;
	if (!psr_valid)
		assertions |= ASSERT_PSR_DISABLED;
	do_assertions(assertions);
}

/*
 * format_draw - test pixel formats that are not FORMAT_DEFAULT
 *
 * METHOD
 *   The real subtest to be executed depends on whether the pixel format is
 *   supported by the features being tested or not. Check the documentation of
 *   each subtest.
 *
 * EXPECTED RESULTS
 *   See the documentation for each subtest.
 *
 * FAILURES
 *   See the documentation for each subtest.
 */
static void format_draw_subtest(const struct test_mode *t)
{
	if (format_is_valid(t->feature, t->format))
		draw_subtest(t);
	else
		badformat_subtest(t);
}

/*
 * slow_draw - sleep a little bit between drawing operations
 *
 * METHOD
 *   This test is basically the same as the draw subtest, except that we sleep a
 *   little bit after each drawing operation. The goal is to detect problems
 *   that can happen in case a drawing operation is done while the machine is in
 *   some deep sleep states.
 *
 * EXPECTED RESULTS
 *   The pattern appears on the screen as expected.
 *
 * FAILURES
 *   I've seen this happen in a SKL machine and still haven't investigated it.
 *   My guess would be that preventing deep sleep states fixes the problem.
 */
static void slow_draw_subtest(const struct test_mode *t)
{
	int r;
	struct draw_pattern_info *pattern = &pattern1;
	struct modeset_params *params = pick_params(t);
	struct fb_region *target;

	prepare_subtest(t, pattern);
	sleep(2);
	target = pick_target(t, params);

	for (r = 0; r < pattern->n_rects; r++) {
		sleep(2);
		draw_rect(pattern, target, t->method, r);
		sleep(2);

		update_wanted_crc(t, &pattern->crcs[t->format][r]);
		do_assertions(0);
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
		igt_assert_eq(rc, 0);
		break;
	default:
		igt_assert_f(false, "Unexpected poll rc %d\n", rc);
		break;
	}
}

static void set_prim_plane_for_params(struct modeset_params *params)
{
	int rc, i, crtc_index = -1;
	uint32_t plane_id = 0;

	for (i = 0; i < drm.res->count_crtcs; i++)
		if (drm.res->crtcs[i] == params->crtc_id)
			crtc_index = i;
	igt_assert(crtc_index >= 0);

	for (i = 0; i < drm.plane_res->count_planes; i++)
		if ((drm.planes[i]->possible_crtcs & (1 << crtc_index)) &&
		    drm.plane_types[i] == DRM_PLANE_TYPE_PRIMARY)
			plane_id = drm.planes[i]->plane_id;
	igt_assert(plane_id);

	rc = drmModeSetPlane(drm.fd, plane_id, params->crtc_id,
			     params->fb.fb->fb_id, 0, 0, 0,
			     params->mode->hdisplay,
			     params->mode->vdisplay,
			     params->fb.x << 16, params->fb.y << 16,
			     params->fb.w << 16, params->fb.h << 16);
	igt_assert(rc == 0);
}

static void page_flip_for_params(struct modeset_params *params,
				 enum flip_type type)
{
	int rc;

	switch (type) {
	case FLIP_PAGEFLIP:
		rc = drmModePageFlip(drm.fd, params->crtc_id,
				     params->fb.fb->fb_id, 0, NULL);
		igt_assert_eq(rc, 0);
		break;
	case FLIP_PAGEFLIP_EVENT:
		rc = drmModePageFlip(drm.fd, params->crtc_id,
				     params->fb.fb->fb_id,
				     DRM_MODE_PAGE_FLIP_EVENT, NULL);
		igt_assert_eq(rc, 0);
		wait_flip_event();
		break;
	case FLIP_MODESET:
		set_mode_for_params(params);
		break;
	case FLIP_PLANES:
		set_prim_plane_for_params(params);
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
static void flip_subtest(const struct test_mode *t)
{
	int r;
	int assertions = 0;
	struct igt_fb fb2, *orig_fb;
	struct modeset_params *params = pick_params(t);
	struct draw_pattern_info *pattern = &pattern1;
	enum color bg_color;

	switch (t->screen) {
	case SCREEN_PRIM:
		assertions |= ASSERT_LAST_ACTION_CHANGED;
		bg_color = COLOR_PRIM_BG;
		break;
	case SCREEN_SCND:
		assertions |= ASSERT_NO_ACTION_CHANGE;
		bg_color = COLOR_SCND_BG;
		break;
	default:
		igt_assert(false);
	}

	prepare_subtest(t, pattern);

	create_fb(t->format, params->fb.fb->width, params->fb.fb->height,
		  LOCAL_I915_FORMAT_MOD_X_TILED, t->plane, &fb2);
	fill_fb(&fb2, bg_color);
	orig_fb = params->fb.fb;

	for (r = 0; r < pattern->n_rects; r++) {
		params->fb.fb = (r % 2 == 0) ? &fb2 : orig_fb;

		if (r != 0)
			draw_rect(pattern, &params->fb, t->method, r - 1);
		draw_rect(pattern, &params->fb, t->method, r);
		update_wanted_crc(t, &pattern->crcs[t->format][r]);

		page_flip_for_params(params, t->flip);

		do_assertions(assertions);
	}

	igt_remove_fb(drm.fd, &fb2);
}

/*
 * fliptrack - check if the hardware tracking works after page flips
 *
 * METHOD
 *   Flip to a new buffer, then draw on it using MMAP_GTT and check the CRC to
 *   make sure the hardware tracking detected the write.
 *
 * EXPECTED RESULTS
 *   Everything works as expected, screen contents are properly updated.
 *
 * FAILURES
 *   First you need to check if the draw and flip subtests pass. Only after both
 *   are passing this test can be useful. If we're failing only on this subtest,
 *   then maybe we are not properly updating the hardware tracking registers
 *   during the flip operations.
 */
static void fliptrack_subtest(const struct test_mode *t, enum flip_type type)
{
	int r;
	struct igt_fb fb2, *orig_fb;
	struct modeset_params *params = pick_params(t);
	struct draw_pattern_info *pattern = &pattern1;

	prepare_subtest(t, pattern);

	create_fb(t->format, params->fb.fb->width, params->fb.fb->height,
		  LOCAL_I915_FORMAT_MOD_X_TILED, t->plane, &fb2);
	fill_fb(&fb2, COLOR_PRIM_BG);
	orig_fb = params->fb.fb;

	for (r = 0; r < pattern->n_rects; r++) {
		params->fb.fb = (r % 2 == 0) ? &fb2 : orig_fb;

		if (r != 0)
			draw_rect(pattern, &params->fb, t->method, r - 1);

		page_flip_for_params(params, type);
		do_assertions(0);

		draw_rect(pattern, &params->fb, t->method, r);
		update_wanted_crc(t, &pattern->crcs[t->format][r]);

		do_assertions(ASSERT_PSR_DISABLED);
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
	update_wanted_crc(t, &pattern->crcs[t->format][0]);

	do_assertions(assertions);

	for (r = 1; r < pattern->n_rects; r++) {
		struct rect rect = pattern->get_rect(&params->fb, r);

		switch (t->plane) {
		case PLANE_CUR:
			rc = drmModeMoveCursor(drm.fd, params->crtc_id, rect.x,
					       rect.y);
			igt_assert_eq(rc, 0);
			break;
		case PLANE_SPR:
			rc = drmModeSetPlane(drm.fd, params->sprite_id,
					     params->crtc_id,
					     params->sprite.fb->fb_id, 0,
					     rect.x, rect.y, rect.w,
					     rect.h, 0, 0, rect.w << 16,
					     rect.h << 16);
			igt_assert_eq(rc, 0);
			break;
		default:
			igt_assert(false);
		}
		update_wanted_crc(t, &pattern->crcs[t->format][r]);

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
	update_wanted_crc(t, &pattern->crcs[t->format][0]);
	do_assertions(assertions);

	for (r = 0; r < 4; r++) {
		if (r % 2 == 0) {
			switch (t->plane) {
			case PLANE_CUR:
				rc = drmModeSetCursor(drm.fd, params->crtc_id,
						      0, 0, 0);
				igt_assert_eq(rc, 0);
				break;
			case PLANE_SPR:
				rc = drmModeSetPlane(drm.fd, params->sprite_id,
						     0, 0, 0, 0, 0, 0, 0, 0, 0,
						     0, 0);
				igt_assert_eq(rc, 0);
				break;
			default:
				igt_assert(false);
			}
			update_wanted_crc(t, &blue_crcs[t->format].crc);

		} else {
			switch (t->plane) {
			case PLANE_CUR:
				rc = drmModeSetCursor(drm.fd, params->crtc_id,
						  params->cursor.fb->gem_handle,
						  params->cursor.w,
						  params->cursor.h);
				igt_assert_eq(rc, 0);
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
				igt_assert_eq(rc, 0);
				break;
			default:
				igt_assert(false);
			}
			update_wanted_crc(t, &pattern->crcs[t->format][0]);

		}

		do_assertions(assertions);
	}
}

static bool prim_plane_disabled(void)
{
	int i, rc;
	bool disabled, found = false;

	for (i = 0; i < drm.plane_res->count_planes; i++) {
		/* We just pick the first CRTC for the primary plane. */
		if ((drm.planes[i]->possible_crtcs & 0x1) &&
		    drm.plane_types[i] == DRM_PLANE_TYPE_PRIMARY) {
			found = true;
			disabled = (drm.planes[i]->crtc_id == 0);
		}
	}

	igt_assert(found);

	rc = drmSetClientCap(drm.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 0);
	igt_assert_eq(rc, 0);

	return disabled;
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
	create_fb(t->format, rect.w, rect.h, LOCAL_I915_FORMAT_MOD_X_TILED,
		  t->plane, &fullscreen_fb);
	/* Call pick_color() again since PRI and SPR may not support the same
	 * pixel formats. */
	rect.color = pick_color(&fullscreen_fb, COLOR_GREEN);
	igt_draw_fill_fb(drm.fd, &fullscreen_fb, rect.color);

	rc = drmModeSetPlane(drm.fd, params->sprite_id, params->crtc_id,
			     fullscreen_fb.fb_id, 0, 0, 0, fullscreen_fb.width,
			     fullscreen_fb.height, 0, 0,
			     fullscreen_fb.width << 16,
			     fullscreen_fb.height << 16);
	igt_assert_eq(rc, 0);
	update_wanted_crc(t, &pattern->crcs[t->format][0]);

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
	igt_assert_eq(rc, 0);

	if (t->screen == SCREEN_PRIM)
		assertions = ASSERT_LAST_ACTION_CHANGED;
	update_wanted_crc(t, &blue_crcs[t->format].crc);
	do_assertions(assertions);

	igt_remove_fb(drm.fd, &fullscreen_fb);
}

/*
 * scaledprimary - try different primary plane scaling strategies
 *
 * METHOD
 *    Enable the primary plane, use drmModeSetPlane to force scaling in
 *    different ways.
 *
 * EXPECTED RESULTS
 *   SKIP on platforms that don't support primary plane scaling. Success on all
 *   others.
 *
 * FAILURES
 *   TODO: although we're exercising the code here, we're not really doing
 *   assertions in order to check if things are working properly. The biggest
 *   issue this code would be able to find would be an incorrectly calculated
 *   CFB size, and today we don't have means to assert this. One day we might
 *   implement some sort of stolen memory checking mechanism, then we'll be able
 *   to force it to run after every drmModeSetPlane call here, so we'll be
 *   checking if the expected CFB size is actually what we think it is.
 */
static void scaledprimary_subtest(const struct test_mode *t)
{
	struct igt_fb new_fb, *old_fb;
	struct modeset_params *params = pick_params(t);
	int i, rc;
	uint32_t plane_id;

	igt_require_f(intel_gen(intel_get_drm_devid(drm.fd)) >= 9,
		      "Can't test primary plane scaling before gen 9\n");

	prepare_subtest(t, NULL);

	old_fb = params->fb.fb;

	create_fb(t->format, params->fb.fb->width, params->fb.fb->height,
		  LOCAL_I915_FORMAT_MOD_X_TILED,
		  t->plane, &new_fb);
	fill_fb(&new_fb, COLOR_BLUE);

	igt_draw_rect_fb(drm.fd, drm.bufmgr, NULL, &new_fb, t->method,
			 params->fb.x, params->fb.y,
			 params->fb.w / 2, params->fb.h / 2,
			 pick_color(&new_fb, COLOR_GREEN));
	igt_draw_rect_fb(drm.fd, drm.bufmgr, NULL, &new_fb, t->method,
			 params->fb.x + params->fb.w / 2,
			 params->fb.y + params->fb.h / 2,
			 params->fb.w / 2, params->fb.h / 2,
			 pick_color(&new_fb, COLOR_RED));
	igt_draw_rect_fb(drm.fd, drm.bufmgr, NULL, &new_fb, t->method,
			 params->fb.x + params->fb.w / 2,
			 params->fb.y + params->fb.h / 2,
			 params->fb.w / 4, params->fb.h / 4,
			 pick_color(&new_fb, COLOR_MAGENTA));

	for (i = 0; i < drm.plane_res->count_planes; i++)
		if ((drm.planes[i]->possible_crtcs & 1) &&
		    drm.plane_types[i] == DRM_PLANE_TYPE_PRIMARY)
			plane_id = drm.planes[i]->plane_id;

	/* No scaling. */
	rc = drmModeSetPlane(drm.fd, plane_id, params->crtc_id,
			     new_fb.fb_id, 0,
			     0, 0,
			     params->mode->hdisplay, params->mode->vdisplay,
			     params->fb.x << 16, params->fb.y << 16,
			     params->fb.w << 16, params->fb.h << 16);
	igt_assert(rc == 0);
	do_assertions(DONT_ASSERT_CRC);

	/* Source upscaling. */
	rc = drmModeSetPlane(drm.fd, plane_id, params->crtc_id,
			     new_fb.fb_id, 0,
			     0, 0,
			     params->mode->hdisplay, params->mode->vdisplay,
			     params->fb.x << 16, params->fb.y << 16,
			     (params->fb.w / 2) << 16,
			     (params->fb.h / 2) << 16);
	igt_assert(rc == 0);
	do_assertions(DONT_ASSERT_CRC);

	/* Destination doesn't fill the entire CRTC, no scaling. */
	rc = drmModeSetPlane(drm.fd, plane_id, params->crtc_id,
			     new_fb.fb_id, 0,
			     params->mode->hdisplay / 4,
			     params->mode->vdisplay / 4,
			     params->mode->hdisplay / 2,
			     params->mode->vdisplay / 2,
			     params->fb.x << 16, params->fb.y << 16,
			     (params->fb.w / 2) << 16,
			     (params->fb.h / 2) << 16);
	igt_assert(rc == 0);
	do_assertions(DONT_ASSERT_CRC);

	/* Destination doesn't fill the entire CRTC, upscaling. */
	rc = drmModeSetPlane(drm.fd, plane_id, params->crtc_id,
			     new_fb.fb_id, 0,
			     params->mode->hdisplay / 4,
			     params->mode->vdisplay / 4,
			     params->mode->hdisplay / 2,
			     params->mode->vdisplay / 2,
			     (params->fb.x + params->fb.w / 2) << 16,
			     (params->fb.y + params->fb.h / 2) << 16,
			     (params->fb.w / 4) << 16,
			     (params->fb.h / 4) << 16);
	igt_assert(rc == 0);
	do_assertions(DONT_ASSERT_CRC);

	/* Back to the good and old blue fb. */
	rc = drmModeSetPlane(drm.fd, plane_id, params->crtc_id,
			     old_fb->fb_id, 0,
			     0, 0,
			     params->mode->hdisplay, params->mode->vdisplay,
			     params->fb.x << 16, params->fb.y << 16,
			     params->fb.w << 16, params->fb.h << 16);
	igt_assert(rc == 0);
	do_assertions(0);

	igt_remove_fb(drm.fd, &new_fb);
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
	struct modeset_params *params = pick_params(t);
	struct igt_fb fb2;

	prepare_subtest(t, NULL);

	create_fb(t->format, params->fb.fb->width, params->fb.fb->height,
		  LOCAL_I915_FORMAT_MOD_X_TILED, t->plane, &fb2);
	fill_fb(&fb2, COLOR_PRIM_BG);

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
 * suspend - make sure suspend/resume keeps us on the same state
 *
 * METHOD
 *   Set a mode, assert FBC is there, suspend, resume, assert FBC is still
 *   there. Unset modes, assert FBC is disabled, resuspend, resume, assert FBC
 *   is still disabled.
 *
 * EXPECTED RESULTS
 *   Suspend/resume doesn't affect the FBC state.
 *
 * FAILURES
 *   A lot of different things could lead to a bug here, you'll have to check
 *   the Kernel code.
 */
static void suspend_subtest(const struct test_mode *t)
{
	struct modeset_params *params = pick_params(t);

	prepare_subtest(t, NULL);
	sleep(5);
	igt_system_suspend_autoresume();
	sleep(5);
	do_assertions(0);

	unset_all_crtcs();
	sleep(5);
	igt_system_suspend_autoresume();
	sleep(5);
	do_assertions(ASSERT_FBC_DISABLED | ASSERT_PSR_DISABLED |
		      DONT_ASSERT_CRC);

	set_mode_for_params(params);
	do_assertions(0);
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
	int max_height, assertions = 0;
	int gen = intel_gen(intel_get_drm_devid(drm.fd));

	switch (gen) {
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

	/* Gen 9 doesn't do the same dspaddr_offset magic as the older
	 * gens, so FBC may not be enabled there. */
	if (gen >= 9)
		assertions |= DONT_ASSERT_FEATURE_STATUS;

	prepare_subtest(t, pattern);
	target = pick_target(t, params);

	create_fb(t->format, params->mode->hdisplay, max_height,
		  LOCAL_I915_FORMAT_MOD_X_TILED, t->plane, &tall_fb);

	fill_fb(&tall_fb, COLOR_PRIM_BG);

	params->fb.fb = &tall_fb;
	params->fb.x = 0;
	params->fb.y = max_height - params->mode->vdisplay;
	set_mode_for_params(params);
	do_assertions(assertions);

	for (r = 0; r < pattern->n_rects; r++) {
		draw_rect(pattern, target, t->method, r);
		update_wanted_crc(t, &pattern->crcs[t->format][r]);

		/* GTT draws disable PSR. */
		do_assertions(assertions | ASSERT_PSR_DISABLED);
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
	igt_assert_eq(rc, -EINVAL);

	/* Bigger than 512, but not 64-byte aligned. */
	rc = __gem_set_tiling(drm.fd, gem_handle, I915_TILING_X, 1022);
	igt_assert_eq(rc, -EINVAL);

	/* Just make sure something actually works. */
	rc = __gem_set_tiling(drm.fd, gem_handle, I915_TILING_X, 1024);
	igt_assert_eq(rc, 0);

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
	struct igt_fb wide_fb, *old_fb;
	struct modeset_params *params = pick_params(t);
	int rc;

	try_invalid_strides();

	prepare_subtest(t, NULL);

	old_fb = params->fb.fb;

	create_fb(t->format, params->fb.fb->width + 4096, params->fb.fb->height,
		  LOCAL_I915_FORMAT_MOD_X_TILED, t->plane, &wide_fb);
	igt_assert(wide_fb.stride > 16384);

	fill_fb(&wide_fb, COLOR_PRIM_BG);

	/* Try a simple modeset with the new fb. */
	params->fb.fb = &wide_fb;
	set_mode_for_params(params);
	do_assertions(ASSERT_FBC_DISABLED);

	/* Go back to the old fb so FBC works again. */
	params->fb.fb = old_fb;
	set_mode_for_params(params);
	do_assertions(0);

	/* We're doing the equivalent of a modeset, but with the planes API. */
	params->fb.fb = &wide_fb;
	set_prim_plane_for_params(params);
	do_assertions(ASSERT_FBC_DISABLED);

	params->fb.fb = old_fb;
	set_mode_for_params(params);
	do_assertions(0);

	/* We can't use the page flip IOCTL to flip to a buffer with a different
	 * stride. */
	rc = drmModePageFlip(drm.fd, params->crtc_id, wide_fb.fb_id, 0, NULL);
	igt_assert(rc == -EINVAL);
	do_assertions(0);

	igt_remove_fb(drm.fd, &wide_fb);
}

/**
 * stridechange - change the frontbuffer stride by doing a modeset
 *
 * METHOD
 *   This test sets a mode on a CRTC, then creates a buffer with a different
 *   stride - still compatible with FBC -, and sets the mode on it. The Kernel
 *   currently shortcuts the modeset path for this case, so it won't trigger
 *   calls to xx_crtc_enable or xx_crtc_disable, and that could lead to
 *   problems, so test the case.
 *
 * EXPECTED RESULTS
 *   With the current Kernel, FBC may or may not remain enabled on this case,
 *   but we can still check the CRC values.
 *
 * FAILURES
 *   A bad Kernel may just not resize the CFB while keeping FBC enabled, and
 *   this can lead to underruns or stolen memory corruption. Underruns usually
 *   lead to CRC check errors, and stolen memory corruption can't be easily
 *   checked currently. A bad Kernel may also just throw some WARNs on dmesg.
 */
static void stridechange_subtest(const struct test_mode *t)
{
	struct igt_fb new_fb, *old_fb;
	struct modeset_params *params = pick_params(t);
	int rc;

	prepare_subtest(t, NULL);

	old_fb = params->fb.fb;

	create_fb(t->format, params->fb.fb->width + 512, params->fb.fb->height,
		  LOCAL_I915_FORMAT_MOD_X_TILED, t->plane, &new_fb);
	fill_fb(&new_fb, COLOR_PRIM_BG);

	igt_assert(old_fb->stride != new_fb.stride);

	/* We can't assert that FBC will be enabled since there may not be
	 * enough space for the CFB, but we can check the CRC. */
	params->fb.fb = &new_fb;
	set_mode_for_params(params);
	do_assertions(DONT_ASSERT_FEATURE_STATUS);

	/* Go back to the fb that can have FBC. */
	params->fb.fb = old_fb;
	set_mode_for_params(params);
	do_assertions(0);

	/* This operation is the same as above, but with the planes API. */
	params->fb.fb = &new_fb;
	set_prim_plane_for_params(params);
	do_assertions(DONT_ASSERT_FEATURE_STATUS);

	params->fb.fb = old_fb;
	set_prim_plane_for_params(params);
	do_assertions(0);

	/* We just can't page flip with a new stride. */
	rc = drmModePageFlip(drm.fd, params->crtc_id, new_fb.fb_id, 0, NULL);
	igt_assert(rc == -EINVAL);
	do_assertions(0);

	igt_remove_fb(drm.fd, &new_fb);
}

/**
 * tilingchange - alternate between tiled and untiled in multiple ways
 *
 * METHOD
 *   This test alternates between tiled and untiled frontbuffers of the same
 *   size and format through multiple different APIs: the page flip IOCTL,
 *   normal modesets and the plane APIs.
 *
 * EXPECTED RESULTS
 *   FBC gets properly disabled for the untiled FB and reenabled for the
 *   tiled FB.
 *
 * FAILURES
 *   Bad Kernels may somehow leave FBC enabled, which can cause FIFO underruns
 *   that lead to CRC assertion failures.
 */
static void tilingchange_subtest(const struct test_mode *t)
{
	struct igt_fb new_fb, *old_fb;
	struct modeset_params *params = pick_params(t);
	enum flip_type flip_type;

	prepare_subtest(t, NULL);

	old_fb = params->fb.fb;

	create_fb(t->format, params->fb.fb->width, params->fb.fb->height,
		  LOCAL_DRM_FORMAT_MOD_NONE, t->plane, &new_fb);
	fill_fb(&new_fb, COLOR_PRIM_BG);

	for (flip_type = 0; flip_type < FLIP_COUNT; flip_type++) {
		igt_debug("Flip type: %d\n", flip_type);

		/* Set a buffer with no tiling. */
		params->fb.fb = &new_fb;
		page_flip_for_params(params, flip_type);
		do_assertions(ASSERT_FBC_DISABLED);

		/* Put FBC back in a working state. */
		params->fb.fb = old_fb;
		page_flip_for_params(params, flip_type);
		do_assertions(0);
	}
}

/*
 * basic - do some basic operations regardless of which features are enabled
 *
 * METHOD
 *   This subtest does page flips and draw operations and checks the CRCs of the
 *   results. The big difference between this and the others is that here we
 *   don't enable/disable any features such as FBC or PSR: we go with whatever
 *   the Kernel has enabled by default for us. This subtest only does things
 *   that are exercised by the other subtests and in a less exhaustive way: it's
 *   completely redundant. On the other hand, it is very quick and was created
 *   with the CI system in mind: it's a quick way to detect regressions, so if
 *   it fails, then we can run the other subtests to find out why.
 *
 * EXPECTED RESULTS
 *   Passed CRC assertions.
 *
 * FAILURES
 *   If you get a failure here, you should run the more specific draw and flip
 *   subtests of each feature in order to discover what exactly is failing and
 *   why.
 *
 * TODO: do sink CRC assertions in case sink_crc.supported. Only do this after
 * our sink CRC code gets 100% reliable, in order to avoid CI false negatives.
 */
static void basic_subtest(const struct test_mode *t)
{
	struct draw_pattern_info *pattern = &pattern1;
	struct modeset_params *params = pick_params(t);
	enum igt_draw_method method;
	struct igt_fb *fb1, fb2;
	int r;
	int assertions = DONT_ASSERT_FEATURE_STATUS;

	prepare_subtest(t, pattern);

	create_fb(t->format, params->fb.fb->width, params->fb.fb->height,
		  LOCAL_I915_FORMAT_MOD_X_TILED, t->plane, &fb2);
	fb1 = params->fb.fb;

	for (r = 0, method = 0; method < IGT_DRAW_METHOD_COUNT; method++, r++) {
		if (r == pattern->n_rects) {
			params->fb.fb = (params->fb.fb == fb1) ? &fb2 : fb1;

			fill_fb_region(&params->fb, COLOR_PRIM_BG);
			update_wanted_crc(t, &blue_crcs[t->format].crc);

			page_flip_for_params(params, t->flip);
			do_assertions(assertions);

			r = 0;
		}

		draw_rect(pattern, &params->fb, method, r);
		update_wanted_crc(t, &pattern->crcs[t->format][r]);
		do_assertions(assertions);
	}
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
	case 'x':
		errno = 0;
		opt.shared_fb_x_offset = strtol(optarg, NULL, 0);
		igt_assert(errno == 0);
		break;
	case 'y':
		errno = 0;
		opt.shared_fb_y_offset = strtol(optarg, NULL, 0);
		igt_assert(errno == 0);
		break;
	case '1':
		igt_assert_eq(opt.only_pipes, PIPE_COUNT);
		opt.only_pipes = PIPE_SINGLE;
		break;
	case '2':
		igt_assert_eq(opt.only_pipes, PIPE_COUNT);
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
"  --shared-fb-x offset        Use 'offset' as the X offset for the shared FB\n"
"  --shared-fb-y offset        Use 'offset' as the Y offset for the shared FB\n"
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

static const char *format_str(enum pixel_format format)
{
	switch (format) {
	case FORMAT_RGB888:
		return "rgb888";
	case FORMAT_RGB565:
		return "rgb565";
	case FORMAT_RGB101010:
		return "rgb101010";
	default:
		igt_assert(false);
	}
}

static const char *flip_str(enum flip_type flip)
{
	switch (flip) {
	case FLIP_PAGEFLIP:
		return "pg";
	case FLIP_PAGEFLIP_EVENT:
		return "ev";
	case FLIP_MODESET:
		return "ms";
	case FLIP_PLANES:
		return "pl";
	default:
		igt_assert(false);
	}
}

#define TEST_MODE_ITER_BEGIN(t) \
	t.format = FORMAT_DEFAULT;					   \
	t.flip = FLIP_PAGEFLIP;						   \
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
		if (!opt.show_hidden && t.feature == FEATURE_NONE)	   \
			continue;					   \
		if (!opt.show_hidden && t.fbs == FBS_SHARED &&		   \
		    (t.plane == PLANE_CUR || t.plane == PLANE_SPR))	   \
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
		{ "shared-fb-x",              1, 0, 'x'},
		{ "shared-fb-y",              1, 0, 'y'},
		{ "1p-only",                  0, 0, '1'},
		{ "2p-only",                  0, 0, '2'},
		{ 0, 0, 0, 0 }
	};

	igt_subtest_init_parse_opts(&argc, argv, "", long_options, help_str,
				    opt_handler, NULL);

	igt_fixture
		setup_environment();

	for (t.feature = 0; t.feature < FEATURE_COUNT; t.feature++) {
		if (!opt.show_hidden && t.feature == FEATURE_NONE)
			continue;
		for (t.pipes = 0; t.pipes < PIPE_COUNT; t.pipes++) {
			t.screen = SCREEN_PRIM;
			t.plane = PLANE_PRI;
			t.fbs = FBS_INDIVIDUAL;
			t.format = FORMAT_DEFAULT;
			/* Make sure nothing is using these values. */
			t.flip = -1;
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
		if (t.plane != PLANE_PRI ||
		    t.screen == SCREEN_OFFSCREEN ||
		    (!opt.show_hidden && t.method != IGT_DRAW_BLT))
			continue;

		for (t.flip = 0; t.flip < FLIP_COUNT; t.flip++)
			igt_subtest_f("%s-%s-%s-%s-%sflip-%s",
				      feature_str(t.feature),
				      pipes_str(t.pipes),
				      screen_str(t.screen),
				      fbs_str(t.fbs),
				      flip_str(t.flip),
				      igt_draw_get_method_name(t.method))
				flip_subtest(&t);
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.plane != PLANE_PRI ||
		    t.screen != SCREEN_PRIM ||
		    t.method != IGT_DRAW_MMAP_GTT ||
		    (t.feature & FEATURE_FBC) == 0)
			continue;

		igt_subtest_f("%s-%s-%s-fliptrack",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      fbs_str(t.fbs))
			fliptrack_subtest(&t, FLIP_PAGEFLIP);
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.screen == SCREEN_OFFSCREEN ||
		    t.method != IGT_DRAW_BLT ||
		    t.plane == PLANE_PRI)
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
		if (t.screen == SCREEN_OFFSCREEN ||
		    t.method != IGT_DRAW_BLT ||
		    t.plane != PLANE_SPR)
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
		if (t.screen != SCREEN_PRIM ||
		    t.method != IGT_DRAW_BLT ||
		    (!opt.show_hidden && t.plane != PLANE_PRI) ||
		    (!opt.show_hidden && t.fbs != FBS_INDIVIDUAL))
			continue;

		igt_subtest_f("%s-%s-%s-%s-multidraw",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      plane_str(t.plane),
			      fbs_str(t.fbs))
			multidraw_subtest(&t);
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.pipes != PIPE_SINGLE ||
		    t.screen != SCREEN_PRIM ||
		    t.plane != PLANE_PRI ||
		    t.fbs != FBS_INDIVIDUAL ||
		    t.method != IGT_DRAW_MMAP_GTT)
			continue;

		igt_subtest_f("%s-farfromfence", feature_str(t.feature))
			farfromfence_subtest(&t);
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.pipes != PIPE_SINGLE ||
		    t.screen != SCREEN_PRIM ||
		    t.plane != PLANE_PRI ||
		    t.fbs != FBS_INDIVIDUAL)
			continue;

		for (t.format = 0; t.format < FORMAT_COUNT; t.format++) {
			/* Skip what we already tested. */
			if (t.format == FORMAT_DEFAULT)
				continue;

			igt_subtest_f("%s-%s-draw-%s",
				      feature_str(t.feature),
				      format_str(t.format),
				      igt_draw_get_method_name(t.method))
				format_draw_subtest(&t);
		}
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.pipes != PIPE_SINGLE ||
		    t.screen != SCREEN_PRIM ||
		    t.plane != PLANE_PRI ||
		    t.method != IGT_DRAW_MMAP_CPU)
			continue;
		igt_subtest_f("%s-%s-scaledprimary",
			      feature_str(t.feature),
			      fbs_str(t.fbs))
			scaledprimary_subtest(&t);
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.pipes != PIPE_SINGLE ||
		    t.screen != SCREEN_PRIM ||
		    t.plane != PLANE_PRI ||
		    t.fbs != FBS_INDIVIDUAL ||
		    t.method != IGT_DRAW_MMAP_CPU)
			continue;

		igt_subtest_f("%s-modesetfrombusy", feature_str(t.feature))
			modesetfrombusy_subtest(&t);

		if (t.feature & FEATURE_FBC) {
			igt_subtest_f("%s-badstride", feature_str(t.feature))
				badstride_subtest(&t);

			igt_subtest_f("%s-stridechange", feature_str(t.feature))
				stridechange_subtest(&t);

			igt_subtest_f("%s-tilingchange", feature_str(t.feature))
				tilingchange_subtest(&t);
		}

		if (t.feature & FEATURE_PSR)
			igt_subtest_f("%s-slowdraw", feature_str(t.feature))
				slow_draw_subtest(&t);

		igt_subtest_f("%s-suspend", feature_str(t.feature))
			suspend_subtest(&t);
	TEST_MODE_ITER_END

	t.pipes = PIPE_SINGLE;
	t.screen = SCREEN_PRIM;
	t.plane = PLANE_PRI;
	t.fbs = FBS_INDIVIDUAL;
	t.feature = FEATURE_DEFAULT;
	t.format = FORMAT_DEFAULT;
	t.flip = FLIP_PAGEFLIP;
	igt_subtest("basic")
		basic_subtest(&t);

	igt_fixture
		teardown_environment();

	igt_exit();
}
