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
 * Authors:
 *    Paulo Zanoni <paulo.r.zanoni@intel.com>
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include "drm.h"
#include "drmtest.h"
#include "intel_batchbuffer.h"
#include "intel_gpu_tools.h"
#include "i915_drm.h"

#define MSR_PC8_RES	0x630
#define MSR_PC9_RES	0x631
#define MSR_PC10_RES	0x632

#define MAX_CONNECTORS	32
#define MAX_ENCODERS	32
#define MAX_CRTCS	16

enum screen_type {
	SCREEN_TYPE_LPSP,
	SCREEN_TYPE_NON_LPSP,
	SCREEN_TYPE_ANY,
};

int drm_fd, msr_fd;
struct mode_set_data ms_data;

/* Stuff used when creating FBs and mode setting. */
struct mode_set_data {
	drmModeResPtr res;
	drmModeConnectorPtr connectors[MAX_CONNECTORS];
	drmModePropertyBlobPtr edids[MAX_CONNECTORS];

	drm_intel_bufmgr *bufmgr;
	uint32_t devid;
};

/* Stuff we query at different times so we can compare. */
struct compare_data {
	drmModeResPtr res;
	drmModeEncoderPtr encoders[MAX_ENCODERS];
	drmModeConnectorPtr connectors[MAX_CONNECTORS];
	drmModeCrtcPtr crtcs[MAX_CRTCS];
	drmModePropertyBlobPtr edids[MAX_CONNECTORS];
};

struct compare_registers {
	/* We know these are lost */
	uint32_t arb_mode;
	uint32_t tilectl;

	/* Stuff touched at init_clock_gating, so we can make sure we
	 * don't need to call it when reiniting. */
	uint32_t gen6_ucgctl2;
	uint32_t gen7_l3cntlreg1;
	uint32_t transa_chicken1;

	uint32_t deier;
	uint32_t gtier;

	uint32_t ddi_buf_trans_a_1;
	uint32_t ddi_buf_trans_b_5;
	uint32_t ddi_buf_trans_c_10;
	uint32_t ddi_buf_trans_d_15;
	uint32_t ddi_buf_trans_e_20;
};

/* If the read fails, then the machine doesn't support PC8+ residencies. */
static bool supports_pc8_plus_residencies(void)
{
	int rc;
	uint64_t val;

	rc = pread(msr_fd, &val, sizeof(uint64_t), MSR_PC8_RES);
	if (rc != sizeof(val))
		return false;
	rc = pread(msr_fd, &val, sizeof(uint64_t), MSR_PC9_RES);
	if (rc != sizeof(val))
		return false;
	rc = pread(msr_fd, &val, sizeof(uint64_t), MSR_PC10_RES);
	if (rc != sizeof(val))
		return false;

	return true;
}

static uint64_t get_residency(uint32_t type)
{
	int rc;
	uint64_t ret;

	rc = pread(msr_fd, &ret, sizeof(uint64_t), type);
	igt_assert(rc == sizeof(ret));

	return ret;
}

static bool pc8_plus_residency_changed(unsigned int timeout_sec)
{
	unsigned int i;
	uint64_t res_pc8, res_pc9, res_pc10;
	int to_sleep = 100 * 1000;

	res_pc8 = get_residency(MSR_PC8_RES);
	res_pc9 = get_residency(MSR_PC9_RES);
	res_pc10 = get_residency(MSR_PC10_RES);

	for (i = 0; i < timeout_sec * 1000 * 1000; i += to_sleep) {
		if (res_pc8 != get_residency(MSR_PC8_RES) ||
		    res_pc9 != get_residency(MSR_PC9_RES) ||
		    res_pc10 != get_residency(MSR_PC10_RES)) {
			return true;
		}
		usleep(to_sleep);
	}

	return false;
}

/* Checks not only if PC8+ is allowed, but also if we're reaching it.
 * We call this when we expect this function to return quickly since PC8 is
 * actually enabled, so the 30s timeout we use shouldn't matter. */
static bool pc8_plus_enabled(void)
{
	return pc8_plus_residency_changed(30);
}

/* We call this when we expect PC8+ to be actually disabled, so we should not
 * return until the 5s timeout expires. In other words: in the "happy case",
 * every time we call this function the program will take 5s more to finish. */
static bool pc8_plus_disabled(void)
{
	return !pc8_plus_residency_changed(5);
}

