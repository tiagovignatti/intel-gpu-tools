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
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <drm.h>

#include "drmtest.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"
#include "intel_chipset.h"
#include "ioctl_wrappers.h"
#include "igt_aux.h"
#include "igt_kms.h"
#include "igt_debugfs.h"

/* One day, this will be on your libdrm. */
#define DRM_CLIENT_CAP_UNIVERSAL_PLANES 2

#define MSR_PC8_RES	0x630
#define MSR_PC9_RES	0x631
#define MSR_PC10_RES	0x632

#define MAX_CONNECTORS	32
#define MAX_ENCODERS	32
#define MAX_CRTCS	16

#define POWER_DIR "/sys/devices/pci0000:00/0000:00:02.0/power"

enum pc8_status {
	PC8_ENABLED,
	PC8_DISABLED
};

enum screen_type {
	SCREEN_TYPE_LPSP,
	SCREEN_TYPE_NON_LPSP,
	SCREEN_TYPE_ANY,
};

enum plane_type {
	PLANE_OVERLAY,
	PLANE_PRIMARY,
	PLANE_CURSOR,
};

/* Wait flags */
#define DONT_WAIT	0
#define WAIT_STATUS	1
#define WAIT_PC8_RES	2
#define WAIT_EXTRA	4
#define USE_DPMS	8

int drm_fd, msr_fd, pm_status_fd, pc8_status_fd;
bool has_runtime_pm, has_pc8;
struct mode_set_data ms_data;

/* Stuff used when creating FBs and mode setting. */
struct mode_set_data {
	drmModeResPtr res;
	drmModeConnectorPtr connectors[MAX_CONNECTORS];
	drmModePropertyBlobPtr edids[MAX_CONNECTORS];

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

struct modeset_params {
	uint32_t crtc_id;
	uint32_t connector_id;
	struct igt_fb fb;
	drmModeModeInfoPtr mode;
};

struct modeset_params lpsp_mode_params;
struct modeset_params non_lpsp_mode_params;
struct modeset_params *default_mode_params;

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

static enum pc8_status get_pc8_status(void)
{
	ssize_t n_read;
	char buf[150]; /* The whole file has less than 100 chars. */

	lseek(pc8_status_fd, 0, SEEK_SET);
	n_read = read(pc8_status_fd, buf, ARRAY_SIZE(buf));
	igt_assert(n_read >= 0);
	buf[n_read] = '\0';

	if (strstr(buf, "\nEnabled: yes\n"))
		return PC8_ENABLED;
	else
		return PC8_DISABLED;
}

static bool wait_for_pc8_status(enum pc8_status status)
{
	int i;
	int hundred_ms = 100 * 1000, ten_s = 10 * 1000 * 1000;

	for (i = 0; i < ten_s; i += hundred_ms) {
		if (get_pc8_status() == status)
			return true;

		usleep(hundred_ms);
	}

	return false;
}

static bool wait_for_suspended(void)
{
	if (has_pc8 && !has_runtime_pm)
		return wait_for_pc8_status(PC8_ENABLED);
	else
		return igt_wait_for_pm_status(IGT_RUNTIME_PM_STATUS_SUSPENDED);
}

static bool wait_for_active(void)
{
	if (has_pc8 && !has_runtime_pm)
		return wait_for_pc8_status(PC8_DISABLED);
	else
		return igt_wait_for_pm_status(IGT_RUNTIME_PM_STATUS_ACTIVE);
}

static void disable_all_screens_dpms(struct mode_set_data *data)
{
	int i;

	for (i = 0; i < data->res->count_connectors; i++) {
		drmModeConnectorPtr c = data->connectors[i];

		kmstest_set_connector_dpms(drm_fd, c, DRM_MODE_DPMS_OFF);
	}
}

static void disable_all_screens(struct mode_set_data *data)
{
	kmstest_unset_all_crtcs(drm_fd, data->res);
}

#define disable_all_screens_and_wait(data) do { \
	disable_all_screens(data); \
	igt_assert(wait_for_suspended()); \
} while (0)

static void disable_or_dpms_all_screens(struct mode_set_data *data, bool dpms)
{
	if (dpms)
		disable_all_screens_dpms(&ms_data);
	else
		disable_all_screens(&ms_data);
}

#define disable_or_dpms_all_screens_and_wait(data, dpms) do { \
	disable_or_dpms_all_screens((data), (dpms)); \
	igt_assert(wait_for_suspended()); \
} while (0)

static bool init_modeset_params_for_type(struct mode_set_data *data,
					 struct modeset_params *params,
					 enum screen_type type)
{
	int i;
	uint32_t connector_id = 0;
	drmModeModeInfoPtr mode = NULL;
	cairo_t *cr;

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

	if (!connector_id)
		return false;

	igt_create_fb(drm_fd, mode->hdisplay, mode->vdisplay,
		      DRM_FORMAT_XRGB8888, I915_TILING_NONE, &params->fb);
	cr = igt_get_cairo_ctx(drm_fd, &params->fb);
	igt_paint_test_pattern(cr, mode->hdisplay, mode->vdisplay);
	cairo_destroy(cr);

	params->crtc_id = data->res->crtcs[0];
	params->connector_id = connector_id;
	params->mode = mode;

	return true;
}

static void init_modeset_cached_params(struct mode_set_data *data)
{
	bool lpsp, non_lpsp;

	lpsp = init_modeset_params_for_type(data, &lpsp_mode_params,
					    SCREEN_TYPE_LPSP);
	non_lpsp = init_modeset_params_for_type(data, &non_lpsp_mode_params,
						SCREEN_TYPE_NON_LPSP);

	if (lpsp)
		default_mode_params = &lpsp_mode_params;
	else if (non_lpsp)
		default_mode_params = &non_lpsp_mode_params;
	else
		default_mode_params = NULL;
}

static bool set_mode_for_params(struct modeset_params *params)
{
	int rc;

	rc = drmModeSetCrtc(drm_fd, params->crtc_id, params->fb.fb_id, 0, 0,
			    &params->connector_id, 1, params->mode);
	return (rc == 0);
}

#define set_mode_for_params_and_wait(params) do { \
	igt_assert(set_mode_for_params(params)); \
	igt_assert(wait_for_active()); \
} while (0)

