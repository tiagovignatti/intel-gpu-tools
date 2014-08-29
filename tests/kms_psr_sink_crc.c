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

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_chipset.h"
#include "igt_debugfs.h"
#include "igt_kms.h"
#include "igt_aux.h"

bool running_with_psr_disabled;

enum planes {
	PRIMARY,
	SPRITE,
	CURSOR,
};

enum operations {
	PAGE_FLIP,
	MMAP_GTT,
	MMAP_GTT_WAITING,
	MMAP_CPU,
	BLT,
	RENDER,
	PLANE_MOVE,
	PLANE_ONOFF,
};

static const char *op_str(enum operations op)
{
	static const char * const name[] = {
		[PAGE_FLIP] = "page_flip",
		[MMAP_GTT] = "mmap_gtt",
		[MMAP_GTT_WAITING] = "mmap_gtt_waiting",
		[MMAP_CPU] = "mmap_cpu",
		[BLT] = "blt",
		[RENDER] = "render",
		[PLANE_MOVE] = "plane_move",
		[PLANE_ONOFF] = "plane_onoff",
	};

	return name[op];
}

typedef struct {
	int drm_fd;
	enum planes test_plane;
	enum operations op;
	uint32_t devid;
	uint32_t crtc_id;
	igt_display_t display;
	drm_intel_bufmgr *bufmgr;
	struct igt_fb fb_green, fb_white;
	igt_plane_t *primary, *sprite, *cursor;
} data_t;

static void create_cursor_fb(data_t *data)
{
	cairo_t *cr;
	uint32_t fb_id;

	fb_id = igt_create_fb(data->drm_fd, 64, 64,
			      DRM_FORMAT_ARGB8888, I915_TILING_NONE,
			      &data->fb_white);
	igt_assert(fb_id);

	cr = igt_get_cairo_ctx(data->drm_fd, &data->fb_white);
	igt_paint_color_alpha(cr, 0, 0, 64, 64, 1.0, 1.0, 1.0, 1.0);
	igt_assert(cairo_status(cr) == 0);
}

static void display_init(data_t *data)
{
	igt_display_init(&data->display, data->drm_fd);
}

