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
#include "igt_edid.h"

igt_simple_main
{
	int drm_fd;
	drmModeRes *res;
	drmModeConnector *connector;
	unsigned char *edid;
	size_t length;
	int mode_count, connector_id;

	drm_fd = drm_open_any_master();
	res = drmModeGetResources(drm_fd);

	igt_assert(drmSetClientCap(drm_fd, DRM_CLIENT_CAP_STEREO_3D, 1) >= 0);

	/* find an hdmi connector */
	for (int i = 0; i < res->count_connectors; i++) {

		connector = drmModeGetConnector(drm_fd, res->connectors[i]);

		if (connector->connector_type == DRM_MODE_CONNECTOR_HDMIA &&
		    connector->connection == DRM_MODE_DISCONNECTED)
			break;

		drmModeFreeConnector(connector);

		connector = NULL;
	}
	igt_require(connector);

	kmstest_edid_add_3d(generic_edid[EDID_FHD], EDID_LENGTH, &edid,
			    &length);

	kmstest_force_edid(drm_fd, connector, edid, length);
	if (!kmstest_force_connector(drm_fd, connector, FORCE_CONNECTOR_ON))
		igt_skip("Could not force connector on\n");

	connector_id = connector->connector_id;

	/* check for 3D modes */
	mode_count = 0;
	connector = drmModeGetConnector(drm_fd, connector_id);
	for (int i = 0; i < connector->count_modes; i++) {
		if (connector->modes[i].flags & DRM_MODE_FLAG_3D_MASK)
			mode_count++;
	}

	igt_assert(mode_count == 13);

	/* set 3D modes */
	igt_info("Testing:\n");
	for (int i = 0; i < connector->count_modes; i++) {
		int fb_id;
		struct kmstest_connector_config config;
		int crtc_mask = -1;
		int ret;

		if (!(connector->modes[i].flags & DRM_MODE_FLAG_3D_MASK))
			continue;

		/* create a configuration */
		ret = kmstest_get_connector_config(drm_fd, connector_id,
						   crtc_mask, &config);
		if (ret != true) {
			igt_info("Error creating configuration for:\n  ");
			kmstest_dump_mode(&connector->modes[i]);

			continue;
		}

		igt_info("  ");
		kmstest_dump_mode(&connector->modes[i]);

		/* create stereo framebuffer */
		fb_id = igt_create_stereo_fb(drm_fd, &connector->modes[i],
					     igt_bpp_depth_to_drm_format(32, 32),
					     I915_TILING_NONE);

		ret = drmModeSetCrtc(drm_fd, config.crtc->crtc_id, fb_id, 0, 0,
				     &connector->connector_id, 1,
				     &connector->modes[i]);

		igt_assert(ret == 0);
	}

	kmstest_force_connector(drm_fd, connector, FORCE_CONNECTOR_UNSPECIFIED);
	kmstest_force_edid(drm_fd, connector, NULL, 0);

	drmModeFreeConnector(connector);
	free(edid);

	igt_exit();
}
