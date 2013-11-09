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

#include "drm_fourcc.h"

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"
#include "rendercopy.h"

enum test_mode {
	TEST_PAGE_FLIP,
	TEST_MMAP_CPU,
	TEST_MMAP_GTT,
	TEST_BLT,
	TEST_RENDER,
	TEST_CONTEXT,
	TEST_PAGE_FLIP_AND_MMAP_CPU,
	TEST_PAGE_FLIP_AND_MMAP_GTT,
	TEST_PAGE_FLIP_AND_BLT,
	TEST_PAGE_FLIP_AND_RENDER,
	TEST_PAGE_FLIP_AND_CONTEXT,
};

typedef struct {
	struct kmstest_connector_config config;
	drmModeModeInfo mode;
	struct kmstest_fb fb[2];
} connector_t;

typedef struct {
	int drm_fd;
	igt_debugfs_t debugfs;
	drmModeRes *resources;
	FILE *ctl;
	igt_crc_t ref_crc[2];
	igt_pipe_crc_t **pipe_crc;
	drm_intel_bufmgr *bufmgr;
	drm_intel_context *ctx[2];
	uint32_t devid;
	uint32_t handle[2];
	uint32_t crtc_id;
	uint32_t crtc_idx;
	uint32_t fb_id[2];
} data_t;

static const char *test_mode_str(enum test_mode mode)
{
	static const char * const test_modes[] = {
		[TEST_PAGE_FLIP] = "page_flip",
		[TEST_MMAP_CPU] = "mmap_cpu",
		[TEST_MMAP_GTT] = "mmap_gtt",
		[TEST_BLT] = "blt",
		[TEST_RENDER] = "render",
		[TEST_CONTEXT] = "context",
		[TEST_PAGE_FLIP_AND_MMAP_CPU] = "page_flip_and_mmap_cpu",
		[TEST_PAGE_FLIP_AND_MMAP_GTT] = "page_flip_and_mmap_gtt",
		[TEST_PAGE_FLIP_AND_BLT] = "page_flip_and_blt",
		[TEST_PAGE_FLIP_AND_RENDER] = "page_flip_and_render",
		[TEST_PAGE_FLIP_AND_CONTEXT] = "page_flip_and_context",
	};

	return test_modes[mode];
}

static uint32_t create_fb(data_t *data,
			  int w, int h,
			  double r, double g, double b,
			  struct kmstest_fb *fb)
{
	uint32_t fb_id;
	cairo_t *cr;

	fb_id = kmstest_create_fb2(data->drm_fd, w, h,
				   DRM_FORMAT_XRGB8888, true, fb);
	igt_assert(fb_id);

	cr = kmstest_get_cairo_ctx(data->drm_fd, fb);
	kmstest_paint_color(cr, 0, 0, w, h, r, g, b);
	igt_assert(cairo_status(cr) == 0);

	return fb_id;
}

