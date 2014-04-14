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
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"
#include "intel_chipset.h"
#include "ioctl_wrappers.h"

typedef struct {
	int drm_fd;
	igt_display_t display;
	igt_pipe_crc_t *pipe_crc;
	drm_intel_bufmgr *bufmgr;
	drm_intel_bo *busy_bo;
	uint32_t devid;
	bool flip_done;
} data_t;

static void exec_nop(data_t *data, uint32_t handle, unsigned int ring)
{
	struct intel_batchbuffer *batch;
	drm_intel_bo *bo;

	batch = intel_batchbuffer_alloc(data->bufmgr, data->devid);
	igt_assert(batch);

	bo = gem_handle_to_libdrm_bo(data->bufmgr, data->drm_fd, "", handle);
	igt_assert(bo);

	/* add relocs to make sure the kernel will think we write to dst */
	BEGIN_BATCH(4);
	OUT_BATCH(MI_BATCH_BUFFER_END);
	OUT_BATCH(MI_NOOP);
	OUT_RELOC(bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(MI_NOOP);
	ADVANCE_BATCH();

	intel_batchbuffer_flush_on_ring(batch, ring);
	intel_batchbuffer_free(batch);

	drm_intel_bo_unreference(bo);
}

static void exec_blt(data_t *data)
{
	struct intel_batchbuffer *batch;
	int w, h, pitch, i;

	batch = intel_batchbuffer_alloc(data->bufmgr, data->devid);
	igt_assert(batch);

	w = 8192;
	h = data->busy_bo->size / (8192 * 4);
	pitch = w * 4;

	for (i = 0; i < 40; i++) {
		BLIT_COPY_BATCH_START(data->devid, 0);
		OUT_BATCH((3 << 24) | /* 32 bits */
			  (0xcc << 16) | /* copy ROP */
			  pitch);
		OUT_BATCH(0 << 16 | 0);
		OUT_BATCH(h << 16 | w);
		OUT_RELOC(data->busy_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
		BLIT_RELOC_UDW(data->devid);
		OUT_BATCH(0 << 16 | 0);
		OUT_BATCH(pitch);
		OUT_RELOC(data->busy_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
		BLIT_RELOC_UDW(data->devid);
		ADVANCE_BATCH();
	}

	intel_batchbuffer_flush(batch);
	intel_batchbuffer_free(batch);
}

static void page_flip_handler(int fd, unsigned int frame, unsigned int sec,
			      unsigned int usec, void *_data)
{
	data_t *data = _data;

	data->flip_done = true;
}

static void wait_for_flip(data_t *data, uint32_t flip_handle)
{
	struct timeval timeout = {
		.tv_sec = 3,
		.tv_usec = 0,
	};
	drmEventContext evctx = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.page_flip_handler = page_flip_handler,
	};
	fd_set fds;

	FD_ZERO(&fds);
	FD_SET(data->drm_fd, &fds);

	while (!data->flip_done) {
		int ret = select(data->drm_fd + 1, &fds, NULL, NULL, &timeout);

		if (ret < 0 && errno == EINTR)
			continue;

		igt_assert(ret >= 0);

		do_or_die(drmHandleEvent(data->drm_fd, &evctx));
	}

	/*
	 * The flip completion may have been signalled prematurely, so
	 * also submit another nop batch and wait for it to make sure
	 * the ring has really been drained.
	 */
	if (IS_GEN7(data->devid) || IS_GEN8(data->devid))
		exec_nop(data, flip_handle, I915_EXEC_BLT);
	else
		exec_nop(data, flip_handle, I915_EXEC_RENDER);
	gem_sync(data->drm_fd, flip_handle);
}

static void make_gpu_busy(data_t *data, uint32_t flip_handle)
{
	/*
	 * Make sure flip_handle has been used on the blt ring.
	 * This should make the flip use the same ring on gen7+.
	 */
	if (IS_GEN7(data->devid) || IS_GEN8(data->devid))
		exec_nop(data, flip_handle, I915_EXEC_BLT);

	/*
	 * Add a pile commands to the ring.  The flip will be
	 * stuck behing these commands and hence gets delayed
	 * significantly.
	 */
	exec_blt(data);

	/*
	 * Make sure the render ring will block until the blt ring is clear.
	 * This is in case the flip will execute on the render ring and the
	 * blits were on the blt ring (this will be the case on gen6 at least).
	 *
	 * We can't add an explicit dependency between flip_handle and the
	 * blits since that would cause the driver to block until the blits
	 * have completed before it will perform a subsequent mmio flip,
	 * and so the test would fail to exercise the mmio vs. CS flip race.
	 */
	if (HAS_BLT_RING(data->devid))
		exec_nop(data, data->busy_bo->handle, I915_EXEC_RENDER);
}

/*
 * 1. set primary plane to full red
 * 2. grab a reference crc
 * 3. set primary plane to full blue
 * 4. queue lots of GPU activity to delay the subsequent page flip
 * 5. queue a page flip to the same blue fb
 * 6. toggle a fullscreen sprite (green) on and back off again
 * 7. set primary plane to red fb
 * 8. wait for GPU to finish
 * 9. compare current crc with reference crc
 *
 * We expect the primary plane to display full red at the end.
 * If the sprite operations have interfered with the page flip,
 * the driver may have mistakenly completed the flip before
 * it was executed by the CS, and hence the subsequent mmio
 * flips may have overtaken it. So once we've finished everything
 * the CS flip may have been the last thing to occur, which means
 * the primary plane may be full blue instead of the red it's
 * supposed to be.
 */
static bool
test_plane(data_t *data, igt_output_t *output, enum pipe pipe, enum igt_plane plane)
{
	struct igt_fb red_fb, green_fb, blue_fb;
	drmModeModeInfo *mode;
	igt_plane_t *primary, *sprite;
	igt_crc_t ref_crc, crc;
	int ret;

	igt_output_set_pipe(output, pipe);
	igt_display_commit(&data->display);

	if (!output->valid) {
		igt_output_set_pipe(output, PIPE_ANY);
		igt_display_commit(&data->display);
		return false;
	}

	primary = igt_output_get_plane(output, 0);
	sprite = igt_output_get_plane(output, plane);

	mode = igt_output_get_mode(output);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888,
			    false, /* tiled */
			    1.0, 0.0, 0.0,
			    &red_fb);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888,
			    false, /* tiled */
			    0.0, 1.0, 0.0,
			    &green_fb);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888,
			    false, /* tiled */
			    0.0, 0.0, 1.0,
			    &blue_fb);

	/*
	 * Make sure these buffers are suited for display use
	 * because most of the modeset operations must be fast
	 * later on.
	 */
	igt_plane_set_fb(primary, &blue_fb);
	igt_display_commit(&data->display);
	igt_plane_set_fb(sprite, &green_fb);
	igt_display_commit(&data->display);
	igt_plane_set_fb(sprite, NULL);
	igt_display_commit(&data->display);

	if (data->pipe_crc)
		igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = igt_pipe_crc_new(pipe, INTEL_PIPE_CRC_SOURCE_AUTO);
	if (!data->pipe_crc) {
		igt_info("auto crc not supported on this connector with crtc %i\n",
			 pipe);

		igt_plane_set_fb(primary, NULL);
		igt_plane_set_fb(sprite, NULL);
		igt_output_set_pipe(output, PIPE_ANY);
		igt_display_commit(&data->display);

		igt_remove_fb(data->drm_fd, &red_fb);
		igt_remove_fb(data->drm_fd, &green_fb);
		igt_remove_fb(data->drm_fd, &blue_fb);

		return false;
	}

	/* set red fb and grab reference crc */
	igt_plane_set_fb(primary, &red_fb);
	igt_display_commit(&data->display);
	igt_pipe_crc_collect_crc(data->pipe_crc, &ref_crc);

	ret = drmModeSetCrtc(data->drm_fd, output->config.crtc->crtc_id,
			     blue_fb.fb_id, 0, 0, &output->id, 1,
			     mode);
	igt_assert(ret == 0);

	make_gpu_busy(data, blue_fb.gem_handle);

	data->flip_done = false;
	ret = drmModePageFlip(data->drm_fd, output->config.crtc->crtc_id,
			      blue_fb.fb_id, DRM_MODE_PAGE_FLIP_EVENT, data);
	igt_assert(ret == 0);

	/*
	 * Toggle a fullscreen sprite on and back off. This will result
	 * in the primary plane getting disabled and re-enbled, and that
	 * leads to mmio flips. The driver may then mistake the flip done
	 * interrupts from the mmio flips as the flip done interrupts for
	 * the CS flip, and hence subsequent mmio flips won't wait for the
	 * CS flips like they should.
	 */
	ret = drmModeSetPlane(data->drm_fd,
			      sprite->drm_plane->plane_id,
			      output->config.crtc->crtc_id,
			      green_fb.fb_id, 0,
			      0, 0, mode->hdisplay, mode->vdisplay,
			      0, 0, mode->hdisplay << 16, mode->vdisplay << 16);
	igt_assert(ret == 0);
	ret = drmModeSetPlane(data->drm_fd,
			      sprite->drm_plane->plane_id,
			      output->config.crtc->crtc_id,
			      0, 0,
			      0, 0, 0, 0,
			      0, 0, 0, 0);
	igt_assert(ret == 0);

	/*
	 * Set primary plane to red fb. This should wait for the CS flip
	 * to complete. But if the kernel mistook the flip done interrupt
	 * from the mmio flip as the flip done from the CS flip, this will
	 * not wait for anything. And hence the the CS flip will actually
	 * occur after this mmio flip.
	 */
	ret = drmModeSetCrtc(data->drm_fd, output->config.crtc->crtc_id,
			     red_fb.fb_id, 0, 0, &output->id, 1,
			     mode);
	igt_assert(ret == 0);

	/* Make sure the flip has been executed */
	wait_for_flip(data, blue_fb.gem_handle);

	/* Grab crc and compare with the extected result */
	igt_pipe_crc_collect_crc(data->pipe_crc, &crc);

	igt_plane_set_fb(primary, NULL);
	igt_display_commit(&data->display);

	igt_remove_fb(data->drm_fd, &red_fb);
	igt_remove_fb(data->drm_fd, &green_fb);
	igt_remove_fb(data->drm_fd, &blue_fb);

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit(&data->display);

	igt_assert(igt_crc_equal(&ref_crc, &crc));

	return true;
}

