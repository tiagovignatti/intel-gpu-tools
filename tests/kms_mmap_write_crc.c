/*
 * Copyright © 2015 Intel Corporation
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
 * Authors:
 *    Tiago Vignatti <tiago.vignatti at intel.com>
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"
#include "intel_chipset.h"
#include "ioctl_wrappers.h"
#include "igt_aux.h"

IGT_TEST_DESCRIPTION(
   "Use the display CRC support to validate mmap write to an already uncached future scanout buffer.");

#define ROUNDS 10

typedef struct {
	int drm_fd;
	igt_display_t display;
	struct igt_fb fb[2];
	igt_output_t *output;
	igt_plane_t *primary;
	enum pipe pipe;
	igt_crc_t ref_crc;
	igt_pipe_crc_t *pipe_crc;
	uint32_t devid;
} data_t;

static int ioctl_sync = true;
int dma_buf_fd;

static char *dmabuf_mmap_framebuffer(int drm_fd, struct igt_fb *fb)
{
	char *ptr = NULL;

	dma_buf_fd = prime_handle_to_fd_for_mmap(drm_fd, fb->gem_handle);
	igt_skip_on(dma_buf_fd == -1 && errno == EINVAL);

	ptr = mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, dma_buf_fd, 0);
	igt_assert(ptr != MAP_FAILED);

	return ptr;
}

static void test(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;
	struct igt_fb *fb = &data->fb[1];
	drmModeModeInfo *mode;
	cairo_t *cr;
	char *ptr;
	uint32_t caching;
	void *buf;
	igt_crc_t crc;

	mode = igt_output_get_mode(output);

	/* create a non-white fb where we can write later */
	igt_create_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
		      DRM_FORMAT_XRGB8888, LOCAL_DRM_FORMAT_MOD_NONE, fb);

	ptr = dmabuf_mmap_framebuffer(data->drm_fd, fb);

	cr = igt_get_cairo_ctx(data->drm_fd, fb);
	igt_paint_test_pattern(cr, fb->width, fb->height);
	cairo_destroy(cr);

	/* flip to it to make it UC/WC and fully flushed */
	igt_plane_set_fb(data->primary, fb);
	igt_display_commit(display);

	/* flip back the original white buffer */
	igt_plane_set_fb(data->primary, &data->fb[0]);
	igt_display_commit(display);

	/* make sure caching mode has become UC/WT */
	caching = gem_get_caching(data->drm_fd, fb->gem_handle);
	igt_assert(caching == I915_CACHING_NONE || caching == I915_CACHING_DISPLAY);

	/*
	 * firstly demonstrate the need for DMA_BUF_SYNC_START ("begin_cpu_access")
	 */
	if (ioctl_sync)
		prime_sync_start(dma_buf_fd, true);

	/* use dmabuf pointer to make the other fb all white too */
	buf = malloc(fb->size);
	igt_assert(buf != NULL);
	memset(buf, 0xff, fb->size);
	memcpy(ptr, buf, fb->size);
	free(buf);

	/* and flip to it */
	igt_plane_set_fb(data->primary, fb);
	igt_display_commit(display);

	/* check that the crc is as expected, which requires that caches got flushed */
	igt_pipe_crc_collect_crc(data->pipe_crc, &crc);
	igt_assert_crc_equal(&crc, &data->ref_crc);

	/*
	 * now demonstrates the need for DMA_BUF_SYNC_END ("end_cpu_access")
	 */

	/* start over, writing non-white to the fb again and flip to it to make it
	 * fully flushed */
	cr = igt_get_cairo_ctx(data->drm_fd, fb);
	igt_paint_test_pattern(cr, fb->width, fb->height);
	cairo_destroy(cr);

	igt_plane_set_fb(data->primary, fb);
	igt_display_commit(display);

	/* sync start, to move to CPU domain */
	if (ioctl_sync)
		prime_sync_start(dma_buf_fd, true);

	/* use dmabuf pointer in the same fb to make it all white */
	buf = malloc(fb->size);
	igt_assert(buf != NULL);
	memset(buf, 0xff, fb->size);
	memcpy(ptr, buf, fb->size);
	free(buf);

	/* if we don't change to the GTT domain again, the whites won't get flushed
	 * and therefore we demonstrates the need for sync end here */
	if (ioctl_sync)
		prime_sync_end(dma_buf_fd, true);

	/* check that the crc is as expected, which requires that caches got flushed */
	igt_pipe_crc_collect_crc(data->pipe_crc, &crc);
	igt_assert_crc_equal(&crc, &data->ref_crc);
}

