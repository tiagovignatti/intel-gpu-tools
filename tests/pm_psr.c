/*
 * Copyright (c) 2013 Intel Corporation
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
 *    Rodrigo Vivi <rodrigo.vivi@intel.com>
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "drmtest.h"

#define SLEEP_DURATION 5000 // in milliseconds

static int get_perf(const char *path)
{
	int ret, perf;
	bool sink, source, enabled;
	FILE *file;
	char str[4];

	file = fopen(path, "r");
	if (file == NULL) {
		fprintf(stderr, "Couldn't open %s (%d)\n", path, errno);
		abort();
	}

	ret = fscanf(file, "Sink_Support: %s\n", str);
	if (ret == 0)
	    igt_skip("i915_edp_psr_status format not supported by this test case\n");
	sink = strcmp(str, "yes") == 0;
	ret = fscanf(file, "Source_OK: %s\n", str);
	igt_assert(ret != 0);
	source = strcmp(str, "yes") == 0;
	ret = fscanf(file, "Enabled: %s\n", str);
	igt_assert(ret != 0);
	enabled = strcmp(str, "yes") == 0;
	ret = fscanf(file, "Performance_Counter: %i", &perf);
	igt_assert(ret != 0);

	if (!sink)
	    igt_skip("This panel does not support PSR.\n");

	if (!source)
	    igt_skip("This Hardware does not support or isn't ready for PSR\n");

	if (!enabled) {
	    fprintf(stderr, "PSR should be enabled\n");
	    igt_fail(1);
	}

	if (perf == 0) {
	    fprintf(stderr, "PSR state never achieved\n");
	    igt_fail(1);
	}

	fclose(file);
	return perf;
}

igt_simple_main
{
	int ret, perf1, perf2;
	int device = drm_get_card();
	char *path;

	igt_skip_on_simulation();

	ret = asprintf(&path, "/sys/kernel/debug/dri/%d/i915_edp_psr_status", device);
	igt_assert(ret != -1);

	perf1 = get_perf(path);
	sleep(SLEEP_DURATION / 1000);
	perf2 = get_perf(path);

	if (perf1 == perf2) {
	    fprintf(stderr, "Unable to enter PSR state again\n");
	    igt_fail(1);
	}
}
