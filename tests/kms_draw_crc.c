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
 */

/* This program tests whether the igt_draw library actually works. */

#include "drmtest.h"
#include "igt_aux.h"
#include "igt_draw.h"
#include "igt_debugfs.h"
#include "igt_fb.h"
#include "igt_kms.h"

#define MAX_CONNECTORS 32

struct modeset_params {
	uint32_t crtc_id;
	uint32_t connector_id;
	drmModeModeInfoPtr mode;
};

int drm_fd;
drmModeResPtr drm_res;
drmModeConnectorPtr drm_connectors[MAX_CONNECTORS];
drm_intel_bufmgr *bufmgr;
igt_pipe_crc_t *pipe_crc;

bool has_method_base_crc = false;
igt_crc_t method_base_crc;

struct modeset_params ms;

static void find_modeset_params(void)
{
	int i;
	uint32_t connector_id = 0, crtc_id;
	drmModeModeInfoPtr mode = NULL;

	for (i = 0; i < drm_res->count_connectors; i++) {
		drmModeConnectorPtr c = drm_connectors[i];

		if (c->count_modes) {
			connector_id = c->connector_id;
			mode = &c->modes[0];
			break;
		}
	}
	igt_require(connector_id);

	crtc_id = drm_res->crtcs[0];
	igt_assert(crtc_id);
	igt_assert(mode);

	ms.connector_id = connector_id;
	ms.crtc_id = crtc_id;
	ms.mode = mode;

}

static void get_method_crc(enum igt_draw_method method, uint64_t tiling,
			   igt_crc_t *crc)
{
	struct igt_fb fb;
	int rc;

	igt_create_fb(drm_fd, ms.mode->hdisplay, ms.mode->vdisplay,
		      DRM_FORMAT_XRGB8888, tiling, &fb);
	igt_draw_rect_fb(drm_fd, bufmgr, NULL, &fb, method,
			 0, 0, fb.width, fb.height, 0xFF);

	igt_draw_rect_fb(drm_fd, bufmgr, NULL, &fb, method,
			 fb.width / 4, fb.height / 4,
			 fb.width / 2, fb.height / 2, 0xFF00);
	igt_draw_rect_fb(drm_fd, bufmgr, NULL, &fb, method,
			 fb.width / 8, fb.height / 8,
			 fb.width / 4, fb.height / 4, 0xFF0000);
	igt_draw_rect_fb(drm_fd, bufmgr, NULL, &fb, method,
			 fb.width / 2, fb.height / 2,
			 fb.width / 3, fb.height / 3, 0xFF00FF);

	rc = drmModeSetCrtc(drm_fd, ms.crtc_id, fb.fb_id, 0, 0,
			    &ms.connector_id, 1, ms.mode);
	igt_assert(rc == 0);

	igt_debug_wait_for_keypress("crc");
	igt_pipe_crc_collect_crc(pipe_crc, crc);

	kmstest_unset_all_crtcs(drm_fd, drm_res);
	igt_remove_fb(drm_fd, &fb);
}

static void draw_method_subtest(enum igt_draw_method method, uint64_t tiling)
{
	igt_crc_t crc;

	kmstest_unset_all_crtcs(drm_fd, drm_res);

	find_modeset_params();

	/* Use IGT_DRAW_MMAP_GTT on an untiled buffer as the parameter for
	 * comparison. Cache the value so we don't recompute it for every single
	 * subtest. */
	if (!has_method_base_crc) {
		get_method_crc(IGT_DRAW_MMAP_GTT, LOCAL_DRM_FORMAT_MOD_NONE,
			       &method_base_crc);
		has_method_base_crc = true;
	}

	get_method_crc(method, tiling, &crc);
	igt_assert_crc_equal(&crc, &method_base_crc);
}

