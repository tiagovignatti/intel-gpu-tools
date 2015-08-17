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


IGT_TEST_DESCRIPTION("Test the relationship between fbcon and the frontbuffer "
		     "tracking infrastructure.");

#define MAX_CONNECTORS 32

static bool do_wait_user = false;

struct drm_info {
	int fd;
	drmModeResPtr res;
	drmModeConnectorPtr connectors[MAX_CONNECTORS];
};

static void wait_user(const char *msg)
{
	if (!do_wait_user)
		return;

	igt_info("%s Press enter...\n", msg);
	while (getchar() != '\n')
		;
}

static void setup_drm(struct drm_info *drm)
{
	int i;

	drm->fd = drm_open_any_master();

	drm->res = drmModeGetResources(drm->fd);
	igt_assert(drm->res->count_connectors <= MAX_CONNECTORS);

	for (i = 0; i < drm->res->count_connectors; i++)
		drm->connectors[i] = drmModeGetConnector(drm->fd,
						drm->res->connectors[i]);

	kmstest_set_vt_graphics_mode();
}

static void teardown_drm(struct drm_info *drm)
{
	int i;

	kmstest_restore_vt_mode();

	for (i = 0; i < drm->res->count_connectors; i++)
		drmModeFreeConnector(drm->connectors[i]);

	drmModeFreeResources(drm->res);
	igt_assert(close(drm->fd) == 0);
}

static bool fbc_supported_on_chipset(void)
{
	char buf[128];

	igt_debugfs_read("i915_fbc_status", buf);
	return !strstr(buf, "FBC unsupported on this chipset\n");
}

static bool connector_can_fbc(drmModeConnectorPtr connector)
{
	return true;
}

static bool fbc_is_enabled(void)
{
	char buf[128];

	igt_debugfs_read("i915_fbc_status", buf);
	return strstr(buf, "FBC enabled\n");
}

static bool fbc_wait_until_enabled(void)
{
	return igt_wait(fbc_is_enabled(), 5000, 1);
}

typedef bool (*connector_possible_fn)(drmModeConnectorPtr connector);

static void set_mode_for_one_screen(struct drm_info *drm, struct igt_fb *fb,
				    connector_possible_fn connector_possible)
{
	int i, rc;
	uint32_t connector_id = 0, crtc_id;
	drmModeModeInfoPtr mode;
	uint32_t buffer_id;
	drmModeConnectorPtr c = NULL;

	for (i = 0; i < drm->res->count_connectors; i++) {
		c = drm->connectors[i];

		if (c->connection == DRM_MODE_CONNECTED && c->count_modes &&
		    connector_possible(c)) {
			connector_id = c->connector_id;
			mode = &c->modes[0];
			break;
		}
	}
	igt_require_f(connector_id, "No connector available\n");

	crtc_id = drm->res->crtcs[0];

	buffer_id = igt_create_fb(drm->fd, mode->hdisplay, mode->vdisplay,
				  DRM_FORMAT_XRGB8888,
				  LOCAL_I915_FORMAT_MOD_X_TILED, fb);
	igt_draw_fill_fb(drm->fd, fb, 0xFF);

	igt_info("Setting %dx%d mode for %s connector\n",
		 mode->hdisplay, mode->vdisplay,
		 kmstest_connector_type_str(c->connector_type));

	rc = drmModeSetCrtc(drm->fd, crtc_id, buffer_id, 0, 0, &connector_id, 1,
			    mode);
	igt_assert_eq(rc, 0);
}

static bool psr_supported_on_chipset(void)
{
	char buf[256];

	igt_debugfs_read("i915_edp_psr_status", buf);
	return strstr(buf, "Sink_Support: yes\n");
}

static bool connector_can_psr(drmModeConnectorPtr connector)
{
	return (connector->connector_type == DRM_MODE_CONNECTOR_eDP);
}

static bool psr_is_enabled(void)
{
	char buf[256];

	igt_debugfs_read("i915_edp_psr_status", buf);
	return strstr(buf, "\nActive: yes\n");
}

static bool psr_wait_until_enabled(void)
{
	return igt_wait(psr_is_enabled(), 5000, 1);
}

struct feature {
	bool (*supported_on_chipset)(void);
	bool (*wait_until_enabled)(void);
	bool (*connector_possible_fn)(drmModeConnectorPtr connector);
	const char *param_name;
} fbc = {
	.supported_on_chipset = fbc_supported_on_chipset,
	.wait_until_enabled = fbc_wait_until_enabled,
	.connector_possible_fn = connector_can_fbc,
	.param_name = "enable_fbc",
}, psr = {
	.supported_on_chipset = psr_supported_on_chipset,
	.wait_until_enabled = psr_wait_until_enabled,
	.connector_possible_fn = connector_can_psr,
	.param_name = "enable_psr",
};

static void disable_features(void)
{
	igt_set_module_param_int(fbc.param_name, 0);
	igt_set_module_param_int(psr.param_name, 0);
}

static void subtest(struct feature *feature, bool suspend)
{
	struct drm_info drm;
	struct igt_fb fb;

	igt_require(feature->supported_on_chipset());

	disable_features();
	igt_set_module_param_int(feature->param_name, 1);

	setup_drm(&drm);

	kmstest_unset_all_crtcs(drm.fd, drm.res);
	wait_user("Modes unset.");
	igt_assert(!feature->wait_until_enabled());

	set_mode_for_one_screen(&drm, &fb, feature->connector_possible_fn);
	wait_user("Screen set.");
	igt_assert(feature->wait_until_enabled());

	if (suspend) {
		igt_system_suspend_autoresume();
		sleep(5);
		igt_assert(feature->wait_until_enabled());
	}

	igt_remove_fb(drm.fd, &fb);
	teardown_drm(&drm);

	/* Wait for fbcon to restore itself. */
	sleep(3);

	wait_user("Back to fbcon.");
	igt_assert(!feature->wait_until_enabled());

	if (suspend) {
		igt_system_suspend_autoresume();
		sleep(5);
		igt_assert(!feature->wait_until_enabled());
	}
}

static void setup_environment(void)
{
	int drm_fd;

	drm_fd = drm_open_any_master();
	igt_require(drm_fd >= 0);
	igt_assert(close(drm_fd) == 0);
}

static void teardown_environment(void)
{
}

igt_main
{
	igt_fixture
		setup_environment();

	igt_subtest("fbc")
		subtest(&fbc, false);
	igt_subtest("psr")
		subtest(&psr, false);
	igt_subtest("fbc-suspend")
		subtest(&fbc, true);
	igt_subtest("psr-suspend")
		subtest(&psr, true);

	igt_fixture
		teardown_environment();
}
