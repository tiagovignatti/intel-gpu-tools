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

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"

typedef struct {
	int drm_fd;
	igt_display_t display;
} data_t;

typedef struct {
	data_t *data;
	igt_pipe_crc_t *pipe_crc;
	igt_crc_t crc_1, crc_2, crc_3, crc_4, crc_5, crc_6, crc_7, crc_8,
		  crc_9, crc_10;
	struct igt_fb red_fb, blue_fb, black_fb, yellow_fb;
	drmModeModeInfo *mode;
} functional_test_t;

typedef struct {
	data_t *data;
	drmModeResPtr moderes;
	struct igt_fb blue_fb, oversized_fb, undersized_fb;
} sanity_test_t;

typedef struct {
	data_t *data;
	struct igt_fb red_fb, blue_fb;
} pageflip_test_t;

static void
functional_test_init(functional_test_t *test, igt_output_t *output, enum pipe pipe)
{
	data_t *data = test->data;
	drmModeModeInfo *mode;

	test->pipe_crc = igt_pipe_crc_new(pipe, INTEL_PIPE_CRC_SOURCE_AUTO);
	if (!test->pipe_crc)
		igt_skip("auto crc not supported on this connector with pipe %i\n",
		       pipe);


	igt_output_set_pipe(output, pipe);

	mode = igt_output_get_mode(output);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
				DRM_FORMAT_XRGB8888,
				false, /* tiled */
				0.0, 0.0, 0.0,
				&test->black_fb);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
				DRM_FORMAT_XRGB8888,
				false, /* tiled */
				0.0, 0.0, 1.0,
				&test->blue_fb);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
				DRM_FORMAT_XRGB8888,
				false, /* tiled */
				1.0, 1.0, 0.0,
				&test->yellow_fb);
	igt_create_color_fb(data->drm_fd, 100, 100,
				DRM_FORMAT_XRGB8888,
				false, /* tiled */
				1.0, 0.0, 0.0,
				&test->red_fb);

	test->mode = mode;
}

static void
functional_test_fini(functional_test_t *test, igt_output_t *output)
{
	igt_pipe_crc_free(test->pipe_crc);

	igt_remove_fb(test->data->drm_fd, &test->black_fb);
	igt_remove_fb(test->data->drm_fd, &test->blue_fb);
	igt_remove_fb(test->data->drm_fd, &test->red_fb);
	igt_remove_fb(test->data->drm_fd, &test->yellow_fb);

	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit2(&test->data->display, COMMIT_LEGACY);
}

/*
 * Universal plane functional testing.
 *   - Black primary plane via traditional interfaces, red sprite, grab CRC:1.
 *   - Blue primary plane via traditional interfaces, red sprite, grab CRC:2.
 *   - Yellow primary via traditional interfaces
 *   - Blue primary plane, red sprite via universal planes, grab CRC:3 and compare
 *     with CRC:2 (should be the same)
 *   - Disable primary plane, grab CRC:4 (should be same as CRC:1)
 *   - Reenable primary, grab CRC:5 (should be same as CRC:2 and CRC:3)
 *   - Yellow primary, no sprite
 *   - Disable CRTC
 *   - Program red sprite (while CRTC off)
 *   - Program blue primary (while CRTC off)
 *   - Enable CRTC, grab CRC:6 (should be same as CRC:2)
 */
