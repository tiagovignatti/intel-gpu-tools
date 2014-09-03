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

#include "drm_fourcc.h"

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_chipset.h"
#include "igt_debugfs.h"
#include "igt_kms.h"
#include "igt_aux.h"

enum tests {
	TEST_PAGE_FLIP,
	TEST_MMAP_CPU,
	TEST_MMAP_GTT,
	TEST_MMAP_GTT_NO_BUSY,
	TEST_MMAP_GTT_WAITING_NO_BUSY,
	TEST_SETDOMAIN_WAIT_WRITE_GTT,
	TEST_SETDOMAIN_WAIT_WRITE_CPU,
	TEST_BLT,
	TEST_RENDER,
	TEST_PAGE_FLIP_AND_MMAP_CPU,
	TEST_PAGE_FLIP_AND_MMAP_GTT,
	TEST_PAGE_FLIP_AND_BLT,
	TEST_PAGE_FLIP_AND_RENDER,
	TEST_CURSOR_MOVE,
	TEST_SPRITE,
};

bool running_with_psr_disabled;

typedef struct {
	int drm_fd;
	enum tests test;
	drmModeRes *resources;
	drm_intel_bufmgr *bufmgr;
	uint32_t devid;
	uint32_t handle[2];
	uint32_t crtc_id;
	uint32_t crtc_idx;
	uint32_t fb_id[3];
	struct kmstest_connector_config config;
	igt_display_t display;
	struct igt_fb fb[2];
	igt_plane_t *plane[2];
} data_t;

static const char *tests_str(enum tests test)
{
	static const char * const testss[] = {
		[TEST_PAGE_FLIP] = "page_flip",
		[TEST_MMAP_CPU] = "mmap_cpu",
		[TEST_MMAP_GTT] = "mmap_gtt",
		[TEST_MMAP_GTT_NO_BUSY] = "mmap_gtt_no_busy",
		[TEST_MMAP_GTT_WAITING_NO_BUSY] = "mmap_gtt_waiting_no_busy",
		[TEST_SETDOMAIN_WAIT_WRITE_GTT] = "setdomain_wait_write_gtt",
		[TEST_SETDOMAIN_WAIT_WRITE_CPU] = "setdomain_wait_write_cpu",
		[TEST_BLT] = "blt",
		[TEST_RENDER] = "render",
		[TEST_PAGE_FLIP_AND_MMAP_CPU] = "page_flip_and_mmap_cpu",
		[TEST_PAGE_FLIP_AND_MMAP_GTT] = "page_flip_and_mmap_gtt",
		[TEST_PAGE_FLIP_AND_BLT] = "page_flip_and_blt",
		[TEST_PAGE_FLIP_AND_RENDER] = "page_flip_and_render",
		[TEST_CURSOR_MOVE] = "cursor_move",
		[TEST_SPRITE] = "sprite",
	};

	return testss[test];
}

static uint32_t create_fb(data_t *data,
			  int w, int h,
			  double r, double g, double b,
			  struct igt_fb *fb)
{
	uint32_t fb_id;
	cairo_t *cr;

	fb_id = igt_create_fb(data->drm_fd, w, h,
			      DRM_FORMAT_XRGB8888, I915_TILING_X, fb);
	igt_assert(fb_id);

	cr = igt_get_cairo_ctx(data->drm_fd, fb);
	igt_paint_color(cr, 0, 0, w, h, r, g, b);
	igt_assert(cairo_status(cr) == 0);
	cairo_destroy(cr);

	return fb_id;
}

static void create_cursor_fb(data_t *data, struct igt_fb *fb)
{
	cairo_t *cr;

	data->fb_id[2] = igt_create_fb(data->drm_fd, 64, 64,
				       DRM_FORMAT_ARGB8888, I915_TILING_NONE,
				       fb);
	igt_assert(data->fb_id[2]);

	cr = igt_get_cairo_ctx(data->drm_fd, fb);
	igt_paint_color_alpha(cr, 0, 0, 64, 64, 1.0, 1.0, 1.0, 1.0);
	igt_assert(cairo_status(cr) == 0);
}

static bool
connector_set_mode(data_t *data, drmModeModeInfo *mode, uint32_t fb_id)
{
	struct kmstest_connector_config *config = &data->config;
	int ret;

#if 0
	fprintf(stdout, "Using pipe %s, %dx%d\n", kmstest_pipe_name(config->pipe),
		mode->hdisplay, mode->vdisplay);
#endif

	ret = drmModeSetCrtc(data->drm_fd,
			     config->crtc->crtc_id,
			     fb_id,
			     0, 0, /* x, y */
			     &config->connector->connector_id,
			     1,
			     mode);
	igt_assert(ret == 0);

	return 0;
}

