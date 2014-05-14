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

#include <cairo.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <sys/time.h>

#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"
#include "igt_kms.h"

#define MAX_CONNECTORS  10
#define MAX_CRTCS       3

/* max combinations with repetitions */
#define MAX_COMBINATION_COUNT   \
	(MAX_CONNECTORS * MAX_CONNECTORS * MAX_CONNECTORS)
#define MAX_COMBINATION_ELEMS   MAX_CRTCS

static int drm_fd;
static drmModeRes *drm_resources;
static int filter_test_id;
static bool dry_run;

const drmModeModeInfo mode_640_480 = {
	.name		= "640x480",
	.vrefresh	= 60,
	.clock		= 25200,

	.hdisplay	= 640,
	.hsync_start	= 656,
	.hsync_end	= 752,
	.htotal		= 800,

	.vdisplay	= 480,
	.vsync_start	= 490,
	.vsync_end	= 492,
	.vtotal		= 525,

	.flags		= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

enum test_flags {
	TEST_INVALID			= 0x01,
	TEST_CLONE			= 0x02,
	TEST_SINGLE_CRTC_CLONE		= 0x04,
	TEST_EXCLUSIVE_CRTC_CLONE	= 0x08,
};

struct test_config {
	const char *name;
	enum test_flags flags;
	drmModeRes *resources;
};

struct connector_config {
	drmModeConnector *connector;
	int crtc_idx;
	bool connected;
	drmModeModeInfo default_mode;
};

struct crtc_config {
	int crtc_idx;
	int crtc_id;
	int pipe_id;
	int connector_count;
	struct connector_config *cconfs;
	struct igt_fb fb_info;
	drmModeModeInfo mode;
};

static bool drm_mode_equal(drmModeModeInfo *m1, drmModeModeInfo *m2)
{
#define COMP(x) do { if (m1->x != m2->x) return false; } while (0)
	COMP(vrefresh);
	COMP(clock);
	COMP(hdisplay);
	COMP(hsync_start);
	COMP(hsync_end);
	COMP(htotal);
	COMP(vdisplay);
	COMP(vsync_start);
	COMP(vsync_end);
	COMP(vtotal);
	COMP(flags);

	return true;
}

static bool connector_supports_mode(drmModeConnector *connector,
				    drmModeModeInfo *mode)
{
	int i;

	for (i = 0; i < connector->count_modes; i++)
		if (drm_mode_equal(&connector->modes[i], mode))
			return true;

	return false;
}

static bool crtc_supports_mode(struct crtc_config *crtc, drmModeModeInfo *mode)
{
	int i;

	for (i = 0; i < crtc->connector_count; i++) {
		if (!connector_supports_mode(crtc->cconfs[i].connector, mode))
			return false;
	}

	return true;
}

static int paint_fb(struct igt_fb *fb, const char *test_name,
		    const char **crtc_str, int crtc_count, int current_crtc_idx)
{
	double x, y;
	cairo_t *cr;
	int i;

	cr = igt_get_cairo_ctx(drm_fd, fb);

	igt_paint_test_pattern(cr, fb->width, fb->height);

	cairo_move_to(cr, fb->width / 2, fb->height / 2);
	cairo_set_font_size(cr, 24);
	igt_cairo_printf_line(cr, align_hcenter, 40, "%s", test_name);

	cairo_get_current_point(cr, &x, &y);
	cairo_move_to(cr, 60, y);

	for (i = 0; i < crtc_count; i++) {
		if (i == current_crtc_idx) {
			cairo_get_current_point(cr, &x, &y);
			cairo_move_to(cr, x - 20, y);
			igt_cairo_printf_line(cr, align_right, 20, "X");
			cairo_move_to(cr, x, y);
		}
		igt_cairo_printf_line(cr, align_left, 20, "%s",
					  crtc_str[i]);
	}

	cairo_destroy(cr);

	return 0;
}

static void create_fb_for_crtc(struct crtc_config *crtc,
			       struct igt_fb *fb_info)
{
	int bpp;
	int depth;
	bool enable_tiling;
	int fb_id;

	bpp = 32;
	depth = 24;
	enable_tiling = false;
	fb_id = igt_create_fb(drm_fd, crtc->mode.hdisplay,
				  crtc->mode.vdisplay,
				  igt_bpp_depth_to_drm_format(bpp, depth),
				  enable_tiling, fb_info);
	igt_assert(fb_id > 0);
}

static void get_mode_for_crtc(struct crtc_config *crtc,
			      drmModeModeInfo *mode_ret)
{
	drmModeModeInfo mode;
	int i;

	/*
	 * First try to select a default mode that is supported by all
	 * connectors.
	 */
	for (i = 0; i < crtc->connector_count; i++) {
		mode = crtc->cconfs[i].default_mode;
		if (crtc_supports_mode(crtc, &mode))
			goto found;
	}

	/*
	 * Then just fall back to find any that is supported by all
	 * connectors.
	 */
	for (i = 0; i < crtc->cconfs[0].connector->count_modes; i++) {
		mode = crtc->cconfs[0].connector->modes[i];
		if (crtc_supports_mode(crtc, &mode))
			goto found;
	}

	/*
	 * If none is found then just pick the default mode of the first
	 * connector and hope the other connectors can support it by scaling
	 * etc.
	 */
	mode = crtc->cconfs[0].default_mode;
found:
	*mode_ret = mode;
}

static int get_encoder_idx(drmModeRes *resources, drmModeEncoder *encoder)
{
	int i;

	for (i = 0; i < resources->count_encoders; i++)
		if (resources->encoders[i] == encoder->encoder_id)
			return i;
	igt_assert(0);
}

static void get_crtc_config_str(struct crtc_config *crtc, char *buf,
				size_t buf_size)
{
	int pos;
	int i;

	pos = snprintf(buf, buf_size,
		       "CRTC[%d] [Pipe %s] Mode: %s@%dHz Connectors: ",
		       crtc->crtc_id, kmstest_pipe_str(crtc->pipe_id),
		       crtc->mode.name, crtc->mode.vrefresh);
	if (pos > buf_size)
		return;
	for (i = 0; i < crtc->connector_count; i++) {
		drmModeConnector *connector = crtc->cconfs[i].connector;

		pos += snprintf(&buf[pos], buf_size - pos,
			"%s%s-%d[%d]%s", i ? ", " : "",
			kmstest_connector_type_str(connector->connector_type),
			connector->connector_type_id, connector->connector_id,
			crtc->cconfs[i].connected ? "" : " (NC)");
		if (pos > buf_size)
			return;
	}
}

static void setup_crtcs(drmModeRes *resources, struct connector_config *cconf,
			int connector_count, struct crtc_config *crtcs,
			int *crtc_count_ret, bool *config_valid_ret)
{
	struct crtc_config *crtc;
	int crtc_count;
	bool config_valid;
	int i;
	int encoder_usage_count[resources->count_encoders];

	i = 0;
	crtc_count = 0;
	crtc = crtcs;
	config_valid = true;

	while (i < connector_count) {
		drmModeCrtc *drm_crtc;
		unsigned long encoder_mask;
		int j;

		igt_assert(crtc_count < MAX_CRTCS);

		crtc->crtc_idx = cconf[i].crtc_idx;
		drm_crtc = drmModeGetCrtc(drm_fd,
					  resources->crtcs[crtc->crtc_idx]);
		crtc->crtc_id = drm_crtc->crtc_id;
		drmModeFreeCrtc(drm_crtc);
		crtc->pipe_id = kmstest_get_pipe_from_crtc_id(drm_fd,
							      crtc->crtc_id);

		crtc->connector_count = 1;
		for (j = i + 1; j < connector_count; j++)
			if (cconf[j].crtc_idx == crtc->crtc_idx)
				crtc->connector_count++;

		crtc->cconfs = malloc(sizeof(*crtc->cconfs) *
				      crtc->connector_count);
		igt_assert(crtc->cconfs);

		encoder_mask = 0;
		for (j = 0; j < crtc->connector_count; j++) {
			drmModeConnector *connector;
			drmModeEncoder *encoder;

			crtc->cconfs[j] = cconf[i + j];
			connector = cconf[i + j].connector;

			/* Intel connectors have only a single encoder */
			igt_assert(connector->count_encoders == 1);
			encoder = drmModeGetEncoder(drm_fd,
						    connector->encoders[0]);
			igt_assert(encoder);

			config_valid &= !!(encoder->possible_crtcs &
					  (1 << crtc->crtc_idx));

			encoder_mask |= 1 << get_encoder_idx(resources,
							     encoder);
			config_valid &= !(encoder_mask &
					  ~encoder->possible_clones);

			drmModeFreeEncoder(encoder);
		}
		get_mode_for_crtc(crtc, &crtc->mode);
		create_fb_for_crtc(crtc, &crtc->fb_info);

		i += crtc->connector_count;
		crtc_count++;
		crtc++;
	}

	memset(encoder_usage_count, 0, sizeof(encoder_usage_count));
	for (i = 0; i < connector_count; i++) {
		drmModeConnector *connector = cconf[i].connector;
		drmModeEncoder *encoder;

		igt_assert(connector->count_encoders == 1);
		encoder = drmModeGetEncoder(drm_fd, connector->encoders[0]);
		encoder_usage_count[get_encoder_idx(resources, encoder)]++;
		drmModeFreeEncoder(encoder);
	}
	for (i = 0; i < resources->count_encoders; i++)
		if (encoder_usage_count[i] > 1)
			config_valid = false;

	*crtc_count_ret = crtc_count;
	*config_valid_ret = config_valid;
}

static void cleanup_crtcs(struct crtc_config *crtcs, int crtc_count)
{
	int i;

	for (i = 0; i < crtc_count; i++) {
		free(crtcs[i].cconfs);
	}
}

static uint32_t *get_connector_ids(struct crtc_config *crtc)
{
	uint32_t *ids;
	int i;

	ids = malloc(sizeof(*ids) * crtc->connector_count);
	igt_assert(ids);
	for (i = 0; i < crtc->connector_count; i++)
		ids[i] = crtc->cconfs[i].connector->connector_id;

	return ids;
}

static void test_crtc_config(const struct test_config *tconf,
			     struct crtc_config *crtcs, int crtc_count)
{
	char str_buf[MAX_CRTCS][1024];
	const char *crtc_strs[MAX_CRTCS];
	struct crtc_config *crtc;
	static int test_id;
	bool config_failed = false;
	bool connector_connected = false;
	int ret = 0;
	int i;

	test_id++;

	if (filter_test_id && filter_test_id != test_id)
		return;

	igt_info("  Test id#%d CRTC count %d\n", test_id, crtc_count);

	for (i = 0; i < crtc_count; i++) {
		get_crtc_config_str(&crtcs[i], str_buf[i], sizeof(str_buf[i]));
		crtc_strs[i] = &str_buf[i][0];
	}

	if (dry_run) {
		for (i = 0; i < crtc_count; i++)
			igt_info("    %s\n", crtc_strs[i]);
		return;
	}

	for (i = 0; i < crtc_count; i++) {
		uint32_t *ids;
		int j;

		crtc = &crtcs[i];

		igt_info("    %s\n", crtc_strs[i]);

		create_fb_for_crtc(crtc, &crtc->fb_info);
		paint_fb(&crtc->fb_info, tconf->name, crtc_strs, crtc_count, i);

		ids = get_connector_ids(crtc);
		ret = drmModeSetCrtc(drm_fd, crtc->crtc_id,
				     crtc->fb_info.fb_id, 0, 0, ids,
				     crtc->connector_count, &crtc->mode);
		free(ids);

		if (ret < 0) {
			igt_assert(errno == EINVAL);
			config_failed = true;
		}

		for (j = 0; j < crtc->connector_count; j++)
			connector_connected |= crtc->cconfs[j].connected;
	}

	igt_assert(config_failed == !!(tconf->flags & TEST_INVALID));

	if (ret == 0 && connector_connected && !(tconf->flags & TEST_INVALID))
		sleep(5);

	for (i = 0; i < crtc_count; i++) {
		if (crtcs[i].fb_info.fb_id) {
			drmModeSetCrtc(drm_fd, crtcs[i].crtc_id, 0, 0, 0, NULL,
					0, NULL);
			drmModeRmFB(drm_fd, crtcs[i].fb_info.fb_id);
			crtcs[i].fb_info.fb_id = 0;
		}
	}

	return;
}

static void test_one_combination(const struct test_config *tconf,
				 struct connector_config *cconfs,
				 int connector_count)
{
	struct crtc_config crtcs[MAX_CRTCS];
	int crtc_count;
	bool config_valid;

	setup_crtcs(tconf->resources, cconfs, connector_count, crtcs,
		    &crtc_count, &config_valid);

	if (config_valid == !(tconf->flags & TEST_INVALID))
		test_crtc_config(tconf, crtcs, crtc_count);

	cleanup_crtcs(crtcs, crtc_count);
}

static int assign_crtc_to_connectors(const struct test_config *tconf,
				     int *crtc_idxs, int connector_count,
				     struct connector_config *cconfs)
{
	unsigned long crtc_idx_mask;
	int i;

	crtc_idx_mask = 0;
	for (i = 0; i < connector_count; i++) {
		int crtc_idx = crtc_idxs[i];

		if ((tconf->flags & TEST_SINGLE_CRTC_CLONE) &&
		    crtc_idx_mask & ~(1 << crtc_idx))
			return -1;

		if ((tconf->flags & TEST_EXCLUSIVE_CRTC_CLONE) &&
		    crtc_idx_mask & (1 << crtc_idx))
			return -1;

		crtc_idx_mask |= 1 << crtc_idx;

		cconfs[i].crtc_idx = crtc_idx;
	}

	return 0;
}

static int get_one_connector(drmModeRes *resources, int connector_id,
			     struct connector_config *cconf)
{
	drmModeConnector *connector;
	drmModeModeInfo mode;

	connector = drmModeGetConnector(drm_fd, connector_id);
	igt_assert(connector);
	cconf->connector = connector;

	cconf->connected = connector->connection == DRM_MODE_CONNECTED;

	/*
	 * For DP/eDP we need a connected sink, since mode setting depends
	 * on successful link training and retrieved DPCD parameters.
	 */
	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_DisplayPort:
	case DRM_MODE_CONNECTOR_eDP:
		if (!cconf->connected) {
			drmModeFreeConnector(connector);
			return -1;
		}
	}

	if (cconf->connected) {
		if (kmstest_get_connector_default_mode(drm_fd, connector,
							&mode) < 0)
			mode = mode_640_480;
	} else {
		mode = mode_640_480;
	}

	cconf->default_mode = mode;

	return 0;
}