static void
functional_test_pipe(data_t *data, enum pipe pipe, igt_output_t *output)
{
	functional_test_t test = { .data = data };
	igt_display_t *display = &data->display;
	igt_plane_t *primary, *sprite;
	int ret;
	int num_primary = 0, num_cursor = 0;
	int i;

	igt_skip_on(pipe >= display->n_pipes);

	fprintf(stdout, "Testing connector %s using pipe %c\n",
		igt_output_name(output), pipe_name(pipe));

	functional_test_init(&test, output, pipe);

	/*
	 * Make sure we have no more than one primary or cursor plane per crtc.
	 * If the kernel accidentally calls drm_plane_init() rather than
	 * drm_universal_plane_init(), the type enum can get interpreted as a
	 * boolean and show up in userspace as the wrong type.
	 */
	for (i = 0; i < display->pipes[pipe].n_planes; i++)
		if (display->pipes[pipe].planes[i].is_primary)
			num_primary++;
		else if (display->pipes[pipe].planes[i].is_cursor)
			num_cursor++;

	igt_assert(num_primary == 1);
	igt_assert(num_cursor <= 1);

	primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);
	sprite = igt_output_get_plane(output, IGT_PLANE_2);
	if (!sprite) {
		functional_test_fini(&test, output);
		igt_skip("No sprite plane available\n");
	}

	igt_plane_set_position(sprite, 100, 100);

	/* Step 1: Legacy API's, black primary, red sprite (CRC 1) */
	igt_plane_set_fb(primary, &test.black_fb);
	igt_plane_set_fb(sprite, &test.red_fb);
	igt_display_commit2(display, COMMIT_LEGACY);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_1);

	/* Step 2: Legacy API', blue primary, red sprite (CRC 2) */
	igt_plane_set_fb(primary, &test.blue_fb);
	igt_plane_set_fb(sprite, &test.red_fb);
	igt_display_commit2(display, COMMIT_LEGACY);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_2);

	/* Step 3: Legacy API's, yellow primary (CRC 3) */
	igt_plane_set_fb(primary, &test.yellow_fb);
	igt_display_commit2(display, COMMIT_LEGACY);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_3);

	/* Step 4: Universal API's, blue primary, red sprite (CRC 4) */
	igt_plane_set_fb(primary, &test.blue_fb);
	igt_plane_set_fb(sprite, &test.red_fb);
	igt_display_commit2(display, COMMIT_UNIVERSAL);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_4);

	/* Step 5: Universal API's, disable primary plane (CRC 5) */
	igt_plane_set_fb(primary, NULL);
	igt_display_commit2(display, COMMIT_UNIVERSAL);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_5);

	/* Step 6: Universal API's, re-enable primary with blue (CRC 6) */
	igt_plane_set_fb(primary, &test.blue_fb);
	igt_display_commit2(display, COMMIT_UNIVERSAL);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_6);

	/* Step 7: Legacy API's, yellow primary, no sprite */
	igt_plane_set_fb(primary, &test.yellow_fb);
	igt_plane_set_fb(sprite, NULL);
	igt_display_commit2(display, COMMIT_LEGACY);

	/* Step 8: Disable CRTC */
	igt_plane_set_fb(primary, NULL);
	igt_display_commit2(display, COMMIT_LEGACY);

	/* Step 9: Universal API's with crtc off:
	 *  - red sprite
	 *  - multiple primary fb's, ending in blue
	 */
	igt_plane_set_fb(sprite, &test.red_fb);
	igt_display_commit2(display, COMMIT_UNIVERSAL);
	igt_plane_set_fb(primary, &test.yellow_fb);
	igt_display_commit2(display, COMMIT_UNIVERSAL);
	igt_plane_set_fb(primary, &test.black_fb);
	igt_display_commit2(display, COMMIT_UNIVERSAL);
	igt_plane_set_fb(primary, &test.blue_fb);
	igt_display_commit2(display, COMMIT_UNIVERSAL);

	/* Step 10: Enable crtc (fb = -1), take CRC (CRC 7) */
	ret = drmModeSetCrtc(data->drm_fd, output->config.crtc->crtc_id, -1,
			     0, 0, &output->config.connector->connector_id,
			     1, test.mode);
	igt_assert(ret == 0);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_7);

	/* Step 11: Disable primary plane */
	igt_plane_set_fb(primary, NULL);
	igt_display_commit2(display, COMMIT_UNIVERSAL);

	/* Step 12: Legacy modeset to yellow FB (CRC 8) */
	igt_plane_set_fb(primary, &test.yellow_fb);
	igt_display_commit2(display, COMMIT_LEGACY);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_8);

	/* Step 13: Legacy API', blue primary, red sprite */
	igt_plane_set_fb(primary, &test.blue_fb);
	igt_plane_set_fb(sprite, &test.red_fb);
	igt_display_commit2(display, COMMIT_LEGACY);

	/* Step 14: Universal API, set primary completely offscreen (CRC 9) */
	ret = drmModeSetPlane(data->drm_fd, primary->drm_plane->plane_id,
			      output->config.crtc->crtc_id,
			      test.blue_fb.fb_id, 0,
			      9000, 9000,
			      test.mode->hdisplay,
			      test.mode->vdisplay,
			      IGT_FIXED(0,0), IGT_FIXED(0,0),
			      IGT_FIXED(test.mode->hdisplay,0),
			      IGT_FIXED(test.mode->vdisplay,0));
	igt_assert(ret == 0);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_9);

	/*
	 * Step 15: Explicitly disable primary after it's already been
	 * implicitly disabled (CRC 10).
	 */
	igt_plane_set_fb(primary, NULL);
	igt_display_commit2(display, COMMIT_UNIVERSAL);
	igt_pipe_crc_collect_crc(test.pipe_crc, &test.crc_10);

	/* Step 16: Legacy API's, blue primary, red sprite */
	igt_plane_set_fb(primary, &test.blue_fb);
	igt_plane_set_fb(sprite, &test.red_fb);
	igt_display_commit2(display, COMMIT_LEGACY);

	/* Blue bg + red sprite should be same under both types of API's */
	igt_assert(igt_crc_equal(&test.crc_2, &test.crc_4));

	/* Disabling primary plane should be same as black primary */
	igt_assert(igt_crc_equal(&test.crc_1, &test.crc_5));

	/* Re-enabling primary should return to blue properly */
	igt_assert(igt_crc_equal(&test.crc_2, &test.crc_6));

	/*
	 * We should be able to setup plane FB's while CRTC is disabled and
	 * then have them pop up correctly when the CRTC is re-enabled.
	 */
	igt_assert(igt_crc_equal(&test.crc_2, &test.crc_7));

	/*
	 * We should be able to modeset with the primary plane off
	 * successfully
	 */
	igt_assert(igt_crc_equal(&test.crc_3, &test.crc_8));

	/*
	 * We should be able to move the primary plane completely offscreen
	 * and have it disable successfully.
	 */
	igt_assert(igt_crc_equal(&test.crc_5, &test.crc_9));

	/*
	 * We should be able to explicitly disable an already
	 * implicitly-disabled primary plane
	 */
	igt_assert(igt_crc_equal(&test.crc_5, &test.crc_10));

	igt_plane_set_fb(primary, NULL);
	igt_plane_set_fb(sprite, NULL);

	functional_test_fini(&test, output);
}

