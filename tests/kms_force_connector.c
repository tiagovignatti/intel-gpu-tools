/*
 * Copyright © 2014 Intel Corporation
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

#include "igt.h"

IGT_TEST_DESCRIPTION("Check the debugfs force connector/edid features work"
		     " correctly.");

#define CHECK_MODE(m, h, w, r) igt_assert(m.hdisplay == h && m.vdisplay == w \
					  && m.vrefresh == r)

igt_main
{
	/* force the VGA output and test that it worked */
	int drm_fd = 0;
	drmModeRes *res;
	drmModeConnector *vga_connector = NULL, *temp;
	igt_display_t display;
	int start_n_modes;

	igt_fixture {
		drm_fd = drm_open_driver_master(DRIVER_INTEL);
		res = drmModeGetResources(drm_fd);

		/* find the vga connector */
		for (int i = 0; i < res->count_connectors; i++) {

			vga_connector = drmModeGetConnector(drm_fd, res->connectors[i]);

			if (vga_connector->connector_type == DRM_MODE_CONNECTOR_VGA)
				break;

			drmModeFreeConnector(vga_connector);

			vga_connector = NULL;
		}

		igt_require(vga_connector);
	}

	igt_subtest("force-connector-state") {
		/* force the connector on and check the reported values */
		kmstest_force_connector(drm_fd, vga_connector, FORCE_CONNECTOR_ON);
		temp = drmModeGetConnector(drm_fd, vga_connector->connector_id);
		igt_assert(temp->connection == DRM_MODE_CONNECTED);
		igt_assert(temp->count_modes > 0);
		drmModeFreeConnector(temp);

		/* attempt to use the display */
		kmstest_set_vt_graphics_mode();
		igt_display_init(&display, drm_fd);
		igt_display_commit(&display);


		/* force the connector off */
		kmstest_force_connector(drm_fd, vga_connector,
					FORCE_CONNECTOR_OFF);
		temp = drmModeGetConnector(drm_fd, vga_connector->connector_id);
		igt_assert(temp->connection == DRM_MODE_DISCONNECTED);
		igt_assert(temp->count_modes == 0);
		drmModeFreeConnector(temp);

		/* check that the previous state is restored */
		kmstest_force_connector(drm_fd, vga_connector,
					FORCE_CONNECTOR_UNSPECIFIED);
		temp = drmModeGetConnector(drm_fd, vga_connector->connector_id);
		igt_assert(temp->connection == vga_connector->connection);
		drmModeFreeConnector(temp);
	}

	igt_subtest("force-edid") {
		kmstest_force_connector(drm_fd, vga_connector,
					FORCE_CONNECTOR_ON);
		temp = drmModeGetConnector(drm_fd, vga_connector->connector_id);
		start_n_modes = temp->count_modes;
		drmModeFreeConnector(temp);

		/* test edid forcing */
		kmstest_force_edid(drm_fd, vga_connector,
				   igt_kms_get_base_edid(), EDID_LENGTH);
		temp = drmModeGetConnector(drm_fd,
					   vga_connector->connector_id);

		CHECK_MODE(temp->modes[0], 1920, 1080, 60);
		CHECK_MODE(temp->modes[1], 1280, 720, 60);
		CHECK_MODE(temp->modes[2], 1024, 768, 60);
		CHECK_MODE(temp->modes[3], 800, 600, 60);
		CHECK_MODE(temp->modes[4], 640, 480, 60);

		drmModeFreeConnector(temp);

		/* remove edid */
		kmstest_force_edid(drm_fd, vga_connector, NULL, 0);
		temp = drmModeGetConnector(drm_fd, vga_connector->connector_id);
		/* the connector should now have the same number of modes that
		 * it started with */
		igt_assert(temp->count_modes == start_n_modes);
		drmModeFreeConnector(temp);

		kmstest_force_connector(drm_fd, vga_connector,
					FORCE_CONNECTOR_UNSPECIFIED);
	}

	igt_fixture {
		drmModeFreeConnector(vga_connector);
	}
}
