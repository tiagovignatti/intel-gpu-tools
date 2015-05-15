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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "drmtest.h"
#include "intel_chipset.h"

#define SLEEP_DURATION 3000 // in milliseconds
#define CODE_TIME 50 // in microseconfs

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

static int read_rc6_residency(const char *name_of_rc6_residency)
{
	unsigned int i;
	const int device = drm_get_card();
	char *path ;
	int  ret;
	FILE *file;
	int value[2];

	/* For some reason my ivb isn't idle even after syncing up with the gpu.
	 * Let's add a sleept just to make it happy. */
	sleep(5);

	ret = asprintf(&path, "/sys/class/drm/card%d/power/rc6_enable", device);
	igt_assert_neq(ret, -1);

	file = fopen(path, "r");
	igt_require(file);

	/* claim success if no rc6 enabled. */
	if (readit(path) == 0)
		igt_success();

	for(i = 0; i < 2; i++)
	{
		sleep(SLEEP_DURATION / 1000);
		ret = asprintf(&path, "/sys/class/drm/card%d/power/%s_residency_ms",device,name_of_rc6_residency);
		igt_assert_neq(ret, -1);
		value[i] = readit(path);
	}
	free(path);

	return value[1] - value[0];
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

static void measure_residencies(int devid, struct residencies *res)
{
	res->rc6 = read_rc6_residency("rc6");
	if (IS_VALLEYVIEW(devid) || IS_CHERRYVIEW(devid))
		res->media_rc6 = read_rc6_residency("media_rc6");

	if (IS_GEN6(devid) || IS_IVYBRIDGE(devid)) {
		res->rc6p = read_rc6_residency("rc6p");
		res->rc6pp = read_rc6_residency("rc6pp");
	}
}

igt_main
{
	int fd;
	int devid = 0;
	struct residencies res;

	igt_skip_on_simulation();

	/* Use drm_open_any to verify device existence */
	igt_fixture {
		fd = drm_open_any();
		devid = intel_get_drm_devid(fd);
		close(fd);

		measure_residencies(devid, &res);
	}

	igt_subtest("rc6-accuracy")
		residency_accuracy(res.rc6, "rc6");
	igt_subtest("media-rc6-accuracy")
		if (IS_VALLEYVIEW(devid) || IS_CHERRYVIEW(devid))
			residency_accuracy(res.media_rc6, "media_rc6");
	igt_subtest("rc6p-accuracy") {
		if (!IS_GEN6(devid) && !IS_IVYBRIDGE(devid))
			igt_skip("This platform doesn't support RC6p\n");
		residency_accuracy(res.rc6p, "rc6p");
	}
	igt_subtest("rc6pp-accuracy") {
		if (!IS_GEN6(devid) && !IS_IVYBRIDGE(devid))
			igt_skip("This platform doesn't support RC6pp\n");
		residency_accuracy(res.rc6pp, "rc6pp");
	}
}
