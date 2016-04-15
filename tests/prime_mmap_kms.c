/*
 * Copyright © 2016 Intel Corporation
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

/*
 * Testcase: show case dma-buf new API and processes restrictions. Most likely
 * you want to run like ./prime_mmap_kms --interactive-debug=paint, to see the
 * actual rectangle painted on the screen.
 */

#include "igt.h"

IGT_TEST_DESCRIPTION(
   "Efficiently sharing CPU and GPU buffers");

/*
 * render_process_t:
 *
 * Render is basically a user-space regular client. It's the unprivileged
 * process with limited system accesses.
 *
 * Worth note the vendor-independent characteristic, meaning that the
 * client doesn't need to perform any vendor specific calls for buffer
 * handling. Mesa GBM library is a counter-example because, even though its API
 * is vendor-independent, under-the-hood the library actually calls vendor
 * specific ioctls, which is not really sandboxable and not the goal here.
 */
typedef struct {
	int prime_fd;
	size_t size;
	int width;
	int height;
} render_process_t;

typedef struct {
	int x;
	int y;
	int w;
	int h;
} rect_t;

/* set ptr in a linear view */
static void set_pixel(void *_ptr, int index, uint32_t color, int bpp)
{
	if (bpp == 16) {
		uint16_t *ptr = _ptr;
		ptr[index] = color;
	} else if (bpp == 32) {
		uint32_t *ptr = _ptr;
		ptr[index] = color;
	} else {
		igt_assert_f(false, "bpp: %d\n", bpp);
	}
}

static void paint(render_process_t *render)
{
	void *frame;
	rect_t rect = {
		.x = 200,
		.y = 200,
		.w = render->width / 4,
		.h = render->height / 4,
	};
	uint32_t color = 0xFF;
	int stride, bpp;
	int x, y, line_begin;

	frame = mmap(NULL, render->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		     render->prime_fd, 0);
	igt_assert(frame != MAP_FAILED);

	/* TODO: what's the mmap'ed buffer semantics on tiling, format etc. How
	 * does the client know whether that the BO was created X-tiled,
	 * Y-tiled and how it will map back? This is something we need to
	 * address in this API still. */
	stride = render->width * 4;
	bpp = 32;

	/* ioctls to keep up the GPU <-> CPU coherency */
	prime_sync_start(render->prime_fd, true);

	/* the actual painting phase happens here */
	for (y = rect.y; y < rect.y + rect.h; y++) {
		line_begin = y * stride / (bpp / 8);
		for (x = rect.x; x < rect.x + rect.w; x++)
			set_pixel(frame, line_begin + x, color, bpp);
	}

	prime_sync_end(render->prime_fd, true);
	munmap(frame, render->size);
}

static void init_renderer(int prime_fd, int fb_size, int width, int height)
{
	render_process_t render;

	render.prime_fd = prime_fd;
	render.size = fb_size;
	render.width = width;
	render.height = height;
	paint(&render);
}

/*
 * gpu_process_t:
 *
 * GPU process is the privileged process and has access to the system graphics
 * routines, like DRM, display management and driver accesses.
 */
typedef struct {
	int drm_fd;
	igt_display_t display;
	struct igt_fb fb;
	igt_output_t *output;
	igt_plane_t *primary;
	enum pipe pipe;
} gpu_process_t;

static void cleanup_crtc(gpu_process_t *gpu)
{
	igt_display_t *display = &gpu->display;
	igt_output_t *output = gpu->output;

	igt_plane_set_fb(gpu->primary, NULL);

	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit(display);

	igt_remove_fb(gpu->drm_fd, &gpu->fb);
}

static void set_crtc(gpu_process_t *gpu)
{
	igt_display_t *display = &gpu->display;

	igt_display_commit(display);
}

static bool prepare_crtc(gpu_process_t *gpu)
{
	igt_display_t *display = &gpu->display;
	igt_output_t *output = gpu->output;
	drmModeModeInfo *mode;

	/* select the pipe we want to use */
	igt_output_set_pipe(output, gpu->pipe);
	igt_display_commit(display);

	if (!output->valid) {
		igt_output_set_pipe(output, PIPE_ANY);
		igt_display_commit(display);
		return false;
	}

	mode = igt_output_get_mode(output);

	/* create a white fb and flip to it */
	igt_create_color_fb(gpu->drm_fd, mode->hdisplay, mode->vdisplay,
			DRM_FORMAT_XRGB8888, LOCAL_DRM_FORMAT_MOD_NONE,
			1.0, 1.0, 1.0, &gpu->fb);

	gpu->primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);

	igt_plane_set_fb(gpu->primary, &gpu->fb);
	igt_display_commit(display);

	return true;
}

/*
 * The idea is to create a BO (in this case the framebuffer's) in one process,
 * export and pass its prime fd to another process, which in turn uses the fd
 * to map and write. This is Chrome-like architectures, where the Web content
 * (a "tab" or the "unprivileged process") maps and CPU-paints a buffer, which
 * was previously allocated in the GPU process ("privileged process").
 */
static void run_test(gpu_process_t *gpu)
{
	igt_display_t *display = &gpu->display;
	igt_output_t *output;
	enum pipe pipe;
	int prime_fd;

	for_each_connected_output(display, output) {
		gpu->output = output;
		for_each_pipe(display, pipe) {
			gpu->pipe = pipe;

			if (!prepare_crtc(gpu))
				continue;

			prime_fd = prime_handle_to_fd_for_mmap(gpu->drm_fd,
			                                       gpu->fb.gem_handle);
			igt_skip_on(prime_fd == -1 && errno == EINVAL);

			/* Note that it only shares the dma-buf fd and some
			 * other basic info */
			igt_fork(renderer_no, 1) {
				init_renderer(prime_fd, gpu->fb.size, gpu->fb.width,
					      gpu->fb.height);
			}
			igt_waitchildren();

			set_crtc(gpu);
			igt_debug_wait_for_keypress("paint");
			cleanup_crtc(gpu);

			/* once is enough */
			return;
		}
	}

	igt_skip("no valid crtc/connector combinations found\n");
}

igt_main
{
	gpu_process_t gpu;

	igt_skip_on_simulation();

	igt_fixture {
		gpu.drm_fd = drm_open_driver_master(DRIVER_INTEL);

		kmstest_set_vt_graphics_mode();

		igt_require_pipe_crc();

		igt_display_init(&gpu.display, gpu.drm_fd);
	}

	igt_subtest("buffer-sharing")
		run_test(&gpu);

	igt_fixture {
		igt_display_fini(&gpu.display);
		close(gpu.drm_fd);
	}

	igt_exit();
}