static void
sanity_test_init(sanity_test_t *test, igt_output_t *output, enum pipe pipe)
{
	data_t *data = test->data;
	drmModeModeInfo *mode;

	igt_output_set_pipe(output, pipe);

	mode = igt_output_get_mode(output);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888,
			    false, /* tiled */
			    0.0, 0.0, 1.0,
			    &test->blue_fb);
	igt_create_color_fb(data->drm_fd,
			    mode->hdisplay + 100, mode->vdisplay + 100,
			    DRM_FORMAT_XRGB8888,
			    false, /* tiled */
			    0.0, 0.0, 1.0,
			    &test->oversized_fb);
	igt_create_color_fb(data->drm_fd,
			    mode->hdisplay - 100, mode->vdisplay - 100,
			    DRM_FORMAT_XRGB8888,
			    false, /* tiled */
			    0.0, 0.0, 1.0,
			    &test->undersized_fb);

	test->moderes = drmModeGetResources(data->drm_fd);
}

static void
sanity_test_fini(sanity_test_t *test, igt_output_t *output)
{
	drmModeFreeResources(test->moderes);

	igt_remove_fb(test->data->drm_fd, &test->oversized_fb);
	igt_remove_fb(test->data->drm_fd, &test->undersized_fb);
	igt_remove_fb(test->data->drm_fd, &test->blue_fb);

	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit2(&test->data->display, COMMIT_LEGACY);
}

