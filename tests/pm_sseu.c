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
 *    Jeff McGee <jeff.mcgee@intel.com>
 */

#include "igt.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "i915_drm.h"
#include "intel_bufmgr.h"

IGT_TEST_DESCRIPTION("Tests slice/subslice/EU power gating functionality.\n");

static double
to_dt(const struct timespec *start, const struct timespec *end)
{
	double dt;

	dt = (end->tv_sec - start->tv_sec) * 1e3;
	dt += (end->tv_nsec - start->tv_nsec) * 1e-6;

	return dt;
}

struct status {
	struct {
		int slice_total;
		int subslice_total;
		int subslice_per;
		int eu_total;
		int eu_per;
		bool has_slice_pg;
		bool has_subslice_pg;
		bool has_eu_pg;
	} info;
	struct {
		int slice_total;
		int subslice_total;
		int subslice_per;
		int eu_total;
		int eu_per;
	} hw;
};

#define DBG_STATUS_BUF_SIZE 4096

struct {
	int init;
	int status_fd;
	char status_buf[DBG_STATUS_BUF_SIZE];
} dbg;

static void
dbg_get_status_section(const char *title, char **first, char **last)
{
	char *pos;

	*first = strstr(dbg.status_buf, title);
	igt_assert(*first != NULL);

	pos = *first;
	do {
		pos = strchr(pos, '\n');
		igt_assert(pos != NULL);
		pos++;
	} while (*pos == ' '); /* lines in the section begin with a space */
	*last = pos - 1;
}

static int
dbg_get_int(const char *first, const char *last, const char *name)
{
	char *pos;

	pos = strstr(first, name);
	igt_assert(pos != NULL);
	pos = strstr(pos, ":");
	igt_assert(pos != NULL);
	pos += 2;
	igt_assert(pos != last);

	return strtol(pos, &pos, 10);
}

static bool
dbg_get_bool(const char *first, const char *last, const char *name)
{
	char *pos;

	pos = strstr(first, name);
	igt_assert(pos != NULL);
	pos = strstr(pos, ":");
	igt_assert(pos != NULL);
	pos += 2;
	igt_assert(pos < last);

	if (*pos == 'y')
		return true;
	if (*pos == 'n')
		return false;

	igt_assert_f(false, "Could not read boolean value for %s.\n", name);
	return false;
}

static void
dbg_get_status(struct status *stat)
{
	char *first, *last;
	int nread;

	lseek(dbg.status_fd, 0, SEEK_SET);
	nread = read(dbg.status_fd, dbg.status_buf, DBG_STATUS_BUF_SIZE);
	igt_assert_lt(nread, DBG_STATUS_BUF_SIZE);
	dbg.status_buf[nread] = '\0';

	memset(stat, 0, sizeof(*stat));

	dbg_get_status_section("SSEU Device Info", &first, &last);
	stat->info.slice_total =
		dbg_get_int(first, last, "Available Slice Total:");
	stat->info.subslice_total =
		dbg_get_int(first, last, "Available Subslice Total:");
	stat->info.subslice_per =
		dbg_get_int(first, last, "Available Subslice Per Slice:");
	stat->info.eu_total =
		dbg_get_int(first, last, "Available EU Total:");
	stat->info.eu_per =
		dbg_get_int(first, last, "Available EU Per Subslice:");
	stat->info.has_slice_pg =
		dbg_get_bool(first, last, "Has Slice Power Gating:");
	stat->info.has_subslice_pg =
		dbg_get_bool(first, last, "Has Subslice Power Gating:");
	stat->info.has_eu_pg =
		dbg_get_bool(first, last, "Has EU Power Gating:");

	dbg_get_status_section("SSEU Device Status", &first, &last);
	stat->hw.slice_total =
		dbg_get_int(first, last, "Enabled Slice Total:");
	stat->hw.subslice_total =
		dbg_get_int(first, last, "Enabled Subslice Total:");
	stat->hw.subslice_per =
		dbg_get_int(first, last, "Enabled Subslice Per Slice:");
	stat->hw.eu_total =
		dbg_get_int(first, last, "Enabled EU Total:");
	stat->hw.eu_per =
		dbg_get_int(first, last, "Enabled EU Per Subslice:");
}

static void
dbg_init(void)
{
	dbg.status_fd = igt_debugfs_open("i915_sseu_status", O_RDONLY);
	igt_skip_on_f(dbg.status_fd == -1,
		      "debugfs entry 'i915_sseu_status' not found\n");
	dbg.init = 1;
}

static void
dbg_deinit(void)
{
	switch (dbg.init)
	{
	case 1:
		close(dbg.status_fd);
	}
}

struct {
	int init;
	int drm_fd;
	int devid;
	int gen;
	int has_ppgtt;
	drm_intel_bufmgr *bufmgr;
	struct intel_batchbuffer *batch;
	igt_media_spinfunc_t spinfunc;
	struct igt_buf buf;
	uint32_t spins_per_msec;
} gem;

