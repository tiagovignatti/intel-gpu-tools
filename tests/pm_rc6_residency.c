/*
 * Copyright © 2012 Intel Corporation
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
 *    Ben Widawsky <ben@bwidawsk.net>
 *
 */

#define _GNU_SOURCE
#include "igt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>


#define SLEEP_DURATION 3000 // in milliseconds
#define CODE_TIME 50 // in microseconfs

#define RC6_ENABLED	1
#define RC6P_ENABLED	2
#define RC6PP_ENABLED	4

struct residencies {
	int rc6;
	int media_rc6;
	int rc6p;
	int rc6pp;
};

static unsigned int readit(const char *path)
{
	unsigned int ret;
	int scanned;

	FILE *file;
	file = fopen(path, "r");
	igt_assert(file);
	scanned = fscanf(file, "%u", &ret);
	igt_assert_eq(scanned, 1);

	fclose(file);

	return ret;
}

static unsigned long get_rc6_enabled_mask(void)
{
	unsigned long rc6_mask;
	char *path;
	int ret;

	ret = asprintf(&path, "/sys/class/drm/card%d/power/rc6_enable",
		       drm_get_card());
	igt_assert_neq(ret, -1);
	rc6_mask = readit(path);
	free(path);

	return rc6_mask;
}

static int read_rc6_residency(const char *name_of_rc6_residency)
{
	unsigned int residency;
	const int device = drm_get_card();
	char *path ;
	int  ret;

	ret = asprintf(&path, "/sys/class/drm/card%d/power/%s_residency_ms",
		       device, name_of_rc6_residency);
	igt_assert_neq(ret, -1);
	residency = readit(path);
	free(path);

	return residency;
}

static void residency_accuracy(unsigned int diff,
			       const char *name_of_rc6_residency)
{
	double ratio;

	ratio = (double)diff / (SLEEP_DURATION + CODE_TIME);

	igt_info("Residency in %s or deeper state: %u ms (ratio to expected duration: %.02f)\n",
		 name_of_rc6_residency, diff, ratio);
	igt_assert_f(ratio > 0.9 && ratio <= 1,
		     "Sysfs RC6 residency counter is inaccurate.\n");
}

static void read_residencies(int devid, unsigned int rc6_mask,
			     struct residencies *res)
{
	if (rc6_mask & RC6_ENABLED)
		res->rc6 = read_rc6_residency("rc6");

	if ((rc6_mask & RC6_ENABLED) &&
	    (IS_VALLEYVIEW(devid) || IS_CHERRYVIEW(devid)))
		res->media_rc6 = read_rc6_residency("media_rc6");

	if (rc6_mask & RC6P_ENABLED)
		res->rc6p = read_rc6_residency("rc6p");

	if (rc6_mask & RC6PP_ENABLED)
		res->rc6pp = read_rc6_residency("rc6pp");
}

static void measure_residencies(int devid, unsigned int rc6_mask,
				struct residencies *res)
{
	struct residencies start = { };
	struct residencies end = { };
	int retry;

	if (!rc6_mask)
		return;

	/*
	 * For some reason my ivb isn't idle even after syncing up with the gpu.
	 * Let's add a sleep just to make it happy.
	 */
	sleep(8);

	/*
	 * Retry in case of counter wrap-around. We simply re-run the
	 * measurement, since the valid counter range is different on
	 * different platforms and so fixing it up would be non-trivial.
	 */
	for (retry = 0; retry < 2; retry++) {
		read_residencies(devid, rc6_mask, &start);
		sleep(SLEEP_DURATION / 1000);
		read_residencies(devid, rc6_mask, &end);

		if (end.rc6 >= start.rc6 && end.media_rc6 >= start.media_rc6 &&
		    end.rc6p >= start.rc6p && end.rc6pp >= start.rc6pp)
			break;
	}
	igt_assert_f(retry < 2, "residency values are not consistent\n");

	res->rc6 = end.rc6 - start.rc6;
	res->rc6p = end.rc6p - start.rc6p;
	res->rc6pp = end.rc6pp - start.rc6pp;
	res->media_rc6 = end.media_rc6 - start.media_rc6;

	/*
	 * For the purposes of this test case we want a given residency value
	 * to include the time spent in the corresponding RC state _and_ also
	 * the time spent in any enabled deeper states. So for example if any
	 * of RC6P or RC6PP is enabled we want the time spent in these states
	 * to be also included in the RC6 residency value. The kernel reported
	 * residency values are exclusive, so add up things here.
	 */
	res->rc6p += res->rc6pp;
	res->rc6 += res->rc6p;
}

igt_main
{
	unsigned int rc6_mask;
	int fd;
	int devid = 0;
	struct residencies res;

	igt_skip_on_simulation();

	/* Use drm_open_driver to verify device existence */
	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);
		devid = intel_get_drm_devid(fd);
		close(fd);

		rc6_mask = get_rc6_enabled_mask();
		measure_residencies(devid, rc6_mask, &res);
	}

	igt_subtest("rc6-accuracy") {
		igt_skip_on(!(rc6_mask & RC6_ENABLED));

		residency_accuracy(res.rc6, "rc6");
	}
	igt_subtest("media-rc6-accuracy") {
		igt_skip_on(!((rc6_mask & RC6_ENABLED) &&
			      (IS_VALLEYVIEW(devid) || IS_CHERRYVIEW(devid))));

		residency_accuracy(res.media_rc6, "media_rc6");
	}
	igt_subtest("rc6p-accuracy") {
		igt_skip_on(!(rc6_mask & RC6P_ENABLED));

		residency_accuracy(res.rc6p, "rc6p");
	}
	igt_subtest("rc6pp-accuracy") {
		igt_skip_on(!(rc6_mask & RC6PP_ENABLED));

		residency_accuracy(res.rc6pp, "rc6pp");
	}
}
