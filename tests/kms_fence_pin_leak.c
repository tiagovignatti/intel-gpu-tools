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

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"
#include "ioctl_wrappers.h"
#include "intel_chipset.h"

typedef struct {
	int drm_fd;
	uint32_t devid;
	drm_intel_bufmgr *bufmgr;
	igt_display_t display;
	drm_intel_bo *bos[64]; /* >= num fence registers */
} data_t;

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

	drm_intel_bo_unreference(dst);
}

static void alloc_fence_objs(data_t *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(data->bos); i++) {
		drm_intel_bo *bo;

		bo = drm_intel_bo_alloc(data->bufmgr, "fence bo", 4096, 4096);
		igt_assert(bo);
		gem_set_tiling(data->drm_fd, bo->handle, I915_TILING_X, 512);

		data->bos[i] = bo;
	}
}

static void touch_fences(data_t *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(data->bos); i++) {
		uint32_t handle = data->bos[i]->handle;
		void *ptr;

		ptr = gem_mmap__gtt(data->drm_fd, handle, 4096, PROT_WRITE);
		gem_set_domain(data->drm_fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
		memset(ptr, 0, 4);
		munmap(ptr, 4096);
	}
}

static void free_fence_objs(data_t *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(data->bos); i++)
		drm_intel_bo_unreference(data->bos[i]);
}

static bool run_single_test(data_t *data, enum pipe pipe, igt_output_t *output)
{
	igt_display_t *display = &data->display;
	drmModeModeInfo *mode;
	igt_plane_t *primary;
	struct igt_fb fb[2];
	int i;

	igt_output_set_pipe(output, pipe);
	igt_display_commit(display);

	if (!output->valid) {
		igt_output_set_pipe(output, PIPE_ANY);
		igt_display_commit(display);
		return false;
	}

	mode = igt_output_get_mode(output);
	primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);

	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888,
			    true, /* need a fence so must be tiled */
			    0.0, 0.0, 0.0,
			    &fb[0]);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888,
			    true, /* need a fence so must be tiled */
			    0.0, 0.0, 0.0,
			    &fb[1]);

	igt_plane_set_fb(primary, &fb[0]);
	igt_display_commit(display);

	for (i = 0; i < 64; i++) {
		drm_intel_context *ctx;

		/*
		 * Link fb.gem_handle to the ppgtt vm of ctx so that the context
		 * destruction will unbind the obj from the ppgtt vm in question.
		 */
		ctx = drm_intel_gem_context_create(data->bufmgr);
		igt_assert(ctx);
		exec_nop(data, fb[i&1].gem_handle, ctx);
		drm_intel_gem_context_destroy(ctx);

		/* Force a context switch to make sure ctx gets destroyed for real. */
		exec_nop(data, fb[i&1].gem_handle, NULL);

		gem_sync(data->drm_fd, fb[i&1].gem_handle);

		/*
		 * Make only the current fb has a fence and
		 * the next fb will pick a new fence. Assuming
		 * all fences are associated with an object, the
		 * kernel will always pick a fence with pin_count==0.
		 */
		touch_fences(data);

		/*
		 * Pin the new buffer and unpin the old buffer from display. If
		 * the kernel is buggy the ppgtt unbind will have dropped the
		 * fence for the old buffer, and now the display code will try
		 * to unpin only to find no fence there. So the pin_count will leak.
		 */
		igt_plane_set_fb(primary, &fb[!(i&1)]);
		igt_display_commit(display);

		printf(".");
		fflush(stdout);
	}

	igt_plane_set_fb(primary, NULL);
	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit(display);

	igt_remove_fb(data->drm_fd, &fb[1]);
	igt_remove_fb(data->drm_fd, &fb[0]);

	printf("\n");

	return true;
}

static void run_test(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output;
	enum pipe p;

	for_each_connected_output(display, output) {
		for (p = 0; p < igt_display_get_n_pipes(display); p++) {
			if (run_single_test(data, p, output))
				return; /* one time ought to be enough */
		}
	}

	igt_skip("no valid crtc/connector combinations found\n");
}

igt_simple_main
{
	drm_intel_context *ctx;
	data_t data = {};

	igt_skip_on_simulation();

	data.drm_fd = drm_open_any();

	data.devid = intel_get_drm_devid(data.drm_fd);

	igt_set_vt_graphics_mode();

	data.bufmgr = drm_intel_bufmgr_gem_init(data.drm_fd, 4096);
	igt_assert(data.bufmgr);
	drm_intel_bufmgr_gem_enable_reuse(data.bufmgr);

	igt_display_init(&data.display, data.drm_fd);

	ctx = drm_intel_gem_context_create(data.bufmgr);
	igt_require(ctx);
	drm_intel_gem_context_destroy(ctx);

	alloc_fence_objs(&data);

	run_test(&data);

	free_fence_objs(&data);

	drm_intel_bufmgr_destroy(data.bufmgr);
	igt_display_fini(&data.display);
}
