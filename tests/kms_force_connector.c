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

#include "igt_core.h"
#include "igt_kms.h"
#include "drmtest.h"

int
main (int argc, char **argv)
{
	/* force the VGA output and test that it worked */
	int drm_fd;
	drmModeRes *res;
	drmModeConnector *connector, *temp;
	igt_display_t display;

	igt_simple_init();

	drm_fd = drm_open_any();
	res = drmModeGetResources(drm_fd);

	/* find the vga connector */
	for (int i = 0; i < res->count_connectors; i++) {

		connector = drmModeGetConnector(drm_fd, res->connectors[i]);

		if (connector->connector_type == DRM_MODE_CONNECTOR_VGA)
			break;

		drmModeFreeConnector(connector);

		connector = NULL;
	}

	igt_assert(connector);

	/* force the connector on and check the reported values */
	kmstest_force_connector(drm_fd, connector, FORCE_CONNECTOR_ON);
	temp = drmModeGetConnector(drm_fd, connector->connector_id);
	igt_assert(temp->connection == DRM_MODE_CONNECTED);
	igt_assert(temp->count_modes > 0);
	drmModeFreeConnector(temp);

	/* attempt to use the display */
	igt_set_vt_graphics_mode();

	igt_display_init(&display, drm_fd);
	igt_display_commit(&display);


	/* force the connector off */
	kmstest_force_connector(drm_fd, connector, FORCE_CONNECTOR_OFF);
	temp = drmModeGetConnector(drm_fd, connector->connector_id);
	igt_assert(temp->connection == DRM_MODE_DISCONNECTED);
	igt_assert(temp->count_modes == 0);
	drmModeFreeConnector(temp);


	/* check that the previous state is restored */
	kmstest_force_connector(drm_fd, connector, FORCE_CONNECTOR_UNSPECIFIED);
	temp = drmModeGetConnector(drm_fd, connector->connector_id);
	igt_assert(temp->connection == connector->connection);
	drmModeFreeConnector(temp);

	drmModeFreeConnector(connector);

	igt_success();
}
