/*
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Imre Deak <imre.deak@intel.com>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <cairo.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>

#include "drmtest.h"
#include "testdisplay.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_gpu_tools.h"

drmModeRes *resources;
int drm_fd;
static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
uint32_t devid;

enum test_flags {
	TEST_DIRECT_RENDER	= 0x01,
	TEST_GPU_BLIT		= 0x02,
};

static int paint_fb(struct kmstest_fb *fb, const char *test_name,
		    const char *mode_format_str, const char *cconf_str)
{
	cairo_t *cr;

	cr = kmstest_get_cairo_ctx(drm_fd, fb);
	if (!cr)
		return -1;

	kmstest_paint_color_gradient(cr, 0, 0, fb->width, fb->height, 1, 1, 1);
	kmstest_paint_test_pattern(cr, fb->width, fb->height);

	cairo_select_font_face(cr, "Helvetica", CAIRO_FONT_SLANT_NORMAL,
			       CAIRO_FONT_WEIGHT_NORMAL);
	cairo_move_to(cr, fb->width / 2, fb->height / 2);
	cairo_set_font_size(cr, 36);
	kmstest_cairo_printf_line(cr, align_hcenter, 10, "%s", test_name);
	kmstest_cairo_printf_line(cr, align_hcenter, 10, "%s", mode_format_str);
	kmstest_cairo_printf_line(cr, align_hcenter, 10, "%s", cconf_str);

	return 0;
}

static void gpu_blit(struct kmstest_fb *dst_fb, struct kmstest_fb *src_fb)
{
	drm_intel_bo *dst_bo;
	drm_intel_bo *src_bo;

	dst_bo = gem_handle_to_libdrm_bo(bufmgr, drm_fd, "destination",
					 dst_fb->gem_handle);
	assert(dst_bo);
	src_bo = gem_handle_to_libdrm_bo(bufmgr, drm_fd, "source",
					 src_fb->gem_handle);
	assert(src_bo);

	intel_copy_bo(batch, dst_bo, src_bo, src_fb->width, src_fb->height);
	intel_batchbuffer_flush(batch);
	gem_quiescent_gpu(drm_fd);

	drm_intel_bo_unreference(src_bo);
	drm_intel_bo_unreference(dst_bo);
}

static int test_format(const char *test_name,
		       struct kmstest_connector_config *cconf,
		       drmModeModeInfo *mode, uint32_t format,
		       enum test_flags flags)
{
	int width;
	int height;
	struct kmstest_fb fb[2];
	char *mode_format_str;
	char *cconf_str;
	int ret;

	ret = asprintf(&mode_format_str, "%s @ %dHz / %s",
		 mode->name, mode->vrefresh, kmstest_format_str(format));
	assert(ret > 0);
	ret = asprintf(&cconf_str, "pipe %s, encoder %s, connector %s",
		       kmstest_pipe_str(cconf->pipe),
		       kmstest_encoder_type_str(cconf->encoder->encoder_type),
		       kmstest_connector_type_str(cconf->connector->connector_type));
	assert(ret > 0);

	printf("Beginning test %s with %s on %s\n",
		test_name, mode_format_str, cconf_str);

	width = mode->hdisplay;
	height = mode->vdisplay;

	if (!kmstest_create_fb2(drm_fd, width, height, format, false, &fb[0]))
		goto err1;

	if (!kmstest_create_fb2(drm_fd, width, height, format, false, &fb[1]))
		goto err2;

	do_or_die(drmModeSetCrtc(drm_fd, cconf->crtc->crtc_id, fb[0].fb_id,
				 0, 0, &cconf->connector->connector_id, 1,
				 mode));
	do_or_die(drmModePageFlip(drm_fd, cconf->crtc->crtc_id, fb[0].fb_id,
				  0, NULL));
	sleep(2);

	if (flags & TEST_DIRECT_RENDER) {
		paint_fb(&fb[0], test_name, mode_format_str, cconf_str);
	} else if (flags & TEST_GPU_BLIT) {
		paint_fb(&fb[1], test_name, mode_format_str, cconf_str);
		gpu_blit(&fb[0], &fb[1]);
	}
	sleep(5);

	printf("Test %s with %s on %s: PASSED\n",
		test_name, mode_format_str, cconf_str);
	free(mode_format_str);
	free(cconf_str);

	kmstest_remove_fb(drm_fd, &fb[1]);
	kmstest_remove_fb(drm_fd, &fb[0]);

	return 0;

err2:
	kmstest_remove_fb(drm_fd, &fb[0]);
err1:
	fprintf(stderr, "skip testing unsupported format %s\n",
		kmstest_format_str(format));

	return -1;
}

static void test_connector(const char *test_name,
			   struct kmstest_connector_config *cconf,
			   enum test_flags flags)
{
	const uint32_t *formats;
	int format_count;
	int i;

	kmstest_get_all_formats(&formats, &format_count);
	for (i = 0; i < cconf->connector->count_modes; i++) {
		int j;

		for (j = 0; j < format_count; j++)
			test_format(test_name,
				    cconf, &cconf->connector->modes[i],
				    formats[j], flags);
	}
}

static int run_test(const char *test_name, enum test_flags flags)
{
	int i;

	resources = drmModeGetResources(drm_fd);
	assert(resources);

	/* Find any connected displays */
	for (i = 0; i < resources->count_connectors; i++) {
		uint32_t connector_id;
		int j;

		connector_id = resources->connectors[i];
		for (j = 0; j < resources->count_crtcs; j++) {
			struct kmstest_connector_config cconf;
			int ret;

			ret = kmstest_get_connector_config(drm_fd, connector_id,
							   1 << j, &cconf);
			if (ret < 0)
				continue;

			test_connector(test_name, &cconf, flags);

			kmstest_free_connector_config(&cconf);
		}
	}

	drmModeFreeResources(resources);

	return 1;
}

int main(int argc, char **argv)
{
	struct {
		enum test_flags flags;
		const char *name;
	} tests[] = {
		{ TEST_DIRECT_RENDER,	"direct-render" },
		{ TEST_GPU_BLIT,	"gpu-blit" },
	};
	int i;

	drmtest_subtest_init(argc, argv);

	if (!drmtest_only_list_subtests()) {
		drm_fd = drm_open_any();

		bufmgr = drm_intel_bufmgr_gem_init(drm_fd, 4096);
		devid = intel_get_drm_devid(drm_fd);
		batch = intel_batchbuffer_alloc(bufmgr, devid);

		do_or_die(drmtest_set_vt_graphics_mode());
	}

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (drmtest_run_subtest(tests[i].name))
			run_test(tests[i].name, tests[i].flags);
	}

	if (!drmtest_only_list_subtests())
		close(drm_fd);

	return 0;
}