/*
 * 1. set primary plane to full red
 * 2. grab a reference crc
 * 3. set primary plane to full green
 * 4. wait for vblank
 * 5. pan primary plane a bit (to cause a mmio flip w/o vblank wait)
 * 6. queue lots of GPU activity to delay the subsequent page flip
 * 6. queue a page flip to a blue fb
 * 7. set primary plane to red fb
 * 8. wait for GPU to finish
 * 9. compare current crc with reference crc
 *
 * We expect the primary plane to display full red at the end.
 * If the previously schedule primary plane pan operation has interfered
 * with the following page flip, the driver may have mistakenly completed
 * the flip before it was executed by the CS, and hence the subsequent mmio
 * flips may have overtaken it. So once we've finished everything
 * the CS flip may have been the last thing to occur, which means
 * the primary plane may be full blue instead of the red it's
 * supposed to be.
 */
static bool
test_crtc(data_t *data, igt_output_t *output, enum pipe pipe)
{
	struct igt_fb red_fb, green_fb, blue_fb;
	drmModeModeInfo *mode;
	igt_plane_t *primary;
	igt_crc_t ref_crc, crc;
	int ret;

	igt_output_set_pipe(output, pipe);
	igt_display_commit(&data->display);

	if (!output->valid) {
		igt_output_set_pipe(output, PIPE_ANY);
		igt_display_commit(&data->display);
		return false;
	}

	primary = igt_output_get_plane(output, 0);

	mode = igt_output_get_mode(output);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay+1,
			    DRM_FORMAT_XRGB8888,
			    false, /* tiled */
			    1.0, 0.0, 0.0,
			    &red_fb);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay+1,
			    DRM_FORMAT_XRGB8888,
			    false, /* tiled */
			    0.0, 0.0, 1.0,
			    &blue_fb);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay+1,
			    DRM_FORMAT_XRGB8888,
			    false, /* tiled */
			    0.0, 1.0, 0.0,
			    &green_fb);

	/*
	 * Make sure these buffers are suited for display use
	 * because most of the modeset operations must be fast
	 * later on.
	 */
	igt_plane_set_fb(primary, &green_fb);
	igt_display_commit(&data->display);
	igt_plane_set_fb(primary, &blue_fb);
	igt_display_commit(&data->display);

	if (data->pipe_crc)
		igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = igt_pipe_crc_new(pipe, INTEL_PIPE_CRC_SOURCE_AUTO);
	if (!data->pipe_crc) {
		igt_info("auto crc not supported on this connector with crtc %i\n",
			 pipe);

		igt_plane_set_fb(primary, NULL);
		igt_output_set_pipe(output, PIPE_ANY);
		igt_display_commit(&data->display);

		igt_remove_fb(data->drm_fd, &red_fb);
		igt_remove_fb(data->drm_fd, &green_fb);
		igt_remove_fb(data->drm_fd, &blue_fb);

		return false;
	}

	/* set red fb and grab reference crc */
	igt_plane_set_fb(primary, &red_fb);
	igt_display_commit(&data->display);
	igt_pipe_crc_collect_crc(data->pipe_crc, &ref_crc);

	/*
	 * Further down we need to issue an mmio flip w/o the kernel
	 * waiting for vblank. The easiest way is to just pan within
	 * the same FB. So pan away a bit here, and later we undo this
	 * with another pan which will result in the desired mmio flip.
	 */
	ret = drmModeSetCrtc(data->drm_fd, output->config.crtc->crtc_id,
			     green_fb.fb_id, 0, 1, &output->id, 1,
			     mode);
	igt_assert(ret == 0);

	/*
	 * Make it more likely that the CS flip has been submitted into the
	 * ring by the time the mmio flip from the drmModeSetCrtc() below
	 * completes. The driver will then mistake the flip done interrupt
	 * from the mmio flip as the flip done interrupt from the CS flip.
	 */
	igt_wait_for_vblank(data->drm_fd, pipe);

	/* now issue the mmio flip w/o vblank waits in the kernel, ie. pan a bit */
	ret = drmModeSetCrtc(data->drm_fd, output->config.crtc->crtc_id,
			     green_fb.fb_id, 0, 0, &output->id, 1,
			     mode);
	igt_assert(ret == 0);

	make_gpu_busy(data, blue_fb.gem_handle);

	/*
	 * Submit the CS flip. The commands must be emitted into the ring
	 * before the mmio flip from the panning operation completes.
	 */
	data->flip_done = false;
	ret = drmModePageFlip(data->drm_fd, output->config.crtc->crtc_id,
			      blue_fb.fb_id, DRM_MODE_PAGE_FLIP_EVENT, data);
	igt_assert(ret == 0);

	/*
	 * Set primary plane to red fb. This should wait for the CS flip
	 * to complete. But if the kernel mistook the flip done interrupt
	 * from the mmio flip as the flip done from the CS flip, this will
	 * not wait for anything. And hence the the CS flip will actually
	 * occur after this mmio flip.
	 */
	ret = drmModeSetCrtc(data->drm_fd, output->config.crtc->crtc_id,
			     red_fb.fb_id, 0, 0, &output->id, 1,
			     mode);
	igt_assert(ret == 0);

	/* Make sure the flip has been executed */
	wait_for_flip(data, blue_fb.gem_handle);

	/* Grab crc and compare with the extected result */
	igt_pipe_crc_collect_crc(data->pipe_crc, &crc);

	igt_plane_set_fb(primary, NULL);
	igt_display_commit(&data->display);

	igt_remove_fb(data->drm_fd, &red_fb);
	igt_remove_fb(data->drm_fd, &green_fb);
	igt_remove_fb(data->drm_fd, &blue_fb);

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit(&data->display);

	igt_assert(igt_crc_equal(&ref_crc, &crc));

	return true;
}

