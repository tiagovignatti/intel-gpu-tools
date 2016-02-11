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

#define CHECK_MODE(m, h, w, r) \
	igt_assert_eq(m.hdisplay, h); igt_assert_eq(m.vdisplay, w); \
	igt_assert_eq(m.vrefresh, r);

static void reset_connectors(void)
{
	int drm_fd = 0;
	drmModeRes *res;
	drmModeConnector *connector = NULL;

	drm_fd = drm_open_driver_master(DRIVER_INTEL);
	res = drmModeGetResources(drm_fd);

	for (int i = 0; i < res->count_connectors; i++) {

		connector = drmModeGetConnectorCurrent(drm_fd,
						       res->connectors[i]);

		kmstest_force_connector(drm_fd, connector,
					FORCE_CONNECTOR_UNSPECIFIED);

		kmstest_force_edid(drm_fd, connector, NULL, 0);

		drmModeFreeConnector(connector);
	}

	igt_set_module_param_int("load_detect_test", 0);
}

static int opt_handler(int opt, int opt_index, void *data)
{
	switch (opt) {
	case 'r':
		reset_connectors();
		exit(0);
		break;
	}

	return 0;
}

int main(int argc, char **argv)
{
	/* force the VGA output and test that it worked */
	int drm_fd = 0;
	drmModeRes *res;
	drmModeConnector *vga_connector = NULL, *temp;
	int start_n_modes, start_connection;
	struct option long_opts[] = {
		{"reset", 0, 0, 'r'},
		{0, 0, 0, 0}
	};
	const char *help_str =
	       "  --reset\t\tReset all connector force states and edid.\n";

	igt_subtest_init_parse_opts(&argc, argv, "", long_opts, help_str,
				    opt_handler, NULL);

	igt_fixture {
		drm_fd = drm_open_driver_master(DRIVER_INTEL);
		res = drmModeGetResources(drm_fd);

		/* find the vga connector */
		for (int i = 0; i < res->count_connectors; i++) {

			vga_connector = drmModeGetConnectorCurrent(drm_fd,
								   res->connectors[i]);

			if (vga_connector->connector_type == DRM_MODE_CONNECTOR_VGA) {
				start_n_modes = vga_connector->count_modes;
				start_connection = vga_connector->connection;
				break;
			}

			drmModeFreeConnector(vga_connector);

			vga_connector = NULL;
		}

		igt_require(vga_connector);
		igt_skip_on(vga_connector->connection == DRM_MODE_CONNECTED);
	}

	igt_subtest("force-load-detect") {
		igt_set_module_param_int("load_detect_test", 1);

		/* This can't use drmModeGetConnectorCurrent
		 * because connector probing is the point of this test.
		 */
		temp = drmModeGetConnector(drm_fd, vga_connector->connector_id);

		igt_set_module_param_int("load_detect_test", 0);

		igt_assert(temp->connection != DRM_MODE_UNKNOWNCONNECTION);

		drmModeFreeConnector(temp);
	}

	igt_subtest("force-connector-state") {
		igt_display_t display;

		/* force the connector on and check the reported values */
		kmstest_force_connector(drm_fd, vga_connector, FORCE_CONNECTOR_ON);
		temp = drmModeGetConnectorCurrent(drm_fd,
						  vga_connector->connector_id);
		igt_assert_eq(temp->connection, DRM_MODE_CONNECTED);
		igt_assert_lt(0, temp->count_modes);
		drmModeFreeConnector(temp);

		/* attempt to use the display */
		kmstest_set_vt_graphics_mode();
		igt_display_init(&display, drm_fd);
		igt_display_commit(&display);
		igt_display_fini(&display);


		/* force the connector off */
		kmstest_force_connector(drm_fd, vga_connector,
					FORCE_CONNECTOR_OFF);
		temp = drmModeGetConnectorCurrent(drm_fd,
						  vga_connector->connector_id);
		igt_assert_eq(temp->connection, DRM_MODE_DISCONNECTED);
		igt_assert_eq(0, temp->count_modes);
		drmModeFreeConnector(temp);

		/* check that the previous state is restored */
		kmstest_force_connector(drm_fd, vga_connector,
					FORCE_CONNECTOR_UNSPECIFIED);
		temp = drmModeGetConnectorCurrent(drm_fd,
						  vga_connector->connector_id);
		igt_assert_eq(temp->connection, start_connection);
		drmModeFreeConnector(temp);
	}

	igt_subtest("force-edid") {
		kmstest_force_connector(drm_fd, vga_connector,
					FORCE_CONNECTOR_ON);
		temp = drmModeGetConnectorCurrent(drm_fd,
						  vga_connector->connector_id);
		drmModeFreeConnector(temp);

		/* test edid forcing */
		kmstest_force_edid(drm_fd, vga_connector,
				   igt_kms_get_base_edid(), EDID_LENGTH);
		temp = drmModeGetConnectorCurrent(drm_fd,
						  vga_connector->connector_id);

		igt_debug("num_conn %i\n", temp->count_modes);

		CHECK_MODE(temp->modes[0], 1920, 1080, 60);
		/* Don't check non-preferred modes to avoid to tight coupling
		 * with the in-kernel EDID parser. */

		drmModeFreeConnector(temp);

		/* remove edid */
		kmstest_force_edid(drm_fd, vga_connector, NULL, 0);
		kmstest_force_connector(drm_fd, vga_connector,
					FORCE_CONNECTOR_UNSPECIFIED);
		temp = drmModeGetConnectorCurrent(drm_fd,
						  vga_connector->connector_id);
		/* the connector should now have the same number of modes that
		 * it started with */
		igt_assert_eq(temp->count_modes, start_n_modes);
		drmModeFreeConnector(temp);

	}

	igt_subtest("prune-stale-modes") {
		int i;

		kmstest_force_connector(drm_fd, vga_connector,
					FORCE_CONNECTOR_ON);

		/* test pruning of stale modes */
		kmstest_force_edid(drm_fd, vga_connector,
				   igt_kms_get_alt_edid(), EDID_LENGTH);
		temp = drmModeGetConnectorCurrent(drm_fd,
						  vga_connector->connector_id);

		for (i = 0; i < temp->count_modes; i++) {
			if (temp->modes[i].hdisplay == 1400 &&
			    temp->modes[i].vdisplay == 1050)
				break;
		}
		igt_assert_f(i != temp->count_modes, "1400x1050 not on mode list\n");

		drmModeFreeConnector(temp);

		kmstest_force_edid(drm_fd, vga_connector,
				   igt_kms_get_base_edid(), EDID_LENGTH);
		temp = drmModeGetConnectorCurrent(drm_fd,
						  vga_connector->connector_id);

		for (i = 0; i < temp->count_modes; i++) {
			if (temp->modes[i].hdisplay == 1400 &&
			    temp->modes[i].vdisplay == 1050)
				break;
		}
		igt_assert_f(i == temp->count_modes, "1400x1050 not pruned from mode list\n");

		drmModeFreeConnector(temp);

		kmstest_force_edid(drm_fd, vga_connector, NULL, 0);
		kmstest_force_connector(drm_fd, vga_connector,
					FORCE_CONNECTOR_UNSPECIFIED);
	}

	igt_fixture {
		drmModeFreeConnector(vga_connector);
		close(drm_fd);

		reset_connectors();
	}

	igt_exit();
}