static int get_connectors(drmModeRes *resources, int *connector_idxs,
			  int connector_count, struct connector_config *cconfs)
{
	int i;

	for (i = 0; i < connector_count; i++) {
		int connector_idx;
		int connector_id;

		connector_idx = connector_idxs[i];
		igt_assert(connector_idx < resources->count_connectors);
		connector_id = resources->connectors[connector_idx];

		if (get_one_connector(resources, connector_id, &cconfs[i]) < 0)
			goto err;

	}

	return 0;

err:
	while (i--)
		drmModeFreeConnector(cconfs[i].connector);

	return -1;
}

static void free_connectors(struct connector_config *cconfs,
			    int connector_count)
{
	int i;

	for (i = 0; i < connector_count; i++)
		drmModeFreeConnector(cconfs[i].connector);
}

struct combination {
	int elems[MAX_COMBINATION_ELEMS];
};

struct combination_set {
	int count;
	struct combination items[MAX_COMBINATION_COUNT];
};

/*
 * Get all possible selection of k elements from n elements with or without
 * repetitions.
 */
static void iterate_combinations(int n, int k, bool allow_repetitions,
				 int depth, int base, struct combination *comb,
				 struct combination_set *set)
{
	int v;

	if (!k) {
		igt_assert(set->count < ARRAY_SIZE(set->items));
		set->items[set->count++] = *comb;
		return;
	}