static void disable_all_screens(struct mode_set_data *data)
{
	int i, rc;

	for (i = 0; i < data->res->count_crtcs; i++) {
		rc = drmModeSetCrtc(drm_fd, data->res->crtcs[i], -1, 0, 0,
				    NULL, 0, NULL);
		igt_assert(rc == 0);
	}
}

static uint32_t create_fb(struct mode_set_data *data, int width, int height)
{
	struct kmstest_fb fb;
	cairo_t *cr;
	uint32_t buffer_id;

	buffer_id = kmstest_create_fb(drm_fd, width, height, 32, 24, false,
				      &fb);
	cr = kmstest_get_cairo_ctx(drm_fd, &fb);
	kmstest_paint_test_pattern(cr, width, height);
	return buffer_id;
}

static bool enable_one_screen_with_type(struct mode_set_data *data,
					enum screen_type type)
{
	uint32_t crtc_id = 0, buffer_id = 0, connector_id = 0;
	drmModeModeInfoPtr mode = NULL;
	int i, rc;

	for (i = 0; i < data->res->count_connectors; i++) {
		drmModeConnectorPtr c = data->connectors[i];

		if (type == SCREEN_TYPE_LPSP &&
		    c->connector_type != DRM_MODE_CONNECTOR_eDP)
			continue;

		if (type == SCREEN_TYPE_NON_LPSP &&
		    c->connector_type == DRM_MODE_CONNECTOR_eDP)
			continue;

		if (c->connection == DRM_MODE_CONNECTED && c->count_modes) {
			connector_id = c->connector_id;
			mode = &c->modes[0];
			break;
		}
	}

	if (connector_id == 0)
		return false;

	crtc_id = data->res->crtcs[0];
	buffer_id = create_fb(data, mode->hdisplay, mode->vdisplay);

	igt_assert(crtc_id);
	igt_assert(buffer_id);
	igt_assert(connector_id);
	igt_assert(mode);

	rc = drmModeSetCrtc(drm_fd, crtc_id, buffer_id, 0, 0, &connector_id,
			    1, mode);
	igt_assert(rc == 0);

	return true;
}

static void enable_one_screen(struct mode_set_data *data)
{
	igt_assert(enable_one_screen_with_type(data, SCREEN_TYPE_ANY));
}

static drmModePropertyBlobPtr get_connector_edid(drmModeConnectorPtr connector,
						 int index)
{
	unsigned int i;
	drmModeObjectPropertiesPtr props;
	drmModePropertyBlobPtr ret = NULL;

	props = drmModeObjectGetProperties(drm_fd, connector->connector_id,
					   DRM_MODE_OBJECT_CONNECTOR);

	for (i = 0; i < props->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(drm_fd,
							     props->props[i]);

		if (strcmp(prop->name, "EDID") == 0) {
			igt_assert(prop->flags & DRM_MODE_PROP_BLOB);
			igt_assert(prop->count_blobs == 0);
			ret = drmModeGetPropertyBlob(drm_fd,
						     props->prop_values[i]);
		}

		drmModeFreeProperty(prop);
	}

	drmModeFreeObjectProperties(props);
	return ret;
}

static void init_mode_set_data(struct mode_set_data *data)
{
	int i;

	data->res = drmModeGetResources(drm_fd);
	igt_assert(data->res);
	igt_assert(data->res->count_connectors <= MAX_CONNECTORS);

	for (i = 0; i < data->res->count_connectors; i++) {
		data->connectors[i] = drmModeGetConnector(drm_fd,
						data->res->connectors[i]);
		data->edids[i] = get_connector_edid(data->connectors[i], i);
	}

	data->bufmgr = drm_intel_bufmgr_gem_init(drm_fd, 4096);
	data->devid = intel_get_drm_devid(drm_fd);

	do_or_die(igt_set_vt_graphics_mode());
	drm_intel_bufmgr_gem_enable_reuse(data->bufmgr);
}

static void fini_mode_set_data(struct mode_set_data *data)
{
	int i;

	drm_intel_bufmgr_destroy(data->bufmgr);

	for (i = 0; i < data->res->count_connectors; i++) {
		drmModeFreeConnector(data->connectors[i]);
		drmModeFreePropertyBlob(data->edids[i]);
	}
	drmModeFreeResources(data->res);
}

