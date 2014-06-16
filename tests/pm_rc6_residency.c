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

#define NUMBER_OF_RC6_RESIDENCY 3
#define SLEEP_DURATION 3000 // in milliseconds
#define RC6_FUDGE 900 // in milliseconds


static unsigned int readit(const char *path)
{
	unsigned int ret;
	int scanned;

	FILE *file;
	file = fopen(path, "r");
	if (file == NULL) {
		fprintf(stderr, "Couldn't open %s (%d)\n", path, errno);
		abort();
	}
	scanned = fscanf(file, "%u", &ret);
	igt_assert(scanned == 1);

	fclose(file);

	return ret;
}

static void read_rc6_residency( int value[], const char *name_of_rc6_residency[])
{
	const int device = drm_get_card();
	char *path ;
	int  ret;
	FILE *file;

	/* For some reason my ivb isn't idle even after syncing up with the gpu.
	 * Let's add a sleept just to make it happy. */
	sleep(5);

	ret = asprintf(&path, "/sys/class/drm/card%d/power/rc6_enable", device);
	igt_assert(ret != -1);

	file = fopen(path, "r");//open
	igt_require(file);

	/* claim success if no rc6 enabled. */
	if (readit(path) == 0)
		igt_success();

	for(unsigned int i = 0; i < 6; i++)
	{
		if(i == 3)
			sleep(SLEEP_DURATION / 1000);
		ret = asprintf(&path, "/sys/class/drm/card%d/power/%s_residency_ms",device,name_of_rc6_residency[i % 3] );
		igt_assert(ret != -1);
		value[i] = readit(path);
	}
	free(path);
}

static void rc6_residency_counter(int value[],const char * name_of_rc6_residency[])
{
	unsigned int flag_counter,flag_support;
	double  counter_result = 0;
	flag_counter = 0;
	flag_support = 0;

	for(int flag = NUMBER_OF_RC6_RESIDENCY-1; flag >= 0 ; flag --)
	{
		unsigned int  tmp_counter, tmp_support;
		double counter;
		counter = ((double)value[flag + 3] - (double)value[flag]) /(double) SLEEP_DURATION;

		if( counter > 0.9 ){
			counter_result = counter;
			tmp_counter = 1;
		}
		else
			tmp_counter = 0;

		if( value [flag + 3] == 0){
			tmp_support = 0;
			printf("This machine doesn't support %s\n",name_of_rc6_residency[flag]);
		}
		else
			tmp_support = 1;

		flag_counter = flag_counter + tmp_counter;
		flag_counter = flag_counter << 1;

		flag_support = flag_support + tmp_support;
		flag_support = flag_support << 1;
	}

	printf("The residency counter : %f \n", counter_result);

	igt_assert_f(flag_counter != 0 , "The RC6 residency counter is not good.\n");
	igt_assert_f(flag_support != 0 , "This machine doesn't support any RC6 state!\n");
	igt_assert_f(counter_result <=1  , "Debug files must be wrong \n");

	printf("This machine entry %s state.\n", name_of_rc6_residency[(flag_counter / 2) - 1]);
}

static void rc6_residency_check(int value[])
{
	unsigned int diff;
	diff = (value[3] - value[0]) +
			(value[4] - value[1]) +
			(value[5] - value[2]);

	igt_assert_f(diff <= (SLEEP_DURATION + RC6_FUDGE),"Diff was too high. That is unpossible\n");
	igt_assert_f(diff >= (SLEEP_DURATION - RC6_FUDGE),"GPU was not in RC6 long enough. Check that "
							"the GPU is as idle as possible(ie. no X, "
							"running and running no other tests)\n");
}

igt_main
{
	int value[6];
	int fd;
	const char * name_of_rc6_residency[3]={"rc6","rc6p","rc6pp"};

	igt_skip_on_simulation();

	/* Use drm_open_any to verify device existence */
	fd = drm_open_any();
	close(fd);

	read_rc6_residency(value, name_of_rc6_residency);

	igt_subtest("rc6-residency-check")
		rc6_residency_check(value);

	igt_subtest("rc6-residency-counter")
		rc6_residency_counter(value, name_of_rc6_residency);

}