	for (v = base; v < n; v++) {
		comb->elems[depth] = v;
		iterate_combinations(n, k - 1, allow_repetitions,
				     depth + 1, allow_repetitions ? 0 : v + 1,
				     comb, set);
	}

}

static void get_combinations(int n, int k, bool allow_repetitions,
			     struct combination_set *set)
{
	struct combination comb;

	igt_assert(k <= ARRAY_SIZE(set->items[0].elems));
	set->count = 0;
	iterate_combinations(n, k, allow_repetitions, 0, 0, &comb, set);
}

static void test_combinations(const struct test_config *tconf,
			      int connector_count)
{
	struct combination_set connector_combs;
	struct combination_set crtc_combs;
	struct connector_config *cconfs;
	int i;

	get_combinations(tconf->resources->count_connectors, connector_count,
			 false, &connector_combs);
	get_combinations(tconf->resources->count_crtcs, connector_count,
			 true, &crtc_combs);

	igt_info("Testing: %s %d connector combinations\n", tconf->name,
		 connector_count);
	for (i = 0; i < connector_combs.count; i++) {
		int *connector_idxs;
		int ret;
		int j;

		cconfs = malloc(sizeof(*cconfs) * connector_count);
		igt_assert(cconfs);

		connector_idxs = &connector_combs.items[i].elems[0];
		ret = get_connectors(tconf->resources, connector_idxs,
				     connector_count, cconfs);
		if (ret < 0)
			goto free_cconfs;

		for (j = 0; j < crtc_combs.count; j++) {
			int *crtc_idxs = &crtc_combs.items[j].elems[0];
			ret = assign_crtc_to_connectors(tconf, crtc_idxs,
							connector_count,
						        cconfs);
			if (ret < 0)
				continue;

			test_one_combination(tconf, cconfs, connector_count);
		}

		free_connectors(cconfs, connector_count);
free_cconfs:
		free(cconfs);
	}
}