static void get_drm_info(struct compare_data *data)
{
	int i;

	data->res = drmModeGetResources(drm_fd);
	igt_assert(data->res);

	igt_assert(data->res->count_connectors <= MAX_CONNECTORS);
	igt_assert(data->res->count_encoders <= MAX_ENCODERS);
	igt_assert(data->res->count_crtcs <= MAX_CRTCS);

	for (i = 0; i < data->res->count_connectors; i++) {
		data->connectors[i] = drmModeGetConnector(drm_fd,
						data->res->connectors[i]);
		data->edids[i] = get_connector_edid(data->connectors[i], i);
	}
	for (i = 0; i < data->res->count_encoders; i++)
		data->encoders[i] = drmModeGetEncoder(drm_fd,
						data->res->encoders[i]);
	for (i = 0; i < data->res->count_crtcs; i++)
		data->crtcs[i] = drmModeGetCrtc(drm_fd, data->res->crtcs[i]);
}

static void get_registers(struct compare_registers *data)
{
	intel_register_access_init(intel_get_pci_device(), 0);
	data->arb_mode = INREG(0x4030);
	data->tilectl = INREG(0x101000);
	data->gen6_ucgctl2 = INREG(0x9404);
	data->gen7_l3cntlreg1 = INREG(0xB0C1);
	data->transa_chicken1 = INREG(0xF0060);
	data->deier = INREG(0x4400C);
	data->gtier = INREG(0x4401C);
	data->ddi_buf_trans_a_1 = INREG(0x64E00);
	data->ddi_buf_trans_b_5 = INREG(0x64E70);
	data->ddi_buf_trans_c_10 = INREG(0x64EE0);
	data->ddi_buf_trans_d_15 = INREG(0x64F58);
	data->ddi_buf_trans_e_20 = INREG(0x64FCC);
	intel_register_access_fini();
}

static void free_drm_info(struct compare_data *data)
{
	int i;

	for (i = 0; i < data->res->count_connectors; i++) {
		drmModeFreeConnector(data->connectors[i]);
		drmModeFreePropertyBlob(data->edids[i]);
	}
	for (i = 0; i < data->res->count_encoders; i++)
		drmModeFreeEncoder(data->encoders[i]);
	for (i = 0; i < data->res->count_crtcs; i++)
		drmModeFreeCrtc(data->crtcs[i]);

	drmModeFreeResources(data->res);
}

#define COMPARE(d1, d2, data) igt_assert(d1->data == d2->data)
#define COMPARE_ARRAY(d1, d2, size, data) do { \
	for (i = 0; i < size; i++) \
		igt_assert(d1->data[i] == d2->data[i]); \
} while (0)

static void assert_drm_resources_equal(struct compare_data *d1,
				       struct compare_data *d2)
{
	COMPARE(d1, d2, res->count_connectors);
	COMPARE(d1, d2, res->count_encoders);
	COMPARE(d1, d2, res->count_crtcs);
	COMPARE(d1, d2, res->min_width);
	COMPARE(d1, d2, res->max_width);
	COMPARE(d1, d2, res->min_height);
	COMPARE(d1, d2, res->max_height);
}

static void assert_modes_equal(drmModeModeInfoPtr m1, drmModeModeInfoPtr m2)
{
	COMPARE(m1, m2, clock);
	COMPARE(m1, m2, hdisplay);
	COMPARE(m1, m2, hsync_start);
	COMPARE(m1, m2, hsync_end);
	COMPARE(m1, m2, htotal);
	COMPARE(m1, m2, hskew);
	COMPARE(m1, m2, vdisplay);
	COMPARE(m1, m2, vsync_start);
	COMPARE(m1, m2, vsync_end);
	COMPARE(m1, m2, vtotal);
	COMPARE(m1, m2, vscan);
	COMPARE(m1, m2, vrefresh);
	COMPARE(m1, m2, flags);
	COMPARE(m1, m2, type);
	igt_assert(strcmp(m1->name, m2->name) == 0);
}

static void assert_drm_connectors_equal(drmModeConnectorPtr c1,
					drmModeConnectorPtr c2)
{
	int i;

	COMPARE(c1, c2, connector_id);
	COMPARE(c1, c2, connector_type);
	COMPARE(c1, c2, connector_type_id);
	COMPARE(c1, c2, mmWidth);
	COMPARE(c1, c2, mmHeight);
	COMPARE(c1, c2, count_modes);
	COMPARE(c1, c2, count_props);
	COMPARE(c1, c2, count_encoders);
	COMPARE_ARRAY(c1, c2, c1->count_props, props);
	COMPARE_ARRAY(c1, c2, c1->count_encoders, encoders);

	for (i = 0; i < c1->count_modes; i++)
		assert_modes_equal(&c1->modes[0], &c2->modes[0]);
}

