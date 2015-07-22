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
 *
 * Authors:
 *    Jeff McGee <jeff.mcgee@intel.com>
 *
 */

#include "igt.h"
#include <unistd.h>
#include <errno.h>
#include <xf86drm.h>
#include <i915_drm.h>
#include "intel_bufmgr.h"

IGT_TEST_DESCRIPTION("Tests the export of parameters via DRM_IOCTL_I915_GETPARAM\n");

int drm_fd;
int devid;

static void
init(void)
{
	drm_fd = drm_open_driver(DRIVER_INTEL);
	devid = intel_get_drm_devid(drm_fd);
}

static void
deinit(void)
{
	close(drm_fd);
}

#define LOCAL_I915_PARAM_SUBSLICE_TOTAL	33
#define LOCAL_I915_PARAM_EU_TOTAL	34

static int
getparam(int param, int *value)
{
	drm_i915_getparam_t gp;
	int ret;

	memset(&gp, 0, sizeof(gp));
	gp.value = value;
	gp.param = param;
	ret = drmIoctl(drm_fd, DRM_IOCTL_I915_GETPARAM, &gp);
	if (ret)
		return -errno;

	return 0;
}

static void
subslice_total(void)
{
	unsigned int subslice_total = 0;
	int ret;

	ret = getparam(LOCAL_I915_PARAM_SUBSLICE_TOTAL, (int*)&subslice_total);
	igt_skip_on_f(ret == -EINVAL && intel_gen(devid), "Interface not supported by kernel\n");

	if (ret) {
		/*
		 * These devices are not required to implement the
		 * interface. If they do not, -ENODEV must be returned.
		*/
		if ((intel_gen(devid) < 8) ||
		    IS_BROADWELL(devid) ||
		    igt_run_in_simulation()) {
			igt_assert_eq(ret, -ENODEV);
			igt_info("subslice total: unknown\n");
		/*
		 * All other devices must implement the interface, so
		 * fail them if we are here.
		 */
		} else {
			igt_assert_eq(ret, 0);
		}
	} else {
		/*
		 * On success, just make sure the returned count value is
		 * non-zero. The validity of the count value for the given
		 * device is not checked.
		*/
		igt_assert_neq(subslice_total, 0);
		igt_info("subslice total: %u\n", subslice_total);
	}
}

static void
eu_total(void)
{
	unsigned int eu_total = 0;
	int ret;

	ret = getparam(LOCAL_I915_PARAM_EU_TOTAL, (int*)&eu_total);
	igt_skip_on_f(ret == -EINVAL, "Interface not supported by kernel\n");

	if (ret) {
		/*
		 * These devices are not required to implement the
		 * interface. If they do not, -ENODEV must be returned.
		*/
		if ((intel_gen(devid) < 8) ||
		    IS_BROADWELL(devid) ||
		    igt_run_in_simulation()) {
			igt_assert_eq(ret, -ENODEV);
			igt_info("EU total: unknown\n");
		/*
		 * All other devices must implement the interface, so
		 * fail them if we are here.
		*/
		} else {
			igt_assert_eq(ret, 0);
		}
	} else {
		/*
		 * On success, just make sure the returned count value is
		 * non-zero. The validity of the count value for the given
		 * device is not checked.
		*/
		igt_assert_neq(eu_total, 0);
		igt_info("EU total: %u\n", eu_total);
	}
}

static void
exit_handler(int sig)
{
	deinit();
}

igt_main
{
	igt_fixture {
		igt_install_exit_handler(exit_handler);
		init();
	}

	igt_subtest("basic-subslice-total")
		subslice_total();

	igt_subtest("basic-eu-total")
		eu_total();
}