static void
gem_check_spin(uint32_t spins)
{
	uint32_t *data;

	data = (uint32_t*)gem.buf.bo->virtual;
	igt_assert_eq_u32(*data, spins);
}

static uint32_t
gem_get_target_spins(double dt)
{
	struct timespec tstart, tdone;
	double prev_dt, cur_dt;
	uint32_t spins;
	int i, ret;

	/* Double increments until we bound the target time */
	prev_dt = 0.0;
	for (i = 0; i < 32; i++) {
		spins = 1 << i;
		clock_gettime(CLOCK_MONOTONIC, &tstart);

		gem.spinfunc(gem.batch, &gem.buf, spins);
		ret = drm_intel_bo_map(gem.buf.bo, 0);
		igt_assert_eq(ret, 0);
		clock_gettime(CLOCK_MONOTONIC, &tdone);

		gem_check_spin(spins);
		drm_intel_bo_unmap(gem.buf.bo);

		cur_dt = to_dt(&tstart, &tdone);
		if (cur_dt > dt)
			break;
		prev_dt = cur_dt;
	}
	igt_assert_neq(i, 32);

	/* Linearly interpolate between i and i-1 to get target increments */
	spins = 1 << (i-1); /* lower bound spins */
	spins += spins * (dt - prev_dt)/(cur_dt - prev_dt); /* target spins */

	return spins;
}

static void
gem_init(void)
{
	gem.drm_fd = drm_open_driver(DRIVER_INTEL);
	gem.init = 1;

	gem.devid = intel_get_drm_devid(gem.drm_fd);
	gem.gen = intel_gen(gem.devid);
	igt_require_f(gem.gen >= 8,
		      "SSEU power gating only relevant for Gen8+");
	gem.has_ppgtt = gem_uses_ppgtt(gem.drm_fd);

	gem.bufmgr = drm_intel_bufmgr_gem_init(gem.drm_fd, 4096);
	igt_assert(gem.bufmgr);
	gem.init = 2;

	drm_intel_bufmgr_gem_enable_reuse(gem.bufmgr);

	gem.batch = intel_batchbuffer_alloc(gem.bufmgr, gem.devid);
	igt_assert(gem.batch);
	gem.init = 3;

	gem.spinfunc = igt_get_media_spinfunc(gem.devid);
	igt_assert(gem.spinfunc);

	gem.buf.stride = sizeof(uint32_t);
	gem.buf.tiling = I915_TILING_NONE;
	gem.buf.size = gem.buf.stride;
	gem.buf.bo = drm_intel_bo_alloc(gem.bufmgr, "", gem.buf.size, 4096);
	igt_assert(gem.buf.bo);
	gem.init = 4;

	gem.spins_per_msec = gem_get_target_spins(100) / 100;
}

static void
gem_deinit(void)
{
	switch (gem.init)
	{
	case 4:
		drm_intel_bo_unmap(gem.buf.bo);
		drm_intel_bo_unreference(gem.buf.bo);
	case 3:
		intel_batchbuffer_free(gem.batch);
	case 2:
		drm_intel_bufmgr_destroy(gem.bufmgr);
	case 1:
		close(gem.drm_fd);
	}
}

static void
check_full_enable(struct status *stat)
{
	igt_assert_eq(stat->hw.slice_total, stat->info.slice_total);
	igt_assert_eq(stat->hw.subslice_total, stat->info.subslice_total);
	igt_assert_eq(stat->hw.subslice_per, stat->info.subslice_per);

	/*
	 * EU are powered in pairs, but it is possible for one EU in the pair
	 * to be non-functional due to fusing. The determination of enabled
	 * EU does not account for this and can therefore actually exceed the
	 * available count. Allow for this small discrepancy in our
	 * comparison.
	*/
	igt_assert_lte(stat->info.eu_total, stat->hw.eu_total);
	igt_assert_lte(stat->info.eu_per, stat->hw.eu_per);
}

static void
full_enable(void)
{
	struct status stat;
	const int spin_msec = 10;
	int ret, spins;

	/* Simulation doesn't currently model slice/subslice/EU power gating. */
	igt_skip_on_simulation();

	/*
	 * Gen9 SKL is the first case in which render power gating can leave
	 * slice/subslice/EU in a partially enabled state upon resumption of
	 * render work. So start checking that this is prevented as of Gen9.
	*/
	igt_require(gem.gen >= 9);

	spins = spin_msec * gem.spins_per_msec;

	gem.spinfunc(gem.batch, &gem.buf, spins);

	usleep(2000); /* 2ms wait to make sure batch is running */
	dbg_get_status(&stat);

	ret = drm_intel_bo_map(gem.buf.bo, 0);
	igt_assert_eq(ret, 0);

	gem_check_spin(spins);
	drm_intel_bo_unmap(gem.buf.bo);

	check_full_enable(&stat);
}

static void
exit_handler(int sig)
{
	gem_deinit();
	dbg_deinit();
}

igt_main
{
	igt_fixture {
		igt_install_exit_handler(exit_handler);

		dbg_init();
		gem_init();
	}

	igt_subtest("full-enable")
		full_enable();
}