static void assert_drm_encoders_equal(drmModeEncoderPtr e1,
				      drmModeEncoderPtr e2)
{
	COMPARE(e1, e2, encoder_id);
	COMPARE(e1, e2, encoder_type);
	COMPARE(e1, e2, possible_crtcs);
	COMPARE(e1, e2, possible_clones);
}

static void assert_drm_crtcs_equal(drmModeCrtcPtr c1, drmModeCrtcPtr c2)
{
	COMPARE(c1, c2, crtc_id);
}

static void assert_drm_edids_equal(drmModePropertyBlobPtr e1,
				   drmModePropertyBlobPtr e2)
{
	if (!e1 && !e2)
		return;
	igt_assert(e1 && e2);

	COMPARE(e1, e2, id);
	COMPARE(e1, e2, length);

	igt_assert(memcmp(e1->data, e2->data, e1->length) == 0);
}

static void compare_registers(struct compare_registers *d1,
			      struct compare_registers *d2)
{
	COMPARE(d1, d2, gen6_ucgctl2);
	COMPARE(d1, d2, gen7_l3cntlreg1);
	COMPARE(d1, d2, transa_chicken1);
	COMPARE(d1, d2, arb_mode);
	COMPARE(d1, d2, tilectl);
	COMPARE(d1, d2, arb_mode);
	COMPARE(d1, d2, tilectl);
	COMPARE(d1, d2, gtier);
	COMPARE(d1, d2, ddi_buf_trans_a_1);
	COMPARE(d1, d2, ddi_buf_trans_b_5);
	COMPARE(d1, d2, ddi_buf_trans_c_10);
	COMPARE(d1, d2, ddi_buf_trans_d_15);
	COMPARE(d1, d2, ddi_buf_trans_e_20);
}

static void assert_drm_infos_equal(struct compare_data *d1,
				   struct compare_data *d2)
{
	int i;

	assert_drm_resources_equal(d1, d2);

	for (i = 0; i < d1->res->count_connectors; i++) {
		assert_drm_connectors_equal(d1->connectors[i],
					    d2->connectors[i]);
		assert_drm_edids_equal(d1->edids[i], d2->edids[i]);
	}

	for (i = 0; i < d1->res->count_encoders; i++)
		assert_drm_encoders_equal(d1->encoders[i], d2->encoders[i]);

	for (i = 0; i < d1->res->count_crtcs; i++)
		assert_drm_crtcs_equal(d1->crtcs[i], d2->crtcs[i]);
}