static bool
connector_set_mode(data_t *data, connector_t *connector,
		   drmModeModeInfo *mode, uint32_t fb_id)
{
	struct kmstest_connector_config *config = &connector->config;
	int ret;

#if 0
	fprintf(stdout, "Using pipe %c, %dx%d\n", pipe_name(config->pipe),
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
	data->resources = drmModeGetResources(data->drm_fd);
	igt_assert(data->resources);

	data->pipe_crc = calloc(data->resources->count_crtcs, sizeof(data->pipe_crc[0]));
}

static void display_fini(data_t *data)
{
	free(data->pipe_crc);
}

static void fill_blt(data_t *data, uint32_t handle, unsigned char color)
{
	drm_intel_bo *dst = gem_handle_to_libdrm_bo(data->bufmgr,
						    data->drm_fd,
						    "", handle);
	struct intel_batchbuffer *batch;

	batch = intel_batchbuffer_alloc(data->bufmgr, data->devid);
	igt_assert(batch);

	BEGIN_BATCH(5);
	OUT_BATCH(COLOR_BLT_CMD);
	OUT_BATCH((1 << 24) | (0xf0 << 16) | 0);
	OUT_BATCH(1 << 16 | 4);
	OUT_RELOC(dst, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(color);
	ADVANCE_BATCH();

	intel_batchbuffer_flush(batch);
	intel_batchbuffer_free(batch);

	gem_bo_busy(data->drm_fd, handle);
}

static void scratch_buf_init(struct scratch_buf *buf, drm_intel_bo *bo)
{
	buf->bo = bo;
	buf->stride = 4096;
	buf->tiling = I915_TILING_X;
	buf->size = 4096;
}

static void exec_nop(data_t *data, uint32_t handle, drm_intel_context *context)
{
	drm_intel_bo *dst;
	struct intel_batchbuffer *batch;

	dst = gem_handle_to_libdrm_bo(data->bufmgr, data->drm_fd, "", handle);
	igt_assert(dst);

	batch = intel_batchbuffer_alloc(data->bufmgr, data->devid);
	igt_assert(batch);

	/* add the reloc to make sure the kernel will think we write to dst */
	BEGIN_BATCH(4);
	OUT_BATCH(MI_BATCH_BUFFER_END);
	OUT_BATCH(MI_NOOP);
	OUT_RELOC(dst, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(MI_NOOP);
	ADVANCE_BATCH();

	intel_batchbuffer_flush_with_context(batch, context);
	intel_batchbuffer_free(batch);
}

static void fill_render(data_t *data, uint32_t handle,
			drm_intel_context *context, unsigned char color)
{
	drm_intel_bo *src, *dst;
	struct intel_batchbuffer *batch;
	struct scratch_buf src_buf, dst_buf;
	const uint8_t buf[4] = { color, color, color, color };
	render_copyfunc_t rendercopy = get_render_copyfunc(data->devid);

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

	rendercopy(batch, context,
		   &src_buf, 0, 0, 1, 1,
		   &dst_buf, 0, 0);

	intel_batchbuffer_free(batch);

	gem_bo_busy(data->drm_fd, handle);
}

static bool fbc_enabled(data_t *data)
{
	FILE *status;
	char str[64] = {};

	status = igt_debugfs_fopen(&data->debugfs, "i915_fbc_status", "r");
	fread(str, sizeof(str) - 1, 1, status);
	fclose(status);
	return strstr(str, "FBC enabled") != NULL;
}

static void test_crc(data_t *data, enum test_mode mode)
{
	static const unsigned char buf[1] = { 0xcc };
	igt_pipe_crc_t *pipe_crc = data->pipe_crc[data->crtc_idx];
	igt_crc_t *crcs = NULL;
	uint32_t handle = data->handle[0];

	igt_assert(fbc_enabled(data));

	if (mode >= TEST_PAGE_FLIP_AND_MMAP_CPU) {
		handle = data->handle[1];
		igt_assert(drmModePageFlip(data->drm_fd, data->crtc_id,
					   data->fb_id[1], 0, NULL) == 0);
		usleep(300000);

		igt_assert(fbc_enabled(data));
	}

	switch (mode) {
		void *ptr;
	case TEST_PAGE_FLIP:
		igt_assert(drmModePageFlip(data->drm_fd, data->crtc_id,
					   data->fb_id[1], 0, NULL) == 0);
		break;
	case TEST_MMAP_CPU:
	case TEST_PAGE_FLIP_AND_MMAP_CPU:
		ptr = gem_mmap__cpu(data->drm_fd, handle, 4096, PROT_WRITE);
		gem_set_domain(data->drm_fd, handle, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
		memset(ptr, 0xff, 4);
		munmap(ptr, 4096);
		gem_sw_finish(data->drm_fd, handle);
		break;
	case TEST_MMAP_GTT:
	case TEST_PAGE_FLIP_AND_MMAP_GTT:
		ptr = gem_mmap__gtt(data->drm_fd, handle, 4096, PROT_WRITE);
		gem_set_domain(data->drm_fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
		memset(ptr, 0xff, 4);
		munmap(ptr, 4096);
		break;
	case TEST_BLT:
	case TEST_PAGE_FLIP_AND_BLT:
		fill_blt(data, handle, 0xff);
		break;
	case TEST_RENDER:
	case TEST_CONTEXT:
	case TEST_PAGE_FLIP_AND_RENDER:
	case TEST_PAGE_FLIP_AND_CONTEXT:
		fill_render(data, handle,
			    (mode == TEST_CONTEXT || mode == TEST_PAGE_FLIP_AND_CONTEXT) ?
			    data->ctx[1] : NULL, 0xff);
		break;
	}

	/*
	 * Make sure we're looking at new data (two vblanks
	 * to leave some leeway for the kernel if we ever do
	 * some kind of delayed FBC disable for GTT mmaps.
	 */
	igt_wait_for_vblank(data->drm_fd, data->crtc_idx);
	igt_wait_for_vblank(data->drm_fd, data->crtc_idx);

	igt_pipe_crc_start(pipe_crc);
	igt_pipe_crc_get_crcs(pipe_crc, 1, &crcs);
	igt_pipe_crc_stop(pipe_crc);
	igt_assert(!igt_crc_equal(&crcs[0], &data->ref_crc[0]));
	if (mode == TEST_PAGE_FLIP)
		igt_assert(igt_crc_equal(&crcs[0], &data->ref_crc[1]));
	else
		igt_assert(!igt_crc_equal(&crcs[0], &data->ref_crc[1]));
	free(crcs);

	/*
	 * Allow time for FBC to kick in again if it
	 * got disabled during dirtyfb or page flip.
	 */
	usleep(300000);

	igt_assert(fbc_enabled(data));

	igt_pipe_crc_start(pipe_crc);
	igt_pipe_crc_get_crcs(pipe_crc, 1, &crcs);
	igt_pipe_crc_stop(pipe_crc);
	igt_assert(!igt_crc_equal(&crcs[0], &data->ref_crc[0]));
	if (mode == TEST_PAGE_FLIP)
		igt_assert(igt_crc_equal(&crcs[0], &data->ref_crc[1]));
	else
		igt_assert(!igt_crc_equal(&crcs[0], &data->ref_crc[1]));
	free(crcs);
}

static bool prepare_crtc(data_t *data, uint32_t connector_id, enum test_mode mode)
{
	igt_pipe_crc_t *pipe_crc;
	igt_crc_t *crcs = NULL;
	connector_t connector;
	int ret;

	ret = kmstest_get_connector_config(data->drm_fd,
					   connector_id,
					   1 << data->crtc_idx,
					   &connector.config);
	if (ret)
		return false;

	igt_pipe_crc_free(data->pipe_crc[data->crtc_idx]);
	data->pipe_crc[data->crtc_idx] = NULL;

	pipe_crc = igt_pipe_crc_new(&data->debugfs,
				    data->drm_fd, data->crtc_idx,
				    INTEL_PIPE_CRC_SOURCE_AUTO);
	if (!pipe_crc) {
		printf("auto crc not supported on this connector with crtc %i\n",
		       data->crtc_idx);
		return false;
	}

	data->pipe_crc[data->crtc_idx] = pipe_crc;

	data->fb_id[0] = create_fb(data,
				   connector.config.default_mode.hdisplay,
				   connector.config.default_mode.vdisplay,
				   0.0, 0.0, 0.0, &connector.fb[0]);
	igt_assert(data->fb_id[0]);

	data->fb_id[1] = create_fb(data,
				   connector.config.default_mode.hdisplay,
				   connector.config.default_mode.vdisplay,
				   0.1, 0.1, 0.1, &connector.fb[1]);
	igt_assert(data->fb_id[1]);

	data->handle[0] = connector.fb[0].gem_handle;
	data->handle[1] = connector.fb[1].gem_handle;

	/* scanout = fb[1] */
	connector_set_mode(data, &connector, &connector.config.default_mode,
			   data->fb_id[1]);
	usleep(300000);

	igt_skip_on(!fbc_enabled(data));

	igt_wait_for_vblank(data->drm_fd, data->crtc_idx);

	/* get reference crc for fb[1] */
	igt_pipe_crc_start(pipe_crc);
	igt_pipe_crc_get_crcs(pipe_crc, 1, &crcs);
	data->ref_crc[1] = crcs[0];
	igt_pipe_crc_stop(pipe_crc);
	free(crcs);

	if (mode == TEST_CONTEXT || mode == TEST_PAGE_FLIP_AND_CONTEXT) {
		data->ctx[0] = drm_intel_gem_context_create(data->bufmgr);
		igt_require(data->ctx[0]);
		data->ctx[1] = drm_intel_gem_context_create(data->bufmgr);
		igt_require(data->ctx[1]);

		/*
		 * Disable FBC RT address for both contexts
		 * (by "rendering" to a non-scanout buffer).
		 */
		exec_nop(data, data->handle[0], data->ctx[1]);
		exec_nop(data, data->handle[0], data->ctx[0]);
		exec_nop(data, data->handle[0], data->ctx[1]);
		exec_nop(data, data->handle[0], data->ctx[0]);
	}

	/* scanout = fb[0] */
	connector_set_mode(data, &connector, &connector.config.default_mode,
			   data->fb_id[0]);
	usleep(300000);

	igt_skip_on(!fbc_enabled(data));

	if (mode == TEST_CONTEXT || mode == TEST_PAGE_FLIP_AND_CONTEXT) {
		/*
		 * make ctx[0] FBC RT address point to fb[0], ctx[1]
		 * FBC RT address is left as disabled.
		 */
		exec_nop(data, connector.fb[0].gem_handle, data->ctx[0]);
	}

	igt_wait_for_vblank(data->drm_fd, data->crtc_idx);

	/* get reference crc for fb[0] */
	igt_pipe_crc_start(pipe_crc);
	igt_pipe_crc_get_crcs(pipe_crc, 1, &crcs);
	data->ref_crc[0] = crcs[0];
	igt_pipe_crc_stop(pipe_crc);
	free(crcs);

	kmstest_free_connector_config(&connector.config);

	return true;
}

static void finish_crtc(data_t *data, enum test_mode mode)
{
	igt_pipe_crc_free(data->pipe_crc[data->crtc_idx]);
	data->pipe_crc[data->crtc_idx] = NULL;

	if (mode == TEST_CONTEXT || mode == TEST_PAGE_FLIP_AND_CONTEXT) {
		drm_intel_gem_context_destroy(data->ctx[0]);
		drm_intel_gem_context_destroy(data->ctx[1]);
	}
}

static void run_test(data_t *data, enum test_mode mode)
{
	int i, n;

	for (i = 0; i < data->resources->count_connectors; i++) {
		uint32_t connector_id = data->resources->connectors[i];

		for (n = 0; n < data->resources->count_crtcs; n++) {
			data->crtc_idx = n;
			data->crtc_id = data->resources->crtcs[n];

			if (!prepare_crtc(data, connector_id, mode))
				continue;

			fprintf(stdout, "Beginning %s on crtc %d, connector %d\n",
				igt_subtest_name(), data->crtc_id, connector_id);
			test_crc(data, mode);

			fprintf(stdout, "\n%s on crtc %d, connector %d: PASSED\n\n",
				igt_subtest_name(), data->crtc_id, connector_id);

			finish_crtc(data, mode);
		}
	}
}

igt_main
{
	data_t data = {};
	enum test_mode mode;

	igt_skip_on_simulation();

	igt_fixture {
		size_t written;
		int ret;
		const char *cmd = "pipe A none";
		char buf[64];
		FILE *status;

		data.drm_fd = drm_open_any();
		igt_require(data.drm_fd);
		igt_set_vt_graphics_mode();

		data.devid = intel_get_drm_devid(data.drm_fd);

		igt_debugfs_init(&data.debugfs);
		data.ctl = igt_debugfs_fopen(&data.debugfs,
					     "i915_display_crc_ctl", "r+");
		igt_require_f(data.ctl,
			      "No display_crc_ctl found, kernel too old\n");
		written = fwrite(cmd, 1, strlen(cmd), data.ctl);
		ret = fflush(data.ctl);
		igt_require_f((written == strlen(cmd) && ret == 0) || errno != ENODEV,
			      "CRCs not supported on this platform\n");

		status = igt_debugfs_fopen(&data.debugfs, "i915_fbc_status", "r");
		igt_require_f(status, "No i915_fbc_status found\n");
		fread(buf, sizeof(buf), 1, status);
		fclose(status);
		buf[sizeof(buf) - 1] = '\0';
		igt_require_f(!strstr(buf, "unsupported by this chipset") &&
			      !strstr(buf, "disabled per module param") &&
			      !strstr(buf, "disabled per chip default"),
			      "FBC not supported/enabled\n");

		data.bufmgr = drm_intel_bufmgr_gem_init(data.drm_fd, 4096);
		igt_assert(data.bufmgr);
		drm_intel_bufmgr_gem_enable_reuse(data.bufmgr);

		display_init(&data);
	}

	for (mode = TEST_PAGE_FLIP; mode <= TEST_PAGE_FLIP_AND_CONTEXT; mode++) {
		igt_subtest_f("%s", test_mode_str(mode)) {
			run_test(&data, mode);
		}
	}

	igt_fixture {
		drm_intel_bufmgr_destroy(data.bufmgr);
		display_fini(&data);
		fclose(data.ctl);
	}
}