static void
run_plane_test_for_pipe(data_t *data, enum pipe pipe)
{
	igt_output_t *output;
	enum igt_plane plane = 1; /* testing with one sprite is enough */
	int valid_tests = 0;

	igt_require(data->display.pipes[pipe].n_planes > 2);

	for_each_connected_output(&data->display, output) {
		if (test_plane(data, output, pipe, plane))
			valid_tests++;
	}

	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

static void
run_crtc_test_for_pipe(data_t *data, enum pipe pipe)
{
	igt_output_t *output;
	int valid_tests = 0;

	for_each_connected_output(&data->display, output) {
		if (test_crtc(data, output, pipe))
			valid_tests++;
	}

	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

static data_t data;

igt_main
{
	int pipe;

	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_any();

		igt_set_vt_graphics_mode();

		data.devid = intel_get_drm_devid(data.drm_fd);

		igt_require_pipe_crc();
		igt_display_init(&data.display, data.drm_fd);

		data.bufmgr = drm_intel_bufmgr_gem_init(data.drm_fd, 4096);
		igt_assert(data.bufmgr);
		drm_intel_bufmgr_gem_enable_reuse(data.bufmgr);

		data.busy_bo = drm_intel_bo_alloc(data.bufmgr, "bo",
						  64*1024*1024, 4096);
		gem_set_tiling(data.drm_fd, data.busy_bo->handle, 0, 4096);
	}

	igt_subtest_f("setplane_vs_cs_flip") {
		for (pipe = 0; pipe < data.display.n_pipes; pipe++)
			run_plane_test_for_pipe(&data, pipe);
	}

	igt_subtest_f("setcrtc_vs_cs_flip") {
		for (pipe = 0; pipe < data.display.n_pipes; pipe++)
			run_crtc_test_for_pipe(&data, pipe);
	}

	igt_fixture {
		drm_intel_bo_unreference(data.busy_bo);
		drm_intel_bufmgr_destroy(data.bufmgr);
		igt_display_fini(&data.display);
	}
}
