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

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"
#include "intel_chipset.h"
#include "intel_batchbuffer.h"
#include "ioctl_wrappers.h"

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
	int drm_fd;
	igt_crc_t ref_crc[2];
	igt_pipe_crc_t *pipe_crc;
	drm_intel_bufmgr *bufmgr;
	drm_intel_context *ctx[2];
	uint32_t devid;
	uint32_t handle[2];
	igt_display_t display;
	igt_output_t *output;
	enum pipe pipe;
	igt_plane_t *primary;
	struct igt_fb fb[2];
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

static void scratch_buf_init(struct igt_buf *buf, drm_intel_bo *bo)
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

	status = igt_debugfs_fopen("i915_fbc_status", "r");
	igt_assert(status);

	fread(str, sizeof(str) - 1, 1, status);
	fclose(status);
	return strstr(str, "FBC enabled") != NULL;
}

static void test_crc(data_t *data, enum test_mode mode)
{
	uint32_t crtc_id = data->output->config.crtc->crtc_id;
	igt_pipe_crc_t *pipe_crc = data->pipe_crc;
	igt_crc_t *crcs = NULL;
	uint32_t handle = data->handle[0];

	igt_assert(fbc_enabled(data));

	if (mode >= TEST_PAGE_FLIP_AND_MMAP_CPU) {
		handle = data->handle[1];
		igt_assert(drmModePageFlip(data->drm_fd, crtc_id,
					   data->fb_id[1], 0, NULL) == 0);
		usleep(300000);

		igt_assert(fbc_enabled(data));
	}

	switch (mode) {
		void *ptr;
	case TEST_PAGE_FLIP:
		igt_assert(drmModePageFlip(data->drm_fd, crtc_id,
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
	igt_wait_for_vblank(data->drm_fd, data->pipe);
	igt_wait_for_vblank(data->drm_fd, data->pipe);

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

static bool prepare_crtc(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;

	/* select the pipe we want to use */
	igt_output_set_pipe(output, data->pipe);
	igt_display_commit(display);

	if (!output->valid) {
		igt_output_set_pipe(output, PIPE_ANY);
		igt_display_commit(display);
		return false;
	}

	return true;
}

static bool prepare_test(data_t *data, enum test_mode test_mode)
{
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;
	drmModeModeInfo *mode;
	igt_pipe_crc_t *pipe_crc;
	igt_crc_t *crcs = NULL;

	data->primary = igt_output_get_plane(data->output, IGT_PLANE_PRIMARY);
	mode = igt_output_get_mode(data->output);

	data->fb_id[0] = igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					     DRM_FORMAT_XRGB8888,
					     true, /* tiled */
					     0.0, 0.0, 0.0, &data->fb[0]);
	igt_assert(data->fb_id[0]);
	data->fb_id[1] = igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
					     DRM_FORMAT_XRGB8888,
					     true, /* tiled */
					     0.1, 0.1, 0.1,
					     &data->fb[1]);
	igt_assert(data->fb_id[1]);

	data->handle[0] = data->fb[0].gem_handle;
	data->handle[1] = data->fb[1].gem_handle;

	/* scanout = fb[1] */
	igt_plane_set_fb(data->primary, &data->fb[1]);
	igt_display_commit(display);
	usleep(300000);

	if (!fbc_enabled(data)) {
		igt_info("FBC not enabled\n");

		igt_plane_set_fb(data->primary, NULL);
		igt_output_set_pipe(output, PIPE_ANY);
		igt_display_commit(display);

		igt_remove_fb(data->drm_fd, &data->fb[0]);
		igt_remove_fb(data->drm_fd, &data->fb[1]);
		return false;
	}

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	pipe_crc = igt_pipe_crc_new(data->pipe,
				    INTEL_PIPE_CRC_SOURCE_AUTO);
	if (!pipe_crc) {
		igt_info("auto crc not supported on this connector with crtc %i\n",
			 data->pipe);

		igt_plane_set_fb(data->primary, NULL);
		igt_output_set_pipe(output, PIPE_ANY);
		igt_display_commit(display);

		igt_remove_fb(data->drm_fd, &data->fb[0]);
		igt_remove_fb(data->drm_fd, &data->fb[1]);
		return false;
	}

	data->pipe_crc = pipe_crc;

	igt_wait_for_vblank(data->drm_fd, data->pipe);

	/* get reference crc for fb[1] */
	igt_pipe_crc_start(pipe_crc);
	igt_pipe_crc_get_crcs(pipe_crc, 1, &crcs);
	data->ref_crc[1] = crcs[0];
	igt_pipe_crc_stop(pipe_crc);
	free(crcs);

	if (test_mode == TEST_CONTEXT || test_mode == TEST_PAGE_FLIP_AND_CONTEXT) {
		data->ctx[0] = drm_intel_gem_context_create(data->bufmgr);
		igt_assert(data->ctx[0]);
		data->ctx[1] = drm_intel_gem_context_create(data->bufmgr);
		igt_assert(data->ctx[1]);

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
	igt_plane_set_fb(data->primary, &data->fb[0]);
	igt_display_commit(display);
	usleep(300000);

	igt_assert(fbc_enabled(data));

	if (test_mode == TEST_CONTEXT || test_mode == TEST_PAGE_FLIP_AND_CONTEXT) {
		/*
		 * make ctx[0] FBC RT address point to fb[0], ctx[1]
		 * FBC RT address is left as disabled.
		 */
		exec_nop(data, data->fb[0].gem_handle, data->ctx[0]);
	}

	igt_wait_for_vblank(data->drm_fd, data->pipe);

	/* get reference crc for fb[0] */
	igt_pipe_crc_start(pipe_crc);
	igt_pipe_crc_get_crcs(pipe_crc, 1, &crcs);
	data->ref_crc[0] = crcs[0];
	igt_pipe_crc_stop(pipe_crc);
	free(crcs);

	return true;
}

static void finish_crtc(data_t *data, enum test_mode mode)
{
	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	if (mode == TEST_CONTEXT || mode == TEST_PAGE_FLIP_AND_CONTEXT) {
		drm_intel_gem_context_destroy(data->ctx[0]);
		drm_intel_gem_context_destroy(data->ctx[1]);
	}

	igt_plane_set_fb(data->primary, NULL);
	igt_output_set_pipe(data->output, PIPE_ANY);
	igt_display_commit(&data->display);

	igt_remove_fb(data->drm_fd, &data->fb[0]);
	igt_remove_fb(data->drm_fd, &data->fb[1]);
}

static void reset_display(data_t *data)
{
	igt_display_t *display = &data->display;

	for_each_connected_output(display, data->output) {
		if (data->output->valid) {
			data->primary =  igt_output_get_plane(data->output, IGT_PLANE_PRIMARY);
			igt_plane_set_fb(data->primary, NULL);
		}
		igt_output_set_pipe(data->output, PIPE_ANY);
	}
}

static void run_test(data_t *data, enum test_mode mode)
{
	igt_display_t *display = &data->display;
	int valid_tests = 0;

	if (mode == TEST_CONTEXT || mode == TEST_PAGE_FLIP_AND_CONTEXT) {
		drm_intel_context *ctx = drm_intel_gem_context_create(data->bufmgr);
		igt_require(ctx);
		drm_intel_gem_context_destroy(ctx);
	}

	reset_display(data);

	for_each_connected_output(display, data->output) {
		for (data->pipe = 0; data->pipe < igt_display_get_n_pipes(display); data->pipe++) {
			if (!prepare_crtc(data))
				continue;

			igt_info("Beginning %s on pipe %c, connector %s\n",
				 igt_subtest_name(), pipe_name(data->pipe),
				 igt_output_name(data->output));

			if (!prepare_test(data, mode)) {
				igt_info("%s on pipe %c, connector %s: SKIPPED\n",
					 igt_subtest_name(), pipe_name(data->pipe),
					 igt_output_name(data->output));
				continue;
			}

			valid_tests++;

			test_crc(data, mode);

			igt_info("%s on pipe %c, connector %s: PASSED\n",
				 igt_subtest_name(), pipe_name(data->pipe),
				 igt_output_name(data->output));

			finish_crtc(data, mode);
		}
	}

	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

igt_main
{
	data_t data = {};
	enum test_mode mode;

	igt_skip_on_simulation();

	igt_fixture {
		char buf[64];
		FILE *status;

		data.drm_fd = drm_open_any();
		igt_set_vt_graphics_mode();

		data.devid = intel_get_drm_devid(data.drm_fd);

		igt_require_pipe_crc();

		status = igt_debugfs_fopen("i915_fbc_status", "r");
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

		igt_display_init(&data.display, data.drm_fd);
	}

	for (mode = TEST_PAGE_FLIP; mode <= TEST_PAGE_FLIP_AND_CONTEXT; mode++) {
		igt_subtest_f("%s", test_mode_str(mode)) {
			run_test(&data, mode);
		}
	}

	igt_fixture {
		drm_intel_bufmgr_destroy(data.bufmgr);
		igt_display_fini(&data.display);
	}
}
