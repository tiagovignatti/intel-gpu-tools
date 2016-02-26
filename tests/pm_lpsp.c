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
 * Author: Paulo Zanoni <paulo.r.zanoni@intel.com>
 *
 */

#include "igt.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


static bool supports_lpsp(uint32_t devid)
{
	return IS_HASWELL(devid) || IS_BROADWELL(devid);
}

static bool lpsp_is_enabled(int drm_fd)
{
	uint32_t val;

	val = INREG(HSW_PWR_WELL_CTL2);
	return !(val & HSW_PWR_WELL_STATE_ENABLED);
}

/* The LPSP mode is all about an enabled pipe, but we expect to also be in the
 * low power mode when no pipes are enabled, so do this check anyway. */
static void screens_disabled_subtest(int drm_fd, drmModeResPtr drm_res)
{
	kmstest_unset_all_crtcs(drm_fd, drm_res);
	igt_assert(lpsp_is_enabled(drm_fd));
}

static uint32_t create_fb(int drm_fd, int width, int height)
{
	struct igt_fb fb;

	return igt_create_pattern_fb(drm_fd, width, height, DRM_FORMAT_XRGB8888,
				     LOCAL_DRM_FORMAT_MOD_NONE, &fb);
}

static void edp_subtest(int drm_fd, drmModeResPtr drm_res,
			drmModeConnectorPtr *drm_connectors, uint32_t devid,
			bool use_panel_fitter)
{
	int i, rc;
	uint32_t crtc_id = 0, buffer_id = 0;
	drmModeConnectorPtr connector = NULL;
	drmModeModeInfoPtr mode = NULL;
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

	kmstest_unset_all_crtcs(drm_fd, drm_res);

	for (i = 0; i < drm_res->count_connectors; i++) {
		drmModeConnectorPtr c = drm_connectors[i];

		if (c->connector_type != DRM_MODE_CONNECTOR_eDP)
			continue;
		if (c->connection != DRM_MODE_CONNECTED)
			continue;

		if (!use_panel_fitter && c->count_modes) {
			connector = c;
			mode = &c->modes[0];
			break;
		}
		if (use_panel_fitter) {
			connector = c;

			/* This is one of the modes Xorg creates for panels, so
			 * it should work just fine. Notice that Gens that
			 * support LPSP are too new for panels with native
			 * 1024x768 resolution, so this should force the panel
			 * fitter. */
			igt_assert(c->count_modes &&
				   c->modes[0].hdisplay > 1024);
			igt_assert(c->count_modes &&
				   c->modes[0].vdisplay > 768);
			mode = &std_1024_mode;
			break;
		}
	}
	igt_require(connector);

	crtc_id = kmstest_find_crtc_for_connector(drm_fd, drm_res, connector,
						  0);
	buffer_id = create_fb(drm_fd, mode->hdisplay, mode->vdisplay);

	igt_assert(buffer_id);
	igt_assert(connector);
	igt_assert(mode);

	rc = drmModeSetCrtc(drm_fd, crtc_id, buffer_id, 0, 0,
			    &connector->connector_id, 1, mode);
	igt_assert_eq(rc, 0);

	if (use_panel_fitter) {
		if (IS_HASWELL(devid))
			igt_assert(!lpsp_is_enabled(drm_fd));
		else
			igt_assert(lpsp_is_enabled(drm_fd));
	} else {
		igt_assert(lpsp_is_enabled(drm_fd));
	}
}

static void non_edp_subtest(int drm_fd, drmModeResPtr drm_res,
			    drmModeConnectorPtr *drm_connectors)
{
	int i, rc;
	uint32_t crtc_id = 0, buffer_id = 0;
	drmModeConnectorPtr connector = NULL;
	drmModeModeInfoPtr mode = NULL;

	kmstest_unset_all_crtcs(drm_fd, drm_res);

	for (i = 0; i < drm_res->count_connectors; i++) {
		drmModeConnectorPtr c = drm_connectors[i];

		if (c->connector_type == DRM_MODE_CONNECTOR_eDP)
			continue;
		if (c->connection != DRM_MODE_CONNECTED)
			continue;

		if (c->count_modes) {
			connector = c;
			mode = &c->modes[0];
			break;
		}
	}
	igt_require(connector);

	crtc_id = kmstest_find_crtc_for_connector(drm_fd, drm_res, connector,
						  0);
	buffer_id = create_fb(drm_fd, mode->hdisplay, mode->vdisplay);

	igt_assert(buffer_id);
	igt_assert(mode);

	rc = drmModeSetCrtc(drm_fd, crtc_id, buffer_id, 0, 0,
			    &connector->connector_id, 1, mode);
	igt_assert_eq(rc, 0);

	igt_assert(!lpsp_is_enabled(drm_fd));
}

#define MAX_CONNECTORS 32

int drm_fd;
uint32_t devid;
drmModeResPtr drm_res;
drmModeConnectorPtr drm_connectors[MAX_CONNECTORS];

igt_main
{
	igt_fixture {
		int i;

		drm_fd = drm_open_driver_master(DRIVER_INTEL);
		igt_require(drm_fd >= 0);

		devid = intel_get_drm_devid(drm_fd);

		drm_res = drmModeGetResources(drm_fd);
		igt_assert(drm_res->count_connectors <= MAX_CONNECTORS);

		for (i = 0; i < drm_res->count_connectors; i++)
			drm_connectors[i] = drmModeGetConnectorCurrent(drm_fd,
							drm_res->connectors[i]);

		igt_pm_enable_audio_runtime_pm();

		igt_require(supports_lpsp(devid));

		intel_register_access_init(intel_get_pci_device(), 0);

		kmstest_set_vt_graphics_mode();
	}

	igt_subtest("screens-disabled")
		screens_disabled_subtest(drm_fd, drm_res);
	igt_subtest("edp-native")
		edp_subtest(drm_fd, drm_res, drm_connectors, devid, false);
	igt_subtest("edp-panel-fitter")
		edp_subtest(drm_fd, drm_res, drm_connectors, devid, true);
	igt_subtest("non-edp")
		non_edp_subtest(drm_fd, drm_res, drm_connectors);

	igt_fixture {
		int i;

		intel_register_access_fini();
		for (i = 0; i < drm_res->count_connectors; i++)
			drmModeFreeConnector(drm_connectors[i]);
		drmModeFreeResources(drm_res);
		close(drm_fd);
	}
}