static void get_fill_crc(uint64_t tiling, igt_crc_t *crc)
{
	struct igt_fb fb;
	int rc;

	igt_create_fb(drm_fd, ms.mode->hdisplay, ms.mode->vdisplay,
		      DRM_FORMAT_XRGB8888, tiling, &fb);

	igt_draw_fill_fb(drm_fd, &fb, 0xFF);

	rc = drmModeSetCrtc(drm_fd, ms.crtc_id, fb.fb_id, 0, 0,
			    &ms.connector_id, 1, ms.mode);
	igt_assert(rc == 0);

	igt_debug_wait_for_keypress("crc");
	igt_pipe_crc_collect_crc(pipe_crc, crc);

	kmstest_unset_all_crtcs(drm_fd, drm_res);
	igt_remove_fb(drm_fd, &fb);
}

static void fill_fb_subtest(void)
{
	int rc;
	struct igt_fb fb;
	igt_crc_t base_crc, crc;

	kmstest_unset_all_crtcs(drm_fd, drm_res);

	find_modeset_params();

	igt_create_fb(drm_fd, ms.mode->hdisplay, ms.mode->vdisplay,
		      DRM_FORMAT_XRGB8888, LOCAL_DRM_FORMAT_MOD_NONE, &fb);

	igt_draw_rect_fb(drm_fd, bufmgr, NULL, &fb, IGT_DRAW_MMAP_GTT,
			 0, 0, fb.width, fb.height, 0xFF);

	rc = drmModeSetCrtc(drm_fd, ms.crtc_id, fb.fb_id, 0, 0,
			    &ms.connector_id, 1, ms.mode);
	igt_assert(rc == 0);

	igt_debug_wait_for_keypress("crc");
	igt_pipe_crc_collect_crc(pipe_crc, &base_crc);

	get_fill_crc(LOCAL_DRM_FORMAT_MOD_NONE, &crc);
	igt_assert_crc_equal(&crc, &base_crc);

	get_fill_crc(LOCAL_I915_FORMAT_MOD_X_TILED, &crc);
	igt_assert_crc_equal(&crc, &base_crc);

	kmstest_unset_all_crtcs(drm_fd, drm_res);
	igt_remove_fb(drm_fd, &fb);
}

static void setup_environment(void)
{
	int i;

	drm_fd = drm_open_any_master();
	igt_require(drm_fd >= 0);

	drm_res = drmModeGetResources(drm_fd);
	igt_assert(drm_res->count_connectors <= MAX_CONNECTORS);

	for (i = 0; i < drm_res->count_connectors; i++)
		drm_connectors[i] = drmModeGetConnector(drm_fd,
							drm_res->connectors[i]);

	kmstest_set_vt_graphics_mode();

	bufmgr = drm_intel_bufmgr_gem_init(drm_fd, 4096);
	igt_assert(bufmgr);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	pipe_crc = igt_pipe_crc_new(0, INTEL_PIPE_CRC_SOURCE_AUTO);
}

static void teardown_environment(void)
{
	int i;

	igt_pipe_crc_free(pipe_crc);

	drm_intel_bufmgr_destroy(bufmgr);

	for (i = 0; i < drm_res->count_connectors; i++)
		drmModeFreeConnector(drm_connectors[i]);

	drmModeFreeResources(drm_res);
	close(drm_fd);
}

igt_main
{
	enum igt_draw_method method;

	igt_fixture
		setup_environment();

	for (method = 0; method < IGT_DRAW_METHOD_COUNT; method++) {
		igt_subtest_f("draw-method-%s-untiled",
			      igt_draw_get_method_name(method))
			draw_method_subtest(method, LOCAL_DRM_FORMAT_MOD_NONE);
		igt_subtest_f("draw-method-%s-tiled",
			      igt_draw_get_method_name(method))
			draw_method_subtest(method,
					    LOCAL_I915_FORMAT_MOD_X_TILED);
	}

	igt_subtest("fill-fb")
		fill_fb_subtest();

	igt_fixture
		teardown_environment();
}