static void run_test(const struct test_config *tconf)
{
	int connector_num;

	connector_num = tconf->flags & TEST_CLONE ? 2 : 1;
	for (; connector_num <= tconf->resources->count_crtcs; connector_num++)
		test_combinations(tconf, connector_num);
}

static int opt_handler(int opt, int opt_index)
{
	switch (opt) {
	case 'd':
		dry_run = true;
		break;
	case 't':
		filter_test_id = atoi(optarg);
		break;
	default:
		igt_assert(0);
	}

	return 0;
}

int main(int argc, char **argv)
{
	const struct {
		enum test_flags flags;
		const char *name;
	} tests[] = {
		{ TEST_CLONE | TEST_SINGLE_CRTC_CLONE,
					"clone-single-crtc" },
		{ TEST_INVALID | TEST_CLONE | TEST_SINGLE_CRTC_CLONE,
					"invalid-clone-single-crtc" },
		{ TEST_INVALID | TEST_CLONE | TEST_EXCLUSIVE_CRTC_CLONE,
					"invalid-clone-exclusive-crtc" },
		{ TEST_CLONE | TEST_EXCLUSIVE_CRTC_CLONE,
					"clone-exclusive-crtc" },
	};
	const char *help_str =
	       "  -d\t\tDon't run any test, only print what would be done. (still needs DRM access)\n"
	       "  -t <test id>\tRun only the test with this id.";
	int i;
	int ret;

	ret = igt_subtest_init_parse_opts(argc, argv, "dt:", NULL, help_str,
					  opt_handler);
	if (ret < 0)
		return ret == -1 ? 0 : ret;

	igt_skip_on_simulation();

	igt_assert_f(!(dry_run && filter_test_id),
		     "only one of -d and -t is accepted\n");

	igt_fixture {
		drm_fd = drm_open_any();
		if (!dry_run)
			igt_set_vt_graphics_mode();

		drm_resources = drmModeGetResources(drm_fd);
		igt_assert(drm_resources);
	}

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		igt_subtest(tests[i].name) {
			struct test_config tconf = {
				.flags		= tests[i].flags,
				.name		= tests[i].name,
				.resources	= drm_resources,
			};
			run_test(&tconf);
		}
	}

	igt_fixture {
		drmModeFreeResources(drm_resources);

		close(drm_fd);
	}

	igt_exit();
}