/*
 * Universal plane sanity testing.
 *   - Primary doesn't cover CRTC
 *   - Primary plane tries to scale down
 *   - Primary plane tries to scale up
 */
static void
sanity_test_pipe(data_t *data, enum pipe pipe, igt_output_t *output)
{
	sanity_test_t test = { .data = data };
	igt_plane_t *primary;
	drmModeModeInfo *mode;
	int i, ret = 0;

	igt_output_set_pipe(output, pipe);
	mode = igt_output_get_mode(output);

	sanity_test_init(&test, output, pipe);

	primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);

	/* Use legacy API to set a mode with a blue FB */
	igt_plane_set_fb(primary, &test.blue_fb);
	igt_display_commit2(&data->display, COMMIT_LEGACY);

	/*
	 * Try to use universal plane API to set primary plane that
	 * doesn't cover CRTC (should fail).
	 */
	igt_plane_set_fb(primary, &test.undersized_fb);
	ret = igt_display_try_commit2(&data->display, COMMIT_UNIVERSAL);
	igt_assert(ret == -EINVAL);

	/* Same as above, but different plane positioning. */
	igt_plane_set_position(primary, 100, 100);
	ret = igt_display_try_commit2(&data->display, COMMIT_UNIVERSAL);
	igt_assert(ret == -EINVAL);

	igt_plane_set_position(primary, 0, 0);

	/* Try to use universal plane API to scale down (should fail) */
	ret = drmModeSetPlane(data->drm_fd, primary->drm_plane->plane_id,
			      output->config.crtc->crtc_id,
			      test.oversized_fb.fb_id, 0,
			      0, 0,
			      mode->hdisplay + 100,
			      mode->vdisplay + 100,
			      IGT_FIXED(0,0), IGT_FIXED(0,0),
			      IGT_FIXED(mode->hdisplay,0),
			      IGT_FIXED(mode->vdisplay,0));
	igt_assert(ret == -ERANGE);

	/* Try to use universal plane API to scale up (should fail) */
	ret = drmModeSetPlane(data->drm_fd, primary->drm_plane->plane_id,
			      output->config.crtc->crtc_id,
			      test.oversized_fb.fb_id, 0,
			      0, 0,
			      mode->hdisplay,
			      mode->vdisplay,
			      IGT_FIXED(0,0), IGT_FIXED(0,0),
			      IGT_FIXED(mode->hdisplay - 100,0),
			      IGT_FIXED(mode->vdisplay - 100,0));
	igt_assert(ret == -ERANGE);

	/* Find other crtcs and try to program our primary plane on them */
	for (i = 0; i < test.moderes->count_crtcs; i++)
		if (test.moderes->crtcs[i] != output->config.crtc->crtc_id) {
			ret = drmModeSetPlane(data->drm_fd,
					      primary->drm_plane->plane_id,
					      test.moderes->crtcs[i],
					      test.blue_fb.fb_id, 0,
					      0, 0,
					      mode->hdisplay,
					      mode->vdisplay,
					      IGT_FIXED(0,0), IGT_FIXED(0,0),
					      IGT_FIXED(mode->hdisplay,0),
					      IGT_FIXED(mode->vdisplay,0));
			igt_assert(ret == -EINVAL);
		}

	igt_plane_set_fb(primary, NULL);
	sanity_test_fini(&test, output);
}

static void
pageflip_test_init(pageflip_test_t *test, igt_output_t *output, enum pipe pipe)
{
	data_t *data = test->data;
	drmModeModeInfo *mode;

	igt_output_set_pipe(output, pipe);

	mode = igt_output_get_mode(output);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888,
			    false, /* tiled */
			    1.0, 0.0, 0.0,
			    &test->red_fb);
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888,
			    false, /* tiled */
			    0.0, 0.0, 1.0,
			    &test->blue_fb);
}

static void
pageflip_test_fini(pageflip_test_t *test, igt_output_t *output)
{
	igt_remove_fb(test->data->drm_fd, &test->red_fb);
	igt_remove_fb(test->data->drm_fd, &test->blue_fb);

	igt_output_set_pipe(output, PIPE_ANY);
	igt_display_commit2(&test->data->display, COMMIT_LEGACY);
}