static bool enable_one_screen_with_type(struct mode_set_data *data,
					enum screen_type type)
{
	struct modeset_params *params = NULL;

	switch (type) {
	case SCREEN_TYPE_ANY:
		params = default_mode_params;
		break;
	case SCREEN_TYPE_LPSP:
		params = &lpsp_mode_params;
		break;
	case SCREEN_TYPE_NON_LPSP:
		params = &non_lpsp_mode_params;
		break;
	default:
		igt_assert(0);
	}

	if (!params)
		return false;

	return set_mode_for_params(params);
}

static void enable_one_screen(struct mode_set_data *data)
{
	/* SKIP if there are no connected screens. */
	igt_require(enable_one_screen_with_type(data, SCREEN_TYPE_ANY));
}

#define enable_one_screen_and_wait(data) do { \
	enable_one_screen(data); \
	igt_assert(wait_for_active()); \
} while (0)

static drmModePropertyBlobPtr get_connector_edid(drmModeConnectorPtr connector,
						 int index)
{
	bool found;
	uint64_t prop_value;
	drmModePropertyPtr prop;
	drmModePropertyBlobPtr blob = NULL;

	found = kmstest_get_property(drm_fd, connector->connector_id,
				     DRM_MODE_OBJECT_CONNECTOR, "EDID",
				     NULL, &prop_value, &prop);

	if (found) {
		igt_assert(prop->flags & DRM_MODE_PROP_BLOB);
		igt_assert(prop->count_blobs == 0);

		blob = drmModeGetPropertyBlob(drm_fd, prop_value);

		drmModeFreeProperty(prop);
	}

	return blob;
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

	data->devid = intel_get_drm_devid(drm_fd);

	kmstest_set_vt_graphics_mode();

	init_modeset_cached_params(&ms_data);
}