static void display_init(data_t *data)
{
	igt_display_init(&data->display, data->drm_fd);
	data->resources = drmModeGetResources(data->drm_fd);
	igt_assert(data->resources);
}

static void display_fini(data_t *data)
{
	igt_display_fini(&data->display);
	drmModeSetCursor(data->drm_fd, data->crtc_id, 0, 0, 0);
	drmModeFreeResources(data->resources);
}

static void fill_blt(data_t *data, uint32_t handle, unsigned char color)
{
	drm_intel_bo *dst = gem_handle_to_libdrm_bo(data->bufmgr,
						    data->drm_fd,
						    "", handle);
	struct intel_batchbuffer *batch;

	batch = intel_batchbuffer_alloc(data->bufmgr, data->devid);
	igt_assert(batch);

	COLOR_BLIT_COPY_BATCH_START(0);
	OUT_BATCH((1 << 24) | (0xf0 << 16) | 0);
	OUT_BATCH(1 << 16 | 4);
	OUT_RELOC(dst, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(color);
	ADVANCE_BATCH();

	intel_batchbuffer_flush(batch);
	intel_batchbuffer_free(batch);

	gem_bo_busy(data->drm_fd, handle);
}

static void scratch_buf_init(struct igt_buf *buf, drm_intel_bo *bo)
{
	buf->bo = bo;
	buf->stride = 4096;
	buf->tiling = I915_TILING_X;
	buf->size = 4096;
}

static void fill_render(data_t *data, uint32_t handle, unsigned char color)
{
	drm_intel_bo *src, *dst;
	struct intel_batchbuffer *batch;
	struct igt_buf src_buf, dst_buf;
	const uint8_t buf[4] = { color, color, color, color };
	igt_render_copyfunc_t rendercopy = igt_get_render_copyfunc(data->devid);

	igt_skip_on(!rendercopy);

	dst = gem_handle_to_libdrm_bo(data->bufmgr, data->drm_fd, "", handle);
	igt_assert(dst);

	src = drm_intel_bo_alloc(data->bufmgr, "", 4096, 4096);
	igt_assert(src);

	gem_write(data->drm_fd, src->handle, 0, buf, 4);

	scratch_buf_init(&src_buf, src);
	scratch_buf_init(&dst_buf, dst);

	batch = intel_batchbuffer_alloc(data->bufmgr, data->devid);
	igt_assert(batch);

	rendercopy(batch, NULL,
		   &src_buf, 0, 0, 1, 1,
		   &dst_buf, 0, 0);

	intel_batchbuffer_free(batch);

	gem_bo_busy(data->drm_fd, handle);
}

static bool psr_enabled(data_t *data)
{
	int ret;
	FILE *file;
	char str[4];

	if (running_with_psr_disabled)
		return true;

	file = igt_debugfs_fopen("i915_edp_psr_status", "r");
	igt_require(file);

	ret = fscanf(file, "Sink_Support: %s\n", str);
	igt_assert(ret != 0);
	ret = fscanf(file, "Source_OK: %s\n", str);
	igt_assert(ret != 0);
	ret = fscanf(file, "Enabled: %s\n", str);
	igt_assert(ret != 0);

	fclose(file);
	return strcmp(str, "yes") == 0;
}

static bool psr_active(data_t *data)
{
	int ret;
	FILE *file;
	char str[4];

	if (running_with_psr_disabled)
		return true;

	file = igt_debugfs_fopen("i915_edp_psr_status", "r");
	igt_require(file);

	ret = fscanf(file, "Sink_Support: %s\n", str);
	igt_assert(ret != 0);
	ret = fscanf(file, "Source_OK: %s\n", str);
	igt_assert(ret != 0);
	ret = fscanf(file, "Enabled: %s\n", str);
	igt_assert(ret != 0);
	ret = fscanf(file, "Active: %s\n", str);
	igt_assert(ret != 0);
	ret = fscanf(file, "Busy frontbuffer bits: %s\n", str);
	igt_assert(ret != 0);
	ret = fscanf(file, "Re-enable work scheduled: %s\n", str);
	igt_assert(ret != 0);
	ret = fscanf(file, "HW Enabled & Active bit: %s\n", str);
	igt_assert(ret != 0);

	fclose(file);
	return strcmp(str, "yes") == 0;
}

static bool wait_psr_entry(data_t *data, int timeout)
{
	while (timeout--) {
		if (psr_active(data))
			return true;
		sleep(1);
	}
	return false;
}

static void get_sink_crc(data_t *data, char *crc) {
	int ret;
	FILE *file;

	file = igt_debugfs_fopen("i915_sink_crc_eDP1", "r");
	igt_require(file);

	ret = fscanf(file, "%s\n", crc);
	igt_require(ret > 0);

	fclose(file);

	igt_debug("%s\n", crc);
	igt_debug_wait_for_keypress("crc");

	/* The important value was already taken.
	 * Now give a time for human eyes
	 */
	usleep(300000);
}

static void test_crc(data_t *data)
{
	uint32_t handle = data->handle[0];
	char ref_crc[12];
	char crc[12];

	if (data->test == TEST_CURSOR_MOVE) {
		igt_assert(drmModeSetCursor(data->drm_fd, data->crtc_id,
					    handle, 64, 64) == 0);
		igt_assert(drmModeMoveCursor(data->drm_fd, data->crtc_id,
					     1, 1) == 0);
	}

	igt_assert(wait_psr_entry(data, 10));
	get_sink_crc(data, ref_crc);

	switch (data->test) {
		void *ptr;
	case TEST_PAGE_FLIP:
		igt_assert(drmModePageFlip(data->drm_fd, data->crtc_id,
					   data->fb_id[1], 0, NULL) == 0);
		break;
	case TEST_PAGE_FLIP_AND_MMAP_CPU:
		handle = data->handle[1];
		igt_assert(drmModePageFlip(data->drm_fd, data->crtc_id,
					   data->fb_id[1], 0, NULL) == 0);
	case TEST_MMAP_CPU:
		ptr = gem_mmap__cpu(data->drm_fd, handle, 4096, PROT_WRITE);
		gem_set_domain(data->drm_fd, handle,
			       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
		sleep(1);
		memset(ptr, 0, 4);
		munmap(ptr, 4096);
		sleep(1);
		gem_sw_finish(data->drm_fd, handle);
		break;
	case TEST_PAGE_FLIP_AND_MMAP_GTT:
		handle = data->handle[1];
		igt_assert(drmModePageFlip(data->drm_fd, data->crtc_id,
					   data->fb_id[1], 0, NULL) == 0);
	case TEST_MMAP_GTT:
		ptr = gem_mmap__gtt(data->drm_fd, handle, 4096, PROT_WRITE);
		gem_set_domain(data->drm_fd, handle,
			       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
		memset(ptr, 0xff, 4);
		munmap(ptr, 4096);
		gem_bo_busy(data->drm_fd, handle);
		break;
	case TEST_MMAP_GTT_NO_BUSY:
		ptr = gem_mmap__gtt(data->drm_fd, handle, 4096, PROT_WRITE);
		gem_set_domain(data->drm_fd, handle,
			       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
		memset(ptr, 0xff, 4);
		munmap(ptr, 4096);
		break;
	case TEST_MMAP_GTT_WAITING_NO_BUSY:
		ptr = gem_mmap__gtt(data->drm_fd, handle, 4096, PROT_WRITE);
		gem_set_domain(data->drm_fd, handle,
			       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
		igt_info("Sleeping for 10 sec...\n");
                sleep(10);
		memset(ptr, 0xff, 4);
		munmap(ptr, 4096);
		break;
	case TEST_SETDOMAIN_WAIT_WRITE_GTT:
		ptr = gem_mmap__gtt(data->drm_fd, handle, 4096, PROT_WRITE);
		fill_blt(data, handle, 0xff);
		igt_assert(wait_psr_entry(data, 10));
		get_sink_crc(data, ref_crc);
		gem_set_domain(data->drm_fd, handle,
			       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
		igt_info("Sleeping for 10 sec...\n");
		sleep(10);
		memset(ptr, 0xff, 4);
		munmap(ptr, 4096);
		break;
	case TEST_SETDOMAIN_WAIT_WRITE_CPU:
		ptr = gem_mmap__cpu(data->drm_fd, handle, 4096, PROT_WRITE);
		fill_blt(data, handle, 0xff);
		igt_assert(wait_psr_entry(data, 10));
		get_sink_crc(data, ref_crc);
		gem_set_domain(data->drm_fd, handle,
			       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
		igt_info("Sleeping for 10 sec...\n");
		sleep(10);
		memset(ptr, 0xff, 4);
		munmap(ptr, 4096);
		gem_sw_finish(data->drm_fd, handle);
		break;
	case TEST_BLT:
	case TEST_PAGE_FLIP_AND_BLT:
		fill_blt(data, handle, 0xff);
		break;
	case TEST_RENDER:
	case TEST_PAGE_FLIP_AND_RENDER:
		fill_render(data, handle, 0xff);
		break;
	case TEST_CURSOR_MOVE:
		igt_assert(drmModeMoveCursor(data->drm_fd, data->crtc_id, 1, 2) == 0);
		break;
	case TEST_SPRITE:
		igt_plane_set_fb(data->plane[0], &data->fb[0]);
		igt_display_commit(&data->display);
		igt_plane_set_fb(data->plane[1], &data->fb[1]);
		igt_display_commit(&data->display);
		break;
	}

	igt_wait_for_vblank(data->drm_fd, data->crtc_idx);

	get_sink_crc(data, crc);
	igt_assert(strcmp(ref_crc, crc) != 0);
}

static bool prepare_crtc(data_t *data, uint32_t connector_id)
{
	if (!kmstest_get_connector_config(data->drm_fd,
					  connector_id,
					  1 << data->crtc_idx,
					  &data->config))
		return false;

	data->fb_id[0] = create_fb(data,
				   data->config.default_mode.hdisplay,
				   data->config.default_mode.vdisplay,
				   0.0, 1.0, 0.0, &data->fb[0]);
	igt_assert(data->fb_id[0]);

	if (data->test == TEST_CURSOR_MOVE)
		create_cursor_fb(data, &data->fb[0]);

	data->fb_id[1] = create_fb(data,
				   data->config.default_mode.hdisplay,
				   data->config.default_mode.vdisplay,
				   1.0, 0.0, 0.0, &data->fb[1]);
	igt_assert(data->fb_id[1]);

	data->handle[0] = data->fb[0].gem_handle;
	data->handle[1] = data->fb[1].gem_handle;

	/* scanout = fb[1] */
	connector_set_mode(data, &data->config.default_mode,
			   data->fb_id[1]);

	/* scanout = fb[0] */
	connector_set_mode(data, &data->config.default_mode,
			   data->fb_id[0]);

	kmstest_free_connector_config(&data->config);

	return true;
}

static void test_sprite(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output;
	drmModeModeInfo *mode;

	for_each_connected_output(display, output) {
		drmModeConnectorPtr c = output->config.connector;

		if (c->connector_type != DRM_MODE_CONNECTOR_eDP ||
		    c->connection != DRM_MODE_CONNECTED)
			continue;

		igt_output_set_pipe(output, PIPE_ANY);

		mode = igt_output_get_mode(output);

		igt_create_color_fb(data->drm_fd,
				    mode->hdisplay, mode->vdisplay,
				    DRM_FORMAT_XRGB8888, I915_TILING_X,
				    0.0, 1.0, 0.0,
				    &data->fb[0]);

		igt_create_color_fb(data->drm_fd,
				    mode->hdisplay/2, mode->vdisplay/2,
				    DRM_FORMAT_XRGB8888, I915_TILING_X,
				    1.0, 0.0, 0.0,
				    &data->fb[1]);

		data->plane[0] = igt_output_get_plane(output, 0);
		data->plane[1] = igt_output_get_plane(output, 1);

		test_crc(data);
	}
}

static void run_test(data_t *data)
{
	int i, n;
	drmModeConnectorPtr c;
	/* Baytrail supports per-pipe PSR configuration, however PSR on
	 * PIPE_B isn't working properly. So let's keep it disabled for now.
	 * crtcs = IS_VALLEYVIEW(data->devid)? 2 : 1; */
	int crtcs = 1;

	if (data->test == TEST_SPRITE) {
		test_sprite(data);
		return;
	}

	for (i = 0; i < data->resources->count_connectors; i++) {
		uint32_t connector_id = data->resources->connectors[i];
		c = drmModeGetConnector(data->drm_fd, connector_id);

		if (c->connector_type != DRM_MODE_CONNECTOR_eDP ||
		    c->connection != DRM_MODE_CONNECTED)
			continue;
		for (n = 0; n < crtcs; n++) {
			data->crtc_idx = n;
			data->crtc_id = data->resources->crtcs[n];

			if (!prepare_crtc(data, connector_id))
				continue;

			test_crc(data);
		}
	}
}

igt_main
{
	data_t data = {};
	enum tests test;
	char *env_psr;

	env_psr = getenv("IGT_PSR_DISABLED");

	running_with_psr_disabled = (bool) env_psr;

	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_any();
		kmstest_set_vt_graphics_mode();

		data.devid = intel_get_drm_devid(data.drm_fd);

		igt_skip_on(!psr_enabled(&data));

		data.bufmgr = drm_intel_bufmgr_gem_init(data.drm_fd, 4096);
		igt_assert(data.bufmgr);
		drm_intel_bufmgr_gem_enable_reuse(data.bufmgr);

		display_init(&data);
	}

	for (test = TEST_PAGE_FLIP; test <= TEST_SPRITE; test++) {
		igt_subtest_f("%s", tests_str(test)) {
			data.test = test;
			run_test(&data);
		}
	}

	igt_fixture {
		drm_intel_bufmgr_destroy(data.bufmgr);
		display_fini(&data);
	}
}
