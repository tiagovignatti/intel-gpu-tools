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
#define RC6_FUDGE 900 // in milliseconds

static unsigned int readit(const char *path)
{
	unsigned int ret;
	int scanned;

	FILE *file;
	file = fopen(path, "r");
	igt_assert(file);
	scanned = fscanf(file, "%u", &ret);
	igt_assert(scanned == 1);

	fclose(file);

	return ret;
}

static void read_rc6_residency( int value[], const char *name_of_rc6_residency)
{
	unsigned int i;
	const int device = drm_get_card();
	char *path ;
	int  ret;
	FILE *file;

	/* For some reason my ivb isn't idle even after syncing up with the gpu.
	 * Let's add a sleept just to make it happy. */
	sleep(5);

	ret = asprintf(&path, "/sys/class/drm/card%d/power/rc6_enable", device);
	igt_assert(ret != -1);

	file = fopen(path, "r");
	igt_require(file);

	/* claim success if no rc6 enabled. */
	if (readit(path) == 0)
		igt_success();

	for(i = 0; i < 2; i++)
	{
		sleep(SLEEP_DURATION / 1000);
		ret = asprintf(&path, "/sys/class/drm/card%d/power/%s_residency_ms",device,name_of_rc6_residency);
		igt_assert(ret != -1);
		value[i] = readit(path);
	}
	free(path);
}

static void residency_accuracy(int value[],const char *name_of_rc6_residency)
{
	unsigned int flag_counter,flag_support;
	double counter_result = 0;
	unsigned int diff;
	unsigned int  tmp_counter, tmp_support;
	double counter;
	flag_counter = 0;
	flag_support = 0;
	diff = (value[1] - value[0]);

	igt_assert_f(diff <= (SLEEP_DURATION + RC6_FUDGE),"Diff was too high. That is unpossible\n");
	igt_assert_f(diff >= (SLEEP_DURATION - RC6_FUDGE),"GPU was not in RC6 long enough. Check that "
							"the GPU is as idle as possible(ie. no X, "
							"running and running no other tests)\n");

	counter = ((double)value[1] - (double)value[0]) /(double) SLEEP_DURATION;

	if( counter > 0.9 ){
		counter_result = counter;
		tmp_counter = 1;
	}
	else
		tmp_counter = 0;

	if( value [1] == 0){
		tmp_support = 0;
		igt_info("This machine/configuration doesn't support %s\n", name_of_rc6_residency);
	}
	else
		tmp_support = 1;

	flag_counter = flag_counter + tmp_counter;
	flag_counter = flag_counter << 1;

	flag_support = flag_support + tmp_support;
	flag_support = flag_support << 1;

	igt_info("The residency counter: %f \n", counter_result);
	igt_skip_on_f(flag_support == 0 , "This machine didn't entry %s state.\n", name_of_rc6_residency);
	igt_assert_f((flag_counter != 0) && (counter_result <=1) , "Sysfs RC6 residency counter is inaccurate.\n");
	igt_info("This machine entry %s state.\n", name_of_rc6_residency);
}

igt_main
{
	int fd;
	int devid = 0;
	int rc6[2], rc6p[2], rc6pp[2];

	igt_skip_on_simulation();

	/* Use drm_open_any to verify device existence */
	igt_fixture {
		fd = drm_open_any();
		devid = intel_get_drm_devid(fd);
		close(fd);

		read_rc6_residency(rc6, "rc6");
		if (IS_GEN6(devid) || IS_IVYBRIDGE(devid)) {
			read_rc6_residency(rc6p, "rc6p");
			read_rc6_residency(rc6pp, "rc6pp");
		}
	}

	igt_subtest("rc6-accuracy")
		residency_accuracy(rc6, "rc6");
	igt_subtest("rc6p-accuracy") {
		if (!IS_GEN6(devid) && !IS_IVYBRIDGE(devid))
			igt_skip("This platform doesn't support RC6p\n");
		residency_accuracy(rc6p, "rc6p");
	}
	igt_subtest("rc6pp-accuracy") {
		if (!IS_GEN6(devid) && !IS_IVYBRIDGE(devid))
			igt_skip("This platform doesn't support RC6pp\n");
		residency_accuracy(rc6pp, "rc6pp");
	}
}