static bool prepare_crtc(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;
	drmModeModeInfo *mode;

	/* select the pipe we want to use */
	igt_output_set_pipe(output, data->pipe);
	igt_display_commit(display);

	if (!output->valid) {
		igt_output_set_pipe(output, PIPE_ANY);
		igt_display_commit(display);
		return false;
	}

	mode = igt_output_get_mode(output);

	/* create a white reference fb and flip to it */
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888, LOCAL_DRM_FORMAT_MOD_NONE,
			    1.0, 1.0, 1.0, &data->fb[0]);

	data->primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);

	igt_plane_set_fb(data->primary, &data->fb[0]);
	igt_display_commit(display);

	if (data->pipe_crc)
		igt_pipe_crc_free(data->pipe_crc);

	data->pipe_crc = igt_pipe_crc_new(data->pipe,
					  INTEL_PIPE_CRC_SOURCE_AUTO);

	/* get reference crc for the white fb */
	igt_pipe_crc_collect_crc(data->pipe_crc, &data->ref_crc);

	return true;
}

static void cleanup_crtc(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	igt_plane_set_fb(data->primary, NULL);

	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit(display);

	igt_remove_fb(data->drm_fd, &data->fb[0]);
	igt_remove_fb(data->drm_fd, &data->fb[1]);
}

static void run_test(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output;
	enum pipe pipe;

	for_each_connected_output(display, output) {
		data->output = output;
		for_each_pipe(display, pipe) {
			data->pipe = pipe;

			if (!prepare_crtc(data))
				continue;

			test(data);
			cleanup_crtc(data);

			/* once is enough */
			return;
		}
	}

	igt_skip("no valid crtc/connector combinations found\n");
}

struct igt_helper_process hog;

/**
 * fork_cpuhog_helper:
 *
 * Fork a child process that loops indefinitely to consume CPU. This is used to
 * fill the CPU caches with random information so they can get stalled,
 * provoking incoherency with the GPU most likely.
 */
static void fork_cpuhog_helper(void)
{
	igt_fork_helper(&hog) {
		while (1) {
			usleep(10); /* quite ramdom really. */

			if ((int)getppid() == 1) /* Parent has died, so must we. */
				exit(0);
		}
	}
}

static int opt_handler(int opt, int opt_index, void *data)
{
	if (opt == 'n') {
		ioctl_sync = false;
		igt_info("set via cmd line to not use sync ioctls\n");
	}

	return 0;
}

static data_t data;

int main(int argc, char **argv)
{
	int i;
	igt_simple_init_parse_opts(&argc, argv, "n", NULL, NULL, opt_handler, NULL);

	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_driver_master(DRIVER_INTEL);

		data.devid = intel_get_drm_devid(data.drm_fd);

		kmstest_set_vt_graphics_mode();

		igt_require_pipe_crc();

		igt_display_init(&data.display, data.drm_fd);

		fork_cpuhog_helper();
	}

	igt_info("Using %d rounds for the test\n", ROUNDS);

	for (i = 0; i < ROUNDS; i++)
		run_test(&data);

	igt_fixture {
		igt_display_fini(&data.display);

		igt_stop_helper(&hog);
	}

	igt_exit();
}