static void fini_mode_set_data(struct mode_set_data *data)
{
	int i;

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

static void test_i2c(struct mode_set_data *data)
{
	int i2c_edids = count_i2c_valid_edids();
	int drm_edids = count_drm_valid_edids(data);

	igt_assert_eq(i2c_edids, drm_edids);
}

static void setup_pc8(void)
{
	has_pc8 = false;

	/* Only Haswell supports the PC8 feature. */
	if (!IS_HASWELL(ms_data.devid) && !IS_BROADWELL(ms_data.devid))
		return;

	/* Make sure our Kernel supports MSR and the module is loaded. */
	igt_assert(system("modprobe -q msr > /dev/null 2>&1") != -1);

	msr_fd = open("/dev/cpu/0/msr", O_RDONLY);
	igt_assert_f(msr_fd >= 0,
		     "Can't open /dev/cpu/0/msr.\n");

	/* Non-ULT machines don't support PC8+. */
	if (!supports_pc8_plus_residencies())
		return;

	pc8_status_fd = open("/sys/kernel/debug/dri/0/i915_pc8_status",
			     O_RDONLY);
	igt_assert_f(pc8_status_fd >= 0,
		     "Can't open /sys/kernel/debug/dri/0/i915_pc8_status");

	has_pc8 = true;
}

/* If we want to actually reach PC8+ states, we need to properly configure all
 * the devices on the system to allow this. This function will try to setup the
 * things we know we need, but won't scream in case anything fails: we don't
 * know which devices are present on your machine, so we can't really expect
 * anything, just try to help with the more common problems. */
static void setup_non_graphics_runtime_pm(void)
{
	int fd, i;
	char *file_name;

	/* Disk runtime PM policies. */
	file_name = malloc(PATH_MAX);
	for (i = 0; ; i++) {

		snprintf(file_name, PATH_MAX,
			 "/sys/class/scsi_host/host%d/link_power_management_policy",
			 i);

		fd = open(file_name, O_WRONLY);
		if (fd < 0)
			break;

		write(fd, "min_power\n", 10);
		close(fd);
	}
	free(file_name);

	/* Audio runtime PM policies. */
	fd = open("/sys/module/snd_hda_intel/parameters/power_save", O_WRONLY);
	if (fd >= 0) {
		write(fd, "1\n", 2);
		close(fd);
	}
	fd = open("/sys/bus/pci/devices/0000:00:03.0/power/control", O_WRONLY);
	if (fd >= 0) {
		write(fd, "auto\n", 5);
		close(fd);
	}
}

static void setup_environment(void)
{
	drm_fd = drm_open_any_master();

	init_mode_set_data(&ms_data);

	setup_non_graphics_runtime_pm();

	has_runtime_pm = igt_setup_runtime_pm();
	setup_pc8();

	igt_info("Runtime PM support: %d\n", has_runtime_pm);
	igt_info("PC8 residency support: %d\n", has_pc8);

	igt_require(has_runtime_pm);

}

static void teardown_environment(void)
{
	fini_mode_set_data(&ms_data);
	drmClose(drm_fd);
	close(msr_fd);
	if (has_pc8)
		close(pc8_status_fd);
}

static void basic_subtest(void)
{
	disable_all_screens_and_wait(&ms_data);

	enable_one_screen_and_wait(&ms_data);
}

static void pc8_residency_subtest(void)
{
	igt_require(has_pc8);

	/* Make sure PC8+ residencies move! */
	disable_all_screens(&ms_data);
	igt_assert_f(pc8_plus_residency_changed(120),
		     "Machine is not reaching PC8+ states, please check its "
		     "configuration.\n");

	/* Make sure PC8+ residencies stop! */
	enable_one_screen(&ms_data);
	igt_assert_f(!pc8_plus_residency_changed(10),
		     "PC8+ residency didn't stop with screen enabled.\n");
}

static void modeset_subtest(enum screen_type type, int rounds, int wait_flags)
{
	int i;

	if (wait_flags & WAIT_PC8_RES)
		igt_require(has_pc8);

	for (i = 0; i < rounds; i++) {
		if (wait_flags & USE_DPMS)
			disable_all_screens_dpms(&ms_data);
		else
			disable_all_screens(&ms_data);

		if (wait_flags & WAIT_STATUS)
			igt_assert(wait_for_suspended());
		if (wait_flags & WAIT_PC8_RES)
			igt_assert(pc8_plus_residency_changed(120));
		if (wait_flags & WAIT_EXTRA)
			sleep(5);

		/* If we skip this line it's because the type of screen we want
		 * is not connected. */
		igt_require(enable_one_screen_with_type(&ms_data, type));
		if (wait_flags & WAIT_STATUS)
			igt_assert(wait_for_active());
		if (wait_flags & WAIT_PC8_RES)
			igt_assert(!pc8_plus_residency_changed(5));
		if (wait_flags & WAIT_EXTRA)
			sleep(5);
	}
}

/* Test of the DRM resources reported by the IOCTLs are still the same. This
 * ensures we still see the monitors with the same eyes. We get the EDIDs and
 * compare them, which ensures we use DP AUX or GMBUS depending on what's
 * connected. */
static void drm_resources_equal_subtest(void)
{
	struct compare_data pre_suspend, during_suspend, post_suspend;

	enable_one_screen_and_wait(&ms_data);
	get_drm_info(&pre_suspend);
	igt_assert(wait_for_active());

	disable_all_screens_and_wait(&ms_data);
	get_drm_info(&during_suspend);
	igt_assert(wait_for_suspended());

	enable_one_screen_and_wait(&ms_data);
	get_drm_info(&post_suspend);
	igt_assert(wait_for_active());

	assert_drm_infos_equal(&pre_suspend, &during_suspend);
	assert_drm_infos_equal(&pre_suspend, &post_suspend);

	free_drm_info(&pre_suspend);
	free_drm_info(&during_suspend);
	free_drm_info(&post_suspend);
}

static void i2c_subtest_check_environment(void)
{
	int i2c_dev_files = 0;
	DIR *dev_dir;
	struct dirent *dirent;

	/* Make sure the /dev/i2c-* files exist. */
	igt_assert(system("modprobe -q i2c-dev > /dev/null 2>&1") != -1);

	dev_dir = opendir("/dev");
	igt_assert(dev_dir);
	while ((dirent = readdir(dev_dir))) {
		if (strncmp(dirent->d_name, "i2c-", 4) == 0)
			i2c_dev_files++;
	}
	closedir(dev_dir);
	igt_require(i2c_dev_files);
}

/* Try to use raw I2C, which also needs interrupts. */
static void i2c_subtest(void)
{
	i2c_subtest_check_environment();

	enable_one_screen_and_wait(&ms_data);

	disable_all_screens_and_wait(&ms_data);
	test_i2c(&ms_data);
	igt_assert(wait_for_suspended());

	enable_one_screen(&ms_data);
}

static void read_full_file(const char *name)
{
	int rc, fd;
	char buf[128];

	igt_assert_f(wait_for_suspended(), "File: %s\n", name);

	fd = open(name, O_RDONLY);
	if (fd < 0)
		return;

	do {
		rc = read(fd, buf, ARRAY_SIZE(buf));
	} while (rc == ARRAY_SIZE(buf));

	rc = close(fd);
	igt_assert(rc == 0);

	igt_assert_f(wait_for_suspended(), "File: %s\n", name);
}

static void read_files_from_dir(const char *name, int level)
{
	DIR *dir;
	struct dirent *dirent;
	char *full_name;
	int rc;

	dir = opendir(name);
	igt_assert(dir);

	full_name = malloc(PATH_MAX);

	igt_assert(level < 128);

	while ((dirent = readdir(dir))) {
		struct stat stat_buf;

		if (strcmp(dirent->d_name, ".") == 0)
			continue;
		if (strcmp(dirent->d_name, "..") == 0)
			continue;

		snprintf(full_name, PATH_MAX, "%s/%s", name, dirent->d_name);

		rc = lstat(full_name, &stat_buf);
		igt_assert(rc == 0);

		if (S_ISDIR(stat_buf.st_mode))
			read_files_from_dir(full_name, level + 1);

		if (S_ISREG(stat_buf.st_mode))
			read_full_file(full_name);
	}

	free(full_name);
	closedir(dir);
}

/* This test will probably pass, with a small chance of hanging the machine in
 * case of bugs. Many of the bugs exercised by this patch just result in dmesg
 * errors, so a "pass" here should be confirmed by a check on dmesg. */
static void debugfs_read_subtest(void)
{
	const char *path = "/sys/kernel/debug/dri/0";
	DIR *dir;

	dir = opendir(path);
	igt_require_f(dir, "Can't open the debugfs directory\n");
	closedir(dir);

	disable_all_screens_and_wait(&ms_data);

	read_files_from_dir(path, 0);
}

/* Read the comment on debugfs_read_subtest(). */
static void sysfs_read_subtest(void)
{
	const char *path = "/sys/devices/pci0000:00/0000:00:02.0";
	DIR *dir;

	dir = opendir(path);
	igt_require_f(dir, "Can't open the sysfs directory\n");
	closedir(dir);

	disable_all_screens_and_wait(&ms_data);

	read_files_from_dir(path, 0);
}

/* Make sure we don't suspend when we have the i915_forcewake_user file open. */
static void debugfs_forcewake_user_subtest(void)
{
	int fd, rc;

	igt_require(intel_gen(ms_data.devid) >= 6);

	disable_all_screens_and_wait(&ms_data);

	fd = igt_open_forcewake_handle();
	igt_require(fd >= 0);

	if (has_runtime_pm) {
		igt_assert(wait_for_active());
		sleep(10);
		igt_assert(wait_for_active());
	} else {
		igt_assert(wait_for_suspended());
	}

	rc = close(fd);
	igt_assert(rc == 0);

	igt_assert(wait_for_suspended());
}

static void gem_mmap_subtest(bool gtt_mmap)
{
	int i;
	uint32_t handle;
	int buf_size = 8192;
	uint8_t *gem_buf;

	/* Create, map and set data while the device is active. */
	enable_one_screen_and_wait(&ms_data);

	handle = gem_create(drm_fd, buf_size);

	if (gtt_mmap)
		gem_buf = gem_mmap__gtt(drm_fd, handle, buf_size,
					PROT_READ | PROT_WRITE);
	else
		gem_buf = gem_mmap__cpu(drm_fd, handle, buf_size, 0);


	for (i = 0; i < buf_size; i++)
		gem_buf[i] = i & 0xFF;

	for (i = 0; i < buf_size; i++)
		igt_assert(gem_buf[i] == (i & 0xFF));

	/* Now suspend, read and modify. */
	disable_all_screens_and_wait(&ms_data);

	for (i = 0; i < buf_size; i++)
		igt_assert(gem_buf[i] == (i & 0xFF));
	igt_assert(wait_for_suspended());

	for (i = 0; i < buf_size; i++)
		gem_buf[i] = (~i & 0xFF);
	igt_assert(wait_for_suspended());

	/* Now resume and see if it's still there. */
	enable_one_screen_and_wait(&ms_data);
	for (i = 0; i < buf_size; i++)
		igt_assert(gem_buf[i] == (~i & 0xFF));

	igt_assert(munmap(gem_buf, buf_size) == 0);

	/* Now the opposite: suspend, and try to create the mmap while
	 * suspended. */
	disable_all_screens_and_wait(&ms_data);

	if (gtt_mmap)
		gem_buf = gem_mmap__gtt(drm_fd, handle, buf_size,
					PROT_READ | PROT_WRITE);
	else
		gem_buf = gem_mmap__cpu(drm_fd, handle, buf_size, 0);

	igt_assert(wait_for_suspended());

	for (i = 0; i < buf_size; i++)
		gem_buf[i] = i & 0xFF;

	for (i = 0; i < buf_size; i++)
		igt_assert(gem_buf[i] == (i & 0xFF));

	igt_assert(wait_for_suspended());

	/* Resume and check if it's still there. */
	enable_one_screen_and_wait(&ms_data);
	for (i = 0; i < buf_size; i++)
		igt_assert(gem_buf[i] == (i & 0xFF));

	igt_assert(munmap(gem_buf, buf_size) == 0);
	gem_close(drm_fd, handle);
}

static void gem_pread_subtest(void)
{
	int i;
	uint32_t handle;
	int buf_size = 8192;
	uint8_t *cpu_buf, *read_buf;

	cpu_buf = malloc(buf_size);
	read_buf = malloc(buf_size);
	igt_assert(cpu_buf);
	igt_assert(read_buf);
	memset(cpu_buf, 0, buf_size);
	memset(read_buf, 0, buf_size);

	/* Create and set data while the device is active. */
	enable_one_screen_and_wait(&ms_data);

	handle = gem_create(drm_fd, buf_size);

	for (i = 0; i < buf_size; i++)
		cpu_buf[i] = i & 0xFF;

	gem_write(drm_fd, handle, 0, cpu_buf, buf_size);

	gem_read(drm_fd, handle, 0, read_buf, buf_size);

	for (i = 0; i < buf_size; i++)
		igt_assert(cpu_buf[i] == read_buf[i]);

	/* Now suspend, read and modify. */
	disable_all_screens_and_wait(&ms_data);

	memset(read_buf, 0, buf_size);
	gem_read(drm_fd, handle, 0, read_buf, buf_size);

	for (i = 0; i < buf_size; i++)
		igt_assert(cpu_buf[i] == read_buf[i]);
	igt_assert(wait_for_suspended());

	for (i = 0; i < buf_size; i++)
		cpu_buf[i] = (~i & 0xFF);
	gem_write(drm_fd, handle, 0, cpu_buf, buf_size);
	igt_assert(wait_for_suspended());

	/* Now resume and see if it's still there. */
	enable_one_screen_and_wait(&ms_data);

	memset(read_buf, 0, buf_size);
	gem_read(drm_fd, handle, 0, read_buf, buf_size);

	for (i = 0; i < buf_size; i++)
		igt_assert(cpu_buf[i] == read_buf[i]);

	gem_close(drm_fd, handle);

	free(cpu_buf);
	free(read_buf);
}

/* Paints a square of color $color, size $width x $height, at position $x x $y
 * of $dst_handle, which contains pitch $pitch. */
static void submit_blt_cmd(uint32_t dst_handle, uint16_t x, uint16_t y,
			   uint16_t width, uint16_t height, uint32_t pitch,
			   uint32_t color, uint32_t *presumed_dst_offset)
{
	int i, reloc_pos;
	uint32_t batch_handle;
	int batch_size = 8 * sizeof(uint32_t);
	uint32_t batch_buf[batch_size];
	struct drm_i915_gem_execbuffer2 execbuf = {};
	struct drm_i915_gem_exec_object2 objs[2] = {{}, {}};
	struct drm_i915_gem_relocation_entry relocs[1] = {{}};
	struct drm_i915_gem_wait gem_wait;

	i = 0;

	if (intel_gen(ms_data.devid) >= 8)
		batch_buf[i++] = XY_COLOR_BLT_CMD_NOLEN |
				 XY_COLOR_BLT_WRITE_ALPHA |
				 XY_COLOR_BLT_WRITE_RGB | 0x5;
	else
		batch_buf[i++] = XY_COLOR_BLT_CMD_NOLEN |
				 XY_COLOR_BLT_WRITE_ALPHA |
				 XY_COLOR_BLT_WRITE_RGB | 0x4;
	batch_buf[i++] = (3 << 24) | (0xF0 << 16) | (pitch);
	batch_buf[i++] = (y << 16) | x;
	batch_buf[i++] = ((y + height) << 16) | (x + width);
	reloc_pos = i;
	batch_buf[i++] = *presumed_dst_offset;
	if (intel_gen(ms_data.devid) >= 8)
		batch_buf[i++] = 0;
	batch_buf[i++] = color;

	batch_buf[i++] = MI_BATCH_BUFFER_END;
	if (intel_gen(ms_data.devid) < 8)
		batch_buf[i++] = MI_NOOP;

	igt_assert(i * sizeof(uint32_t) == batch_size);

	batch_handle = gem_create(drm_fd, batch_size);
	gem_write(drm_fd, batch_handle, 0, batch_buf, batch_size);

	relocs[0].target_handle = dst_handle;
	relocs[0].delta = 0;
	relocs[0].offset = reloc_pos * sizeof(uint32_t);
	relocs[0].presumed_offset = *presumed_dst_offset;
	relocs[0].read_domains = 0;
	relocs[0].write_domain = I915_GEM_DOMAIN_RENDER;

	objs[0].handle = dst_handle;
	objs[0].alignment = 64;

	objs[1].handle = batch_handle;
	objs[1].relocation_count = 1;
	objs[1].relocs_ptr = (uintptr_t)relocs;

	execbuf.buffers_ptr = (uintptr_t)objs;
	execbuf.buffer_count = 2;
	execbuf.batch_len = batch_size;
	execbuf.flags = I915_EXEC_BLT;
	i915_execbuffer2_set_context_id(execbuf, 0);

	do_ioctl(drm_fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);

	*presumed_dst_offset = relocs[0].presumed_offset;

	gem_wait.flags = 0;
	gem_wait.timeout_ns = 10000000000LL; /* 10s */

	gem_wait.bo_handle = batch_handle;
	do_ioctl(drm_fd, DRM_IOCTL_I915_GEM_WAIT, &gem_wait);

	gem_wait.bo_handle = dst_handle;
	do_ioctl(drm_fd, DRM_IOCTL_I915_GEM_WAIT, &gem_wait);

	gem_close(drm_fd, batch_handle);
}

/* Make sure we can submit a batch buffer and verify its result. */
static void gem_execbuf_subtest(void)
{
	int x, y;
	uint32_t handle;
	int bpp = 4;
	int pitch = 128 * bpp;
	int dst_size = 128 * 128 * bpp; /* 128x128 square */
	uint32_t *cpu_buf;
	uint32_t presumed_offset = 0;
	int sq_x = 5, sq_y = 10, sq_w = 15, sq_h = 20;
	uint32_t color;

	/* Create and set data while the device is active. */
	enable_one_screen_and_wait(&ms_data);

	handle = gem_create(drm_fd, dst_size);

	cpu_buf = malloc(dst_size);
	igt_assert(cpu_buf);
	memset(cpu_buf, 0, dst_size);
	gem_write(drm_fd, handle, 0, cpu_buf, dst_size);

	/* Now suspend and try it. */
	disable_all_screens_and_wait(&ms_data);

	color = 0x12345678;
	submit_blt_cmd(handle, sq_x, sq_y, sq_w, sq_h, pitch, color,
		       &presumed_offset);
	igt_assert(wait_for_suspended());

	gem_read(drm_fd, handle, 0, cpu_buf, dst_size);
	igt_assert(wait_for_suspended());
	for (y = 0; y < 128; y++) {
		for (x = 0; x < 128; x++) {
			uint32_t px = cpu_buf[y * 128 + x];

			if (y >= sq_y && y < (sq_y + sq_h) &&
			    x >= sq_x && x < (sq_x + sq_w))
				igt_assert(px == color);
			else
				igt_assert(px == 0);
		}
	}

	/* Now resume and check for it again. */
	enable_one_screen_and_wait(&ms_data);

	memset(cpu_buf, 0, dst_size);
	gem_read(drm_fd, handle, 0, cpu_buf, dst_size);
	for (y = 0; y < 128; y++) {
		for (x = 0; x < 128; x++) {
			uint32_t px = cpu_buf[y * 128 + x];

			if (y >= sq_y && y < (sq_y + sq_h) &&
			    x >= sq_x && x < (sq_x + sq_w))
				igt_assert(px == color);
			else
				igt_assert(px == 0);
		}
	}

	/* Now we'll do the opposite: do the blt while active, then read while
	 * suspended. We use the same spot, but a different color. As a bonus,
	 * we're testing the presumed_offset from the previous command. */
	color = 0x87654321;
	submit_blt_cmd(handle, sq_x, sq_y, sq_w, sq_h, pitch, color,
		       &presumed_offset);

	disable_all_screens_and_wait(&ms_data);

	memset(cpu_buf, 0, dst_size);
	gem_read(drm_fd, handle, 0, cpu_buf, dst_size);
	for (y = 0; y < 128; y++) {
		for (x = 0; x < 128; x++) {
			uint32_t px = cpu_buf[y * 128 + x];

			if (y >= sq_y && y < (sq_y + sq_h) &&
			    x >= sq_x && x < (sq_x + sq_w))
				igt_assert(px == color);
			else
				igt_assert(px == 0);
		}
	}

	gem_close(drm_fd, handle);

	free(cpu_buf);
}

/* Assuming execbuf already works, let's see what happens when we force many
 * suspend/resume cycles with commands. */
static void gem_execbuf_stress_subtest(int rounds, int wait_flags)
{
	int i;
	int batch_size = 4 * sizeof(uint32_t);
	uint32_t batch_buf[batch_size];
	uint32_t handle;
	struct drm_i915_gem_execbuffer2 execbuf = {};
	struct drm_i915_gem_exec_object2 objs[1] = {{}};

	if (wait_flags & WAIT_PC8_RES)
		igt_require(has_pc8);

	i = 0;
	batch_buf[i++] = MI_NOOP;
	batch_buf[i++] = MI_NOOP;
	batch_buf[i++] = MI_BATCH_BUFFER_END;
	batch_buf[i++] = MI_NOOP;
	igt_assert(i * sizeof(uint32_t) == batch_size);

	disable_all_screens_and_wait(&ms_data);

	handle = gem_create(drm_fd, batch_size);
	gem_write(drm_fd, handle, 0, batch_buf, batch_size);

	objs[0].handle = handle;

	execbuf.buffers_ptr = (uintptr_t)objs;
	execbuf.buffer_count = 1;
	execbuf.batch_len = batch_size;
	execbuf.flags = I915_EXEC_RENDER;
	i915_execbuffer2_set_context_id(execbuf, 0);

	for (i = 0; i < rounds; i++) {
		do_ioctl(drm_fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);

		if (wait_flags & WAIT_STATUS)
			igt_assert(wait_for_suspended());
		if (wait_flags & WAIT_PC8_RES)
			igt_assert(pc8_plus_residency_changed(120));
		if (wait_flags & WAIT_EXTRA)
			sleep(5);
	}

	gem_close(drm_fd, handle);
}

/* When this test was written, it triggered WARNs and DRM_ERRORs on dmesg. */
static void gem_idle_subtest(void)
{
	disable_all_screens_and_wait(&ms_data);

	sleep(5);

	gem_quiescent_gpu(drm_fd);
}

/* This also triggered WARNs on dmesg at some point. */
static void reg_read_ioctl_subtest(void)
{
	struct drm_i915_reg_read rr = {
		.offset = 0x2358, /* render ring timestamp */
	};

	disable_all_screens_and_wait(&ms_data);

	do_ioctl(drm_fd, DRM_IOCTL_I915_REG_READ, &rr);

	igt_assert(wait_for_suspended());
}

static bool device_in_pci_d3(void)
{
	struct pci_device *pci_dev;
	int rc;
	uint16_t val;

	pci_dev = intel_get_pci_device();

	rc = pci_device_cfg_read_u16(pci_dev, &val, 0xd4);
	igt_assert(rc == 0);

	return (val & 0x3) == 0x3;
}

static void pci_d3_state_subtest(void)
{
	igt_require(has_runtime_pm);

	disable_all_screens_and_wait(&ms_data);

	igt_assert(device_in_pci_d3());

	enable_one_screen_and_wait(&ms_data);

	igt_assert(!device_in_pci_d3());
}

static void stay_subtest(void)
{
	disable_all_screens_and_wait(&ms_data);

	while (1)
		sleep(600);
}

static void system_suspend_subtest(void)
{
	disable_all_screens_and_wait(&ms_data);
	igt_system_suspend_autoresume();
	igt_assert(wait_for_suspended());
}

/* Enable a screen, activate DPMS, then do a modeset. At some point our driver
 * produced WARNs on this case. */
static void dpms_mode_unset_subtest(enum screen_type type)
{
	disable_all_screens_and_wait(&ms_data);

	igt_require(enable_one_screen_with_type(&ms_data, type));
	igt_assert(wait_for_active());

	disable_all_screens_dpms(&ms_data);
	igt_assert(wait_for_suspended());

	disable_all_screens_and_wait(&ms_data);
}

static void fill_igt_fb(struct igt_fb *fb, uint32_t color)
{
	int i;
	uint32_t *ptr;

	ptr = gem_mmap__gtt(drm_fd, fb->gem_handle, fb->size, PROT_WRITE);
	for (i = 0; i < fb->size/sizeof(uint32_t); i++)
		ptr[i] = color;
	igt_assert(munmap(ptr, fb->size) == 0);
}

/* At some point, this test triggered WARNs in the Kernel. */
static void cursor_subtest(bool dpms)
{
	int rc;
	struct igt_fb cursor_fb1, cursor_fb2, cursor_fb3;
	uint32_t crtc_id;

	disable_all_screens_and_wait(&ms_data);

	igt_require(default_mode_params);
	crtc_id = default_mode_params->crtc_id;

	igt_create_fb(drm_fd, 64, 64, DRM_FORMAT_ARGB8888, I915_TILING_NONE,
		      &cursor_fb1);
	igt_create_fb(drm_fd, 64, 64, DRM_FORMAT_ARGB8888, I915_TILING_NONE,
		      &cursor_fb2);
	igt_create_fb(drm_fd, 64, 64, DRM_FORMAT_ARGB8888, I915_TILING_X,
		      &cursor_fb3);

	fill_igt_fb(&cursor_fb1, 0xFF00FFFF);
	fill_igt_fb(&cursor_fb2, 0xFF00FF00);
	fill_igt_fb(&cursor_fb3, 0xFFFF0000);

	set_mode_for_params_and_wait(default_mode_params);

	rc = drmModeSetCursor(drm_fd, crtc_id, cursor_fb1.gem_handle,
			      cursor_fb1.width, cursor_fb1.height);
	igt_assert(rc == 0);
	rc = drmModeMoveCursor(drm_fd, crtc_id, 0, 0);
	igt_assert(rc == 0);
	igt_assert(wait_for_active());

	disable_or_dpms_all_screens_and_wait(&ms_data, dpms);

	/* First, just move the cursor. */
	rc = drmModeMoveCursor(drm_fd, crtc_id, 1, 1);
	igt_assert(rc == 0);
	igt_assert(wait_for_suspended());

	/* Then unset it, and set a new one. */
	rc = drmModeSetCursor(drm_fd, crtc_id, 0, 0, 0);
	igt_assert(rc == 0);
	igt_assert(wait_for_suspended());

	rc = drmModeSetCursor(drm_fd, crtc_id, cursor_fb2.gem_handle,
			      cursor_fb1.width, cursor_fb2.height);
	igt_assert(rc == 0);
	igt_assert(wait_for_suspended());

	/* Move the new cursor. */
	rc = drmModeMoveCursor(drm_fd, crtc_id, 2, 2);
	igt_assert(rc == 0);
	igt_assert(wait_for_suspended());

	/* Now set a new one without unsetting the previous one. */
	rc = drmModeSetCursor(drm_fd, crtc_id, cursor_fb1.gem_handle,
			      cursor_fb1.width, cursor_fb1.height);
	igt_assert(rc == 0);
	igt_assert(wait_for_suspended());

	/* Cursor 3 was created with tiling and painted with a GTT mmap, so
	 * hopefully it has some fences around it. */
	rc = drmModeRmFB(drm_fd, cursor_fb3.fb_id);
	igt_assert(rc == 0);
	gem_set_tiling(drm_fd, cursor_fb3.gem_handle, false, cursor_fb3.stride);
	igt_assert(wait_for_suspended());

	rc = drmModeSetCursor(drm_fd, crtc_id, cursor_fb3.gem_handle,
			      cursor_fb3.width, cursor_fb3.height);
	igt_assert(rc == 0);
	igt_assert(wait_for_suspended());

	/* Make sure nothing remains for the other tests. */
	rc = drmModeSetCursor(drm_fd, crtc_id, 0, 0, 0);
	igt_assert(rc == 0);
	igt_assert(wait_for_suspended());
}

static enum plane_type get_plane_type(uint32_t plane_id)
{
	int i;
	bool found;
	uint64_t prop_value;
	drmModePropertyPtr prop;
	const char *enum_name = NULL;
	enum plane_type type;

	found = kmstest_get_property(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE,
				     "type", NULL, &prop_value, &prop);
	igt_assert(found);

	igt_assert(prop->flags & DRM_MODE_PROP_ENUM);
	igt_assert(prop_value < prop->count_enums);

	for (i = 0; i < prop->count_enums; i++) {
		if (prop->enums[i].value == prop_value) {
			enum_name = prop->enums[i].name;
			break;
		}
	}
	igt_assert(enum_name);

	if (strcmp(enum_name, "Overlay") == 0)
		type = PLANE_OVERLAY;
	else if (strcmp(enum_name, "Primary") == 0)
		type = PLANE_PRIMARY;
	else if (strcmp(enum_name, "Cursor") == 0)
		type = PLANE_CURSOR;
	else
		igt_assert(0);

	drmModeFreeProperty(prop);

	return type;
}

static void test_one_plane(bool dpms, uint32_t plane_id,
			   enum plane_type plane_type)
{
	int rc;
	uint32_t plane_format, plane_w, plane_h;
	uint32_t crtc_id;
	struct igt_fb plane_fb1, plane_fb2;
	int32_t crtc_x = 0, crtc_y = 0;
	unsigned int tiling;

	disable_all_screens_and_wait(&ms_data);

	igt_require(default_mode_params);
	crtc_id = default_mode_params->crtc_id;

	switch (plane_type) {
	case PLANE_OVERLAY:
		plane_format = DRM_FORMAT_XRGB8888;
		plane_w = 64;
		plane_h = 64;
		tiling = I915_TILING_X;
		break;
	case PLANE_PRIMARY:
		plane_format = DRM_FORMAT_XRGB8888;
		plane_w = default_mode_params->mode->hdisplay;
		plane_h = default_mode_params->mode->vdisplay;
		tiling = I915_TILING_X;
		break;
	case PLANE_CURSOR:
		plane_format = DRM_FORMAT_ARGB8888;
		plane_w = 64;
		plane_h = 64;
		tiling = I915_TILING_NONE;
		break;
	default:
		igt_assert(0);
		break;
	}

	igt_create_fb(drm_fd, plane_w, plane_h, plane_format, tiling,
		      &plane_fb1);
	igt_create_fb(drm_fd, plane_w, plane_h, plane_format, tiling,
		      &plane_fb2);
	fill_igt_fb(&plane_fb1, 0xFF00FFFF);
	fill_igt_fb(&plane_fb2, 0xFF00FF00);

	set_mode_for_params_and_wait(default_mode_params);

	rc = drmModeSetPlane(drm_fd, plane_id, crtc_id, plane_fb1.fb_id, 0,
			     0, 0, plane_fb1.width, plane_fb1.height,
			     0 << 16, 0 << 16, plane_fb1.width << 16,
			     plane_fb1.height << 16);
	igt_assert(rc == 0);

	disable_or_dpms_all_screens_and_wait(&ms_data, dpms);

	/* Just move the plane around. */
	if (plane_type != PLANE_PRIMARY) {
		crtc_x++;
		crtc_y++;
	}
	rc = drmModeSetPlane(drm_fd, plane_id, crtc_id, plane_fb1.fb_id, 0,
			     crtc_x, crtc_y, plane_fb1.width, plane_fb1.height,
			     0 << 16, 0 << 16, plane_fb1.width << 16,
			     plane_fb1.height << 16);
	igt_assert(rc == 0);
	igt_assert(wait_for_suspended());

	/* Unset, then change the plane. */
	rc = drmModeSetPlane(drm_fd, plane_id, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	igt_assert(rc == 0);
	igt_assert(wait_for_suspended());

	rc = drmModeSetPlane(drm_fd, plane_id, crtc_id, plane_fb2.fb_id, 0,
			     crtc_x, crtc_y, plane_fb2.width, plane_fb2.height,
			     0 << 16, 0 << 16, plane_fb2.width << 16,
			     plane_fb2.height << 16);
	igt_assert(rc == 0);
	igt_assert(wait_for_suspended());

	/* Now change the plane without unsetting first. */
	rc = drmModeSetPlane(drm_fd, plane_id, crtc_id, plane_fb1.fb_id, 0,
			     crtc_x, crtc_y, plane_fb1.width, plane_fb1.height,
			     0 << 16, 0 << 16, plane_fb1.width << 16,
			     plane_fb1.height << 16);
	igt_assert(rc == 0);
	igt_assert(wait_for_suspended());

	/* Make sure nothing remains for the other tests. */
	rc = drmModeSetPlane(drm_fd, plane_id, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	igt_assert(rc == 0);
	igt_assert(wait_for_suspended());
}

/* This one also triggered WARNs on our driver at some point in time. */
static void planes_subtest(bool universal, bool dpms)
{
	int i, rc, planes_tested = 0;
	drmModePlaneResPtr planes;

	if (universal) {
		rc = drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES,
				     1);
		igt_require(rc == 0);
	}

	planes = drmModeGetPlaneResources(drm_fd);
	for (i = 0; i < planes->count_planes; i++) {
		drmModePlanePtr plane;

		plane = drmModeGetPlane(drm_fd, planes->planes[i]);
		igt_assert(plane);

		/* We just pick the first CRTC on the list, so we can test for
		 * 0x1 as the index. */
		if (plane->possible_crtcs & 0x1) {
			enum plane_type type;

			type = universal ? get_plane_type(plane->plane_id) :
					   PLANE_OVERLAY;
			test_one_plane(dpms, plane->plane_id, type);
			planes_tested++;
		}
		drmModeFreePlane(plane);
	}
	drmModeFreePlaneResources(planes);

	if (universal) {
		rc = drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 0);
		igt_assert(rc == 0);

		igt_assert(planes_tested >= 3);
	} else {
		igt_assert(planes_tested >= 1);
	}
}

static void fences_subtest(bool dpms)
{
	int i;
	uint32_t *buf_ptr;
	uint32_t tiling = false, swizzle;
	struct modeset_params params;

	disable_all_screens_and_wait(&ms_data);

	igt_require(default_mode_params);
	params.crtc_id = default_mode_params->crtc_id;
	params.connector_id = default_mode_params->connector_id;
	params.mode = default_mode_params->mode;
	igt_create_fb(drm_fd, params.mode->hdisplay, params.mode->vdisplay,
		      DRM_FORMAT_XRGB8888, I915_TILING_X, &params.fb);

	/* Even though we passed "true" as the tiling argument, double-check
	 * that the fb is really tiled. */
	gem_get_tiling(drm_fd, params.fb.gem_handle, &tiling, &swizzle);
	igt_assert(tiling);

	buf_ptr = gem_mmap__gtt(drm_fd, params.fb.gem_handle,
				params.fb.size, PROT_WRITE | PROT_READ);
	for (i = 0; i < params.fb.size/sizeof(uint32_t); i++)
		buf_ptr[i] = i;

	set_mode_for_params_and_wait(&params);

	disable_or_dpms_all_screens_and_wait(&ms_data, dpms);

	for (i = 0; i < params.fb.size/sizeof(uint32_t); i++)
		igt_assert_eq(buf_ptr[i], i);
	igt_assert(wait_for_suspended());

	if (dpms) {
		drmModeConnectorPtr c = NULL;

		for (i = 0; i < ms_data.res->count_connectors; i++)
			if (ms_data.connectors[i]->connector_id ==
			    params.connector_id)
				c = ms_data.connectors[i];
		igt_assert(c);

		kmstest_set_connector_dpms(drm_fd, c, DRM_MODE_DPMS_ON);
	} else {
		set_mode_for_params(&params);
	}
	igt_assert(wait_for_active());

	for (i = 0; i < params.fb.size/sizeof(uint32_t); i++)
		igt_assert_eq(buf_ptr[i], i);

	igt_assert(munmap(buf_ptr, params.fb.size) == 0);
}

int rounds = 50;
bool stay = false;

static int opt_handler(int opt, int opt_index)
{
	switch (opt) {
	case 'q':
		rounds = 10;
		break;
	case 's':
		stay = true;
		break;
	default:
		igt_assert(0);
	}

	return 0;
}

int main(int argc, char *argv[])
{
	const char *help_str =
	       "  --quick\t\tMake the stress-tests not stressful, for quick regression testing.\n"
	       "  --stay\t\tDisable all screen and try to go into runtime pm. Useful for debugging.";
	static struct option long_options[] = {
		{"quick", 0, 0, 'q'},
		{"stay", 0, 0, 's'},
		{ 0, 0, 0, 0 }
	};

	igt_subtest_init_parse_opts(argc, argv, "", long_options,
				    help_str, opt_handler);

	/* Skip instead of failing in case the machine is not prepared to reach
	 * PC8+. We don't want bug reports from cases where the machine is just
	 * not properly configured. */
	igt_fixture
		setup_environment();

	if (stay)
		igt_subtest("stay")
			stay_subtest();

	/* Essential things */
	igt_subtest("rte")
		basic_subtest();
	igt_subtest("drm-resources-equal")
		drm_resources_equal_subtest();
	igt_subtest("pci-d3-state")
		pci_d3_state_subtest();

	/* Basic modeset */
	igt_subtest("modeset-lpsp")
		modeset_subtest(SCREEN_TYPE_LPSP, 1, WAIT_STATUS);
	igt_subtest("modeset-non-lpsp")
		modeset_subtest(SCREEN_TYPE_NON_LPSP, 1, WAIT_STATUS);
	igt_subtest("dpms-lpsp")
		modeset_subtest(SCREEN_TYPE_LPSP, 1, WAIT_STATUS | USE_DPMS);
	igt_subtest("dpms-non-lpsp")
		modeset_subtest(SCREEN_TYPE_NON_LPSP, 1, WAIT_STATUS | USE_DPMS);

	/* GEM */
	igt_subtest("gem-mmap-cpu")
		gem_mmap_subtest(false);
	igt_subtest("gem-mmap-gtt")
		gem_mmap_subtest(true);
	igt_subtest("gem-pread")
		gem_pread_subtest();
	igt_subtest("gem-execbuf")
		gem_execbuf_subtest();
	igt_subtest("gem-idle")
		gem_idle_subtest();

	/* Planes and cursors */
	igt_subtest("cursor")
		cursor_subtest(false);
	igt_subtest("cursor-dpms")
		cursor_subtest(true);
	igt_subtest("legacy-planes")
		planes_subtest(false, false);
	igt_subtest("legacy-planes-dpms")
		planes_subtest(false, true);
	igt_subtest("universal-planes")
		planes_subtest(true, false);
	igt_subtest("universal-planes-dpms")
		planes_subtest(true, true);

	/* Misc */
	igt_subtest("reg-read-ioctl")
		reg_read_ioctl_subtest();
	igt_subtest("i2c")
		i2c_subtest();
	igt_subtest("pc8-residency")
		pc8_residency_subtest();
	igt_subtest("debugfs-read")
		debugfs_read_subtest();
	igt_subtest("debugfs-forcewake-user")
		debugfs_forcewake_user_subtest();
	igt_subtest("sysfs-read")
		sysfs_read_subtest();
	igt_subtest("dpms-mode-unset-lpsp")
		dpms_mode_unset_subtest(SCREEN_TYPE_LPSP);
	igt_subtest("dpms-mode-unset-non-lpsp")
		dpms_mode_unset_subtest(SCREEN_TYPE_NON_LPSP);
	igt_subtest("fences")
		fences_subtest(false);
	igt_subtest("fences-dpms")
		fences_subtest(true);

	/* Modeset stress */
	igt_subtest("modeset-lpsp-stress")
		modeset_subtest(SCREEN_TYPE_LPSP, rounds, WAIT_STATUS);
	igt_subtest("modeset-non-lpsp-stress")
		modeset_subtest(SCREEN_TYPE_NON_LPSP, rounds, WAIT_STATUS);
	igt_subtest("modeset-lpsp-stress-no-wait")
		modeset_subtest(SCREEN_TYPE_LPSP, rounds, DONT_WAIT);
	igt_subtest("modeset-non-lpsp-stress-no-wait")
		modeset_subtest(SCREEN_TYPE_NON_LPSP, rounds, DONT_WAIT);
	igt_subtest("modeset-pc8-residency-stress")
		modeset_subtest(SCREEN_TYPE_ANY, rounds, WAIT_PC8_RES);
	igt_subtest("modeset-stress-extra-wait")
		modeset_subtest(SCREEN_TYPE_ANY, rounds,
				WAIT_STATUS | WAIT_EXTRA);

	/* System suspend */
	igt_subtest("system-suspend")
		system_suspend_subtest();

	/* GEM stress */
	igt_subtest("gem-execbuf-stress")
		gem_execbuf_stress_subtest(rounds, WAIT_STATUS);
	igt_subtest("gem-execbuf-stress-pc8")
		gem_execbuf_stress_subtest(rounds, WAIT_PC8_RES);
	igt_subtest("gem-execbuf-stress-extra-wait")
		gem_execbuf_stress_subtest(rounds, WAIT_STATUS | WAIT_EXTRA);

	igt_fixture
		teardown_environment();

	igt_exit();
}
