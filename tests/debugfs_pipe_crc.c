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
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "drmtest.h"
#include "igt_debugfs.h"

typedef struct {
	struct kmstest_connector_config config;
	struct kmstest_fb fb;
	bool valid;
} connector_t;

typedef struct {
	int drm_fd;
	igt_debugfs_t debugfs;
	drmModeRes *resources;
	int n_connectors;
	connector_t *connectors;
	FILE *ctl;
} data_t;

static void test_bad_command(data_t *data, const char *cmd)
{
	size_t written;

	written = fwrite(cmd, 1, strlen(cmd), data->ctl);
	fflush(data->ctl);
	igt_assert_cmpint(written, ==, (strlen(cmd)));
	igt_assert(ferror(data->ctl));
	igt_assert_cmpint(errno, ==, EINVAL);
}

static void connector_init(data_t *data, connector_t *connector,
			   uint32_t id, uint32_t crtc_id_mask)
{
	int ret;

	ret = kmstest_get_connector_config(data->drm_fd, id, crtc_id_mask,
					   &connector->config);
	if (ret == 0)
		connector->valid = true;
	else
		connector->valid = false;

}

static void connector_fini(connector_t *connector)
{
	kmstest_free_connector_config(&connector->config);
}

static bool
connector_set_mode(data_t *data, connector_t *connector, drmModeModeInfo *mode)
{
	struct kmstest_connector_config *config = &connector->config;
	unsigned int fb_id;
	cairo_t *cr;
	int ret;

	fb_id = kmstest_create_fb(data->drm_fd,
				  mode->hdisplay, mode->vdisplay,
				  32 /* bpp */, 24 /* depth */,
				  false /* tiling */,
				  &connector->fb);
	igt_assert(fb_id);

	cr = kmstest_get_cairo_ctx(data->drm_fd, &connector->fb);
	kmstest_paint_color(cr, 0, 0, mode->hdisplay, mode->vdisplay,
			    0.0, 1.0, 0.0);
	igt_assert(cairo_status(cr) == 0);

#if 0
	fprintf(stdout, "Using pipe %c, %dx%d\n", pipe_name(config->pipe),
		mode->hdisplay, mode->vdisplay);
#endif

	ret = drmModeSetCrtc(data->drm_fd,
			     config->crtc->crtc_id,
			     connector->fb.fb_id,
			     0, 0, /* x, y */
			     &config->connector->connector_id,
			     1,
			     mode);
	igt_assert(ret == 0);

	return 0;
}

static void display_init(data_t *data)
{
	data->resources = drmModeGetResources(data->drm_fd);
	igt_assert(data->resources);

	data->n_connectors = data->resources->count_connectors;
	data->connectors = calloc(data->n_connectors, sizeof(connector_t));
	igt_assert(data->connectors);
}

static void connectors_init(data_t *data, uint32_t crtc_id_mask)
{
	int i;

	for (i = 0; i < data->n_connectors; i++) {
		uint32_t id = data->resources->connectors[i];

		connector_init(data, &data->connectors[i], id, crtc_id_mask);
	}
}

static void display_fini(data_t *data)
{
	int i;

	for (i = 0; i < data->n_connectors; i++)
		connector_fini(&data->connectors[i]);
	free(data->connectors);

	drmModeFreeResources(data->resources);
}

static connector_t *
display_find_first_valid_connector(data_t *data,
				   uint32_t crtc_id_mask)
{
	int i;

	connectors_init(data, crtc_id_mask);

	for (i = 0;  i < data->n_connectors; i++) {
		connector_t *connector = &data->connectors[i];

		if (connector->valid)
			return connector;
	}

	return NULL;
}

static void test_read_crc(data_t *data, int pipe)
{
	connector_t *connector;
	igt_pipe_crc_t *pipe_crc;
	igt_crc_t *crcs = NULL;

	connector = display_find_first_valid_connector(data, 1 << pipe);
	igt_require_f(connector, "No connector found for pipe %i\n", pipe);

	pipe_crc = igt_pipe_crc_new(&data->debugfs, data->drm_fd,
				    connector->config.pipe,
				    INTEL_PIPE_CRC_SOURCE_PLANE1);

	connector_set_mode(data, connector, &connector->config.default_mode);

	if (!igt_pipe_crc_start(pipe_crc)) {
		igt_pipe_crc_free(pipe_crc);
		pipe_crc = igt_pipe_crc_new(&data->debugfs, data->drm_fd,
					    connector->config.pipe,
					    INTEL_PIPE_CRC_SOURCE_PIPE);
		igt_assert(igt_pipe_crc_start(pipe_crc));
	}

	/* wait for 3 vblanks and the corresponding 3 CRCs */
	igt_pipe_crc_get_crcs(pipe_crc, 3, &crcs);

	igt_pipe_crc_stop(pipe_crc);

	/* and ensure that they'are all equal, we haven't changed the fb */
	igt_assert(igt_crc_equal(&crcs[0], &crcs[1]));
	igt_assert(igt_crc_equal(&crcs[1], &crcs[2]));

	free(crcs);
	igt_pipe_crc_free(pipe_crc);
	kmstest_remove_fb(data->drm_fd, &connector->fb);
}

static void exit_handler(int sig)
{
	igt_pipe_crc_reset();
}

int main(int argc, char **argv)
{
	data_t data = {0, };

	igt_subtest_init(argc, argv);

	igt_fixture {
		size_t written;
		int ret;
		const char *cmd = "pipe A none";

		data.drm_fd = drm_open_any();
		do_or_die(igt_set_vt_graphics_mode());
		do_or_die(igt_install_exit_handler(exit_handler));

		display_init(&data);

		igt_debugfs_init(&data.debugfs);
		data.ctl = igt_debugfs_fopen(&data.debugfs,
					     "i915_display_crc_ctl", "r+");
		igt_require_f(data.ctl,
			      "No display_crc_ctl found, kernel too old\n");
		written = fwrite(cmd, 1, strlen(cmd), data.ctl);
		ret = fflush(data.ctl);
		igt_require_f((written == strlen(cmd) && ret == 0) || errno != ENODEV,
			      "CRCs not supported on this platform\n");
	}

	igt_subtest("bad-pipe")
		test_bad_command(&data, "pipe D none");

	igt_subtest("bad-source")
		test_bad_command(&data, "pipe A foo");

	igt_subtest("bad-nb-words-1")
		test_bad_command(&data, "pipe foo");

	igt_subtest("bad-nb-words-3")
		test_bad_command(&data, "pipe A none option");

	igt_subtest("read-crc-pipe-A")
		test_read_crc(&data, 0);

	igt_subtest("read-crc-pipe-B")
		test_read_crc(&data, 1);

	igt_subtest("read-crc-pipe-C")
		test_read_crc(&data, 2);

	igt_fixture {
		igt_pipe_crc_reset();
		display_fini(&data);
		fclose(data.ctl);
	}

	return 0;
}