static void
pageflip_test_pipe(data_t *data, enum pipe pipe, igt_output_t *output)
{
	pageflip_test_t test = { .data = data };
	igt_plane_t *primary;
	struct timeval timeout = { .tv_sec = 0, .tv_usec = 500 };
	drmEventContext evctx = { .version = DRM_EVENT_CONTEXT_VERSION };

	fd_set fds;
	int ret = 0;

	igt_output_set_pipe(output, pipe);

	pageflip_test_init(&test, output, pipe);

	primary = igt_output_get_plane(output, IGT_PLANE_PRIMARY);

	/* Use legacy API to set a mode with a blue FB */
	igt_plane_set_fb(primary, &test.blue_fb);
	igt_display_commit2(&data->display, COMMIT_LEGACY);

	/* Disable the primary plane */
	igt_plane_set_fb(primary, NULL);
	igt_display_commit2(&data->display, COMMIT_UNIVERSAL);

	/*
	 * Issue a pageflip to red FB
	 *
	 * Note that crtc->primary->fb = NULL causes flip to return EBUSY for
	 * historical reasons...
	 */
	ret = drmModePageFlip(data->drm_fd, output->config.crtc->crtc_id,
			      test.red_fb.fb_id, 0, NULL);
	igt_assert(ret == -EBUSY);

	/* Turn primary plane back on */
	igt_plane_set_fb(primary, &test.blue_fb);
	igt_display_commit2(&data->display, COMMIT_UNIVERSAL);

	/*
	 * Issue a pageflip to red, then immediately try to disable the primary
	 * plane, hopefully before the pageflip has a chance to complete.  The
	 * plane disable operation should wind up blocking while the pageflip
	 * completes, which we don't have a good way to specifically test for,
	 * but at least we can make sure that nothing blows up.
	 */
	ret = drmModePageFlip(data->drm_fd, output->config.crtc->crtc_id,
			      test.red_fb.fb_id, DRM_MODE_PAGE_FLIP_EVENT,
			      &test);
	igt_assert(ret == 0);
	igt_plane_set_fb(primary, NULL);
	igt_display_commit2(&data->display, COMMIT_UNIVERSAL);

	/* Wait for pageflip completion, then consume event on fd */
	FD_ZERO(&fds);
	FD_SET(data->drm_fd, &fds);
	do {
		ret = select(data->drm_fd + 1, &fds, NULL, NULL, &timeout);
	} while (ret < 0 && errno == EINTR);
	igt_assert(ret == 1);
	ret = drmHandleEvent(data->drm_fd, &evctx);
	igt_assert(ret == 0);

	igt_plane_set_fb(primary, NULL);
	pageflip_test_fini(&test, output);
}

static void
run_tests_for_pipe(data_t *data, enum pipe pipe)
{
	igt_output_t *output;

	igt_assert(data->display.has_universal_planes);

	igt_subtest_f("universal-plane-pipe-%c-functional", pipe_name(pipe))
		for_each_connected_output(&data->display, output)
			functional_test_pipe(data, pipe, output);

	igt_subtest_f("universal-plane-pipe-%c-sanity", pipe_name(pipe))
		for_each_connected_output(&data->display, output)
			sanity_test_pipe(data, pipe, output);

	igt_subtest_f("disable-primary-vs-flip-pipe-%c", pipe_name(pipe))
		for_each_connected_output(&data->display, output)
			pageflip_test_pipe(data, pipe, output);
}

static data_t data;

igt_main
{

	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_any();

		igt_set_vt_graphics_mode();

		igt_require_pipe_crc();
		igt_display_init(&data.display, data.drm_fd);

		igt_require(data.display.has_universal_planes);
	}

	for (int pipe = 0; pipe < 3; pipe++)
		run_tests_for_pipe(&data, pipe);

	igt_fixture {
		igt_display_fini(&data.display);
	}
}