static void blt_color_fill(struct intel_batchbuffer *batch,
			   drm_intel_bo *buf,
			   const unsigned int pages)
{
	const unsigned short height = pages/4;
	const unsigned short width = 4096;

	BEGIN_BATCH(5);
	OUT_BATCH(COLOR_BLT_CMD | COLOR_BLT_WRITE_ALPHA | COLOR_BLT_WRITE_RGB);
	OUT_BATCH((3 << 24) | /* 32 Bit Color */
		  0xF0 | /* Raster OP copy background register */
		  0); /* Dest pitch is 0 */
	OUT_BATCH(width << 16 | height);
	OUT_RELOC(buf, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(rand()); /* random pattern */
	ADVANCE_BATCH();
}

static void test_batch(struct mode_set_data *data)
{
	struct intel_batchbuffer *batch;
	int64_t timeout_ns = 1000 * 1000 * 1000 * 2;
	drm_intel_bo *dst;
	int i, rc;

	dst = drm_intel_bo_alloc(data->bufmgr, "dst", (8 << 20), 4096);

	batch = intel_batchbuffer_alloc(data->bufmgr,
					intel_get_drm_devid(drm_fd));

	for (i = 0; i < 1000; i++)
		blt_color_fill(batch, dst, ((8 << 20) >> 12));

	rc = drm_intel_gem_bo_wait(dst, timeout_ns);
	igt_assert(!rc);

	intel_batchbuffer_free(batch);
}

/* We could check the checksum too, but just the header is probably enough. */
static bool edid_is_valid(const unsigned char *edid)
{
	char edid_header[] = {
		0x0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0,
	};

	return (memcmp(edid, edid_header, sizeof(edid_header)) == 0);
}

static int count_drm_valid_edids(struct mode_set_data *data)
{
	int i, ret = 0;

	for (i = 0; i < data->res->count_connectors; i++)
		if (data->edids[i] && edid_is_valid(data->edids[i]->data))
			ret++;
	return ret;
}

static bool i2c_edid_is_valid(int fd)
{
	int rc;
	unsigned char edid[128] = {};
	struct i2c_msg msgs[] = {
		{ /* Start at 0. */
			.addr = 0x50,
			.flags = 0,
			.len = 1,
			.buf = edid,
		}, { /* Now read the EDID. */
			.addr = 0x50,
			.flags = I2C_M_RD,
			.len = 128,
			.buf = edid,
		}
	};
	struct i2c_rdwr_ioctl_data msgset = {
		.msgs = msgs,
		.nmsgs = 2,
	};

	rc = ioctl(fd, I2C_RDWR, &msgset);
	return (rc >= 0) ? edid_is_valid(edid) : false;
}

static int count_i2c_valid_edids(void)
{
	int fd, ret = 0;
	DIR *dir;

	struct dirent *dirent;
	char full_name[32];

	dir = opendir("/dev/");
	igt_assert(dir);

	while ((dirent = readdir(dir))) {
		if (strncmp(dirent->d_name, "i2c-", 4) == 0) {
			snprintf(full_name, 32, "/dev/%s", dirent->d_name);
			fd = open(full_name, O_RDWR);
			igt_assert(fd != -1);
			if (i2c_edid_is_valid(fd))
				ret++;
			close(fd);
		}
	}

	closedir(dir);

	return ret;
}

static bool test_i2c(struct mode_set_data *data)
{
	int i2c_edids = count_i2c_valid_edids();
	int drm_edids = count_drm_valid_edids(data);

	return i2c_edids == drm_edids;
}

static void setup_environment(void)
{
	drm_fd = drm_open_any();
	igt_assert(drm_fd >= 0);

	init_mode_set_data(&ms_data);

	/* Only Haswell supports the PC8 feature. */
	igt_require_f(IS_HASWELL(ms_data.devid),
		      "PC8+ feature only supported on Haswell.\n");

	/* Make sure our Kernel supports MSR and the module is loaded. */
	msr_fd = open("/dev/cpu/0/msr", O_RDONLY);
	igt_assert_f(msr_fd >= 0,
		     "Can't open /dev/cpu/0/msr.\n");

	/* Non-ULT machines don't support PC8+. */
	igt_require_f(supports_pc8_plus_residencies(),
		      "Machine doesn't support PC8+ residencies.\n");
}

static void basic_subtest(void)
{
	/* Make sure PC8+ residencies move! */
	disable_all_screens(&ms_data);
	igt_assert_f(pc8_plus_enabled(),
		     "Machine is not reaching PC8+ states, please check its "
		     "configuration.\n");

	/* Make sure PC8+ residencies stop! */
	enable_one_screen(&ms_data);
	igt_assert_f(pc8_plus_disabled(),
		     "PC8+ residency didn't stop with screen enabled.\n");
}

static void modeset_subtest(bool lpsp, bool stress, bool wait_for_residency)
{
	int i, rounds;
	enum screen_type type;

	rounds = stress ? 50 : 1;
	type = lpsp ? SCREEN_TYPE_LPSP : SCREEN_TYPE_NON_LPSP;

	for (i = 0; i < rounds; i++) {
		disable_all_screens(&ms_data);
		if (wait_for_residency)
			igt_assert(pc8_plus_enabled());

		/* If we skip this line it's because the type of screen we want
		 * is not connected. */
		igt_require(enable_one_screen_with_type(&ms_data, type));
		if (wait_for_residency)
			igt_assert(pc8_plus_disabled());
	}
}

static void teardown_environment(void)
{
	fini_mode_set_data(&ms_data);
	drmClose(drm_fd);
	close(msr_fd);
}

/* Test of the DRM resources reported by the IOCTLs are still the same. This
 * ensures we still see the monitors with the same eyes. We get the EDIDs and
 * compare them, which ensures we use DP AUX or GMBUS depending on what's
 * connected. */
static void drm_resources_equal_subtest(void)
{
	struct compare_data pre_pc8, during_pc8, post_pc8;

	printf("Checking the if the DRM resources match.\n");

	enable_one_screen(&ms_data);
	igt_assert(pc8_plus_disabled());
	get_drm_info(&pre_pc8);
	igt_assert(pc8_plus_disabled());

	disable_all_screens(&ms_data);
	igt_assert(pc8_plus_enabled());
	get_drm_info(&during_pc8);
	igt_assert(pc8_plus_enabled());

	enable_one_screen(&ms_data);
	igt_assert(pc8_plus_disabled());
	get_drm_info(&post_pc8);
	igt_assert(pc8_plus_disabled());

	assert_drm_infos_equal(&pre_pc8, &during_pc8);
	assert_drm_infos_equal(&pre_pc8, &post_pc8);

	free_drm_info(&pre_pc8);
	free_drm_info(&during_pc8);
	free_drm_info(&post_pc8);
}

/* Make sure interrupts are working. */
static void batch_subtest(void)
{
	printf("Testing batchbuffers.\n");

	enable_one_screen(&ms_data);
	igt_assert(pc8_plus_disabled());

	disable_all_screens(&ms_data);
	igt_assert(pc8_plus_enabled());
	test_batch(&ms_data);
	igt_assert(pc8_plus_enabled());
}

/* Try to use raw I2C, which also needs interrupts. */
static void i2c_subtest(void)
{
	int i2c_dev_files = 0;
	DIR *dev_dir;
	struct dirent *dirent;

	/* Make sure the /dev/i2c-* files exist. */
	dev_dir = opendir("/dev");
	igt_assert(dev_dir);
	while ((dirent = readdir(dev_dir))) {
		if (strncmp(dirent->d_name, "i2c-", 4) == 0)
			i2c_dev_files++;
	}
	closedir(dev_dir);
	igt_require(i2c_dev_files);

	enable_one_screen(&ms_data);
	igt_assert(pc8_plus_disabled());

	disable_all_screens(&ms_data);
	igt_assert(pc8_plus_enabled());
	igt_assert(test_i2c(&ms_data));
	igt_assert(pc8_plus_enabled());

	enable_one_screen(&ms_data);
}

/* Make us enter/leave PC8+ many times. */
static void stress_test(void)
{
	int i;

	printf("Stress testing.\n");

	for (i = 0; i < 100; i++) {
		disable_all_screens(&ms_data);
		igt_assert(pc8_plus_enabled());
		test_batch(&ms_data);
		igt_assert(pc8_plus_enabled());
	}
}

/* Just reading/writing registers from outside the Kernel is not really a safe
 * thing to do on Haswell, so don't do this test on the default case. */
static void register_compare_subtest(void)
{
	struct compare_registers pre_pc8, post_pc8;

	printf("Testing register compare.\n");

	enable_one_screen(&ms_data);
	igt_assert(pc8_plus_disabled());
	get_registers(&pre_pc8);
	igt_assert(pc8_plus_disabled());

	disable_all_screens(&ms_data);
	igt_assert(pc8_plus_enabled());
	enable_one_screen(&ms_data);
	igt_assert(pc8_plus_disabled());
	/* Wait for the registers to be restored. */
	sleep(1);
	get_registers(&post_pc8);
	igt_assert(pc8_plus_disabled());

	compare_registers(&pre_pc8, &post_pc8);
}

int main(int argc, char *argv[])
{
	bool do_register_compare = false;

	if (argc > 1 && strcmp(argv[1], "--do-register-compare") == 0)
		do_register_compare = true;

	igt_subtest_init(argc, argv);

	/* Skip instead of failing in case the machine is not prepared to reach
	 * PC8+. We don't want bug reports from cases where the machine is just
	 * not properly configured. */
	igt_fixture
		setup_environment();

	igt_subtest("basic")
		basic_subtest();
	igt_subtest("drm-resources-equal")
		drm_resources_equal_subtest();
	igt_subtest("modeset-lpsp")
		modeset_subtest(true, false, true);
	igt_subtest("modeset-non-lpsp")
		modeset_subtest(false, false, true);
	igt_subtest("batch")
		batch_subtest();
	igt_subtest("i2c")
		i2c_subtest();
	igt_subtest("stress-test")
		stress_test();
	igt_subtest("modeset-non-lpsp-stress")
		modeset_subtest(false, true, true);
	igt_subtest("modeset-lpsp-stress-no-wait")
		modeset_subtest(true, true, false);
	igt_subtest("modeset-non-lpsp-stress-no-wait")
		modeset_subtest(false, true, false);
	igt_subtest("register-compare") {
		igt_require(do_register_compare);
		register_compare_subtest();
	}

	igt_fixture
		teardown_environment();

	igt_exit();
}