static void display_fini(data_t *data)
{
	igt_display_fini(&data->display);
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
	uint32_t handle = data->fb_white.gem_handle;
	igt_plane_t *test_plane;
	void *ptr;
	char ref_crc[12];
	char crc[12];

	igt_plane_set_fb(data->primary, &data->fb_green);
	igt_display_commit(&data->display);

	/* Setting a secondary fb/plane */
	switch (data->test_plane) {
	case PRIMARY: default: test_plane = data->primary; break;
	case SPRITE: test_plane = data->sprite; break;
	case CURSOR: test_plane = data->cursor; break;
	}
	igt_plane_set_fb(test_plane, &data->fb_white);
	igt_display_commit(&data->display);

	igt_assert(wait_psr_entry(data, 10));
	get_sink_crc(data, ref_crc);

	switch (data->op) {
	case PAGE_FLIP:
		/* Only in use when testing primary plane */
		igt_assert(drmModePageFlip(data->drm_fd, data->crtc_id,
					   data->fb_green.fb_id, 0, NULL) == 0);
		break;
	case MMAP_GTT:
		ptr = gem_mmap__gtt(data->drm_fd, handle, 4096, PROT_WRITE);
		gem_set_domain(data->drm_fd, handle,
			       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
		memset(ptr, 0, 4);
		munmap(ptr, 4096);
		break;
	case MMAP_GTT_WAITING:
		ptr = gem_mmap__gtt(data->drm_fd, handle, 4096, PROT_WRITE);
		gem_set_domain(data->drm_fd, handle,
			       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);

		/* Printing white on white so the screen shouldn't change */
		memset(ptr, 0xff, 4);
		get_sink_crc(data, crc);
		igt_assert(strcmp(ref_crc, crc) == 0);

		fprintf(stdout, "Waiting 10s...\n");
		sleep(10);

		/* Now lets print black to change the screen */
		memset(ptr, 0, 4);
		munmap(ptr, 4096);
		break;
	case MMAP_CPU:
		ptr = gem_mmap__cpu(data->drm_fd, handle, 4096, PROT_WRITE);
		gem_set_domain(data->drm_fd, handle,
			       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
		memset(ptr, 0, 4);
		munmap(ptr, 4096);
		gem_sw_finish(data->drm_fd, handle);
		break;
	case BLT:
		fill_blt(data, handle, 0);
		break;
	case RENDER:
		fill_render(data, handle, 0);
		break;
	case PLANE_MOVE:
		/* Only in use when testing Sprite and Cursor */
		igt_plane_set_position(test_plane, 1, 1);
		igt_display_commit(&data->display);
		break;
	case PLANE_ONOFF:
		/* Only in use when testing Sprite and Cursor */
		igt_plane_set_fb(test_plane, NULL);
		igt_display_commit(&data->display);
		break;
	}
	get_sink_crc(data, crc);
	igt_assert(strcmp(ref_crc, crc) != 0);
}

static void test_cleanup(data_t *data) {
	igt_plane_set_fb(data->primary, NULL);
	if (data->test_plane == SPRITE)
		igt_plane_set_fb(data->sprite, NULL);
	if (data->test_plane == CURSOR)
		igt_plane_set_fb(data->cursor, NULL);

	igt_display_commit(&data->display);

	igt_remove_fb(data->drm_fd, &data->fb_green);
	igt_remove_fb(data->drm_fd, &data->fb_white);
}

static void run_test(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output;
	drmModeModeInfo *mode;
	uint32_t white_h, white_v;

	for_each_connected_output(display, output) {
		drmModeConnectorPtr c = output->config.connector;

		if (c->connector_type != DRM_MODE_CONNECTOR_eDP ||
		    c->connection != DRM_MODE_CONNECTED)
			continue;

		igt_output_set_pipe(output, PIPE_ANY);
		data->crtc_id = output->config.crtc->crtc_id;

		mode = igt_output_get_mode(output);

		igt_create_color_fb(data->drm_fd,
				    mode->hdisplay, mode->vdisplay,
				    DRM_FORMAT_XRGB8888, I915_TILING_X,
				    0.0, 1.0, 0.0,
				    &data->fb_green);

		data->primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
		igt_plane_set_fb(data->primary, NULL);

		white_h = mode->hdisplay;
		white_v = mode->vdisplay;

		switch (data->test_plane) {
		case SPRITE:
			data->sprite = igt_output_get_plane(output,
							    IGT_PLANE_2);
			igt_plane_set_fb(data->sprite, NULL);
			/* To make it different for human eyes let's make
			 * sprite visible in only one quarter of the primary
			 */
			white_h = white_h/2;
			white_v = white_v/2;
		case PRIMARY:
			igt_create_color_fb(data->drm_fd,
					    white_h, white_v,
					    DRM_FORMAT_XRGB8888, I915_TILING_X,
					    1.0, 1.0, 1.0,
					    &data->fb_white);
			break;
		case CURSOR:
			data->cursor = igt_output_get_plane(output,
							    IGT_PLANE_CURSOR);
			igt_plane_set_fb(data->cursor, NULL);
			create_cursor_fb(data);
			igt_plane_set_position(data->cursor, 0, 0);
			break;
		}

		igt_display_commit(&data->display);

		test_crc(data);

		test_cleanup(data);
	}
}

igt_main
{
	data_t data = {};
	enum operations op;
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

	for (op = PAGE_FLIP; op <= RENDER; op++) {
		igt_subtest_f("primary_%s", op_str(op)) {
			data.test_plane = PRIMARY;
			data.op = op;
			run_test(&data);
		}
	}

	for (op = MMAP_GTT; op <= PLANE_ONOFF; op++) {
		igt_subtest_f("sprite_%s", op_str(op)) {
			data.test_plane = SPRITE;
			data.op = op;
			run_test(&data);
		}
	}

	for (op = MMAP_GTT; op <= PLANE_ONOFF; op++) {
		igt_subtest_f("cursor_%s", op_str(op)) {
			data.test_plane = CURSOR;
			data.op = op;
			run_test(&data);
		}
	}

	igt_fixture {
		drm_intel_bufmgr_destroy(data.bufmgr);
		display_fini(&data);
	}
}
