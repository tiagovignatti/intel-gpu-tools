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
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <assert.h>
#include <errno.h>

#include "drmtest.h"
#include "igt_core.h"

bool simple;
bool list_subtests;
bool in_fixture;
bool in_subtest;

char test[] = "test";
char list[] = "--list-subtests";
char *argv_list[] = { test, list };
char *argv_run[] = { test };

static int do_fork(void)
{
	int pid, status;

	switch (pid = fork()) {
	case -1:
		assert(0);
	case 0:
		if (simple) {
			igt_simple_init();

			igt_skip_on_simulation();

			exit(0);
		} else {
			if (list_subtests)
				igt_subtest_init(2, argv_list);
			else
				igt_subtest_init(1, argv_run);

			if (in_fixture) {
				igt_fixture
					igt_skip_on_simulation();
			} if (in_subtest) {
				igt_subtest("sim")
					igt_skip_on_simulation();
			} else
				igt_skip_on_simulation();

			if (!in_subtest)
				igt_subtest("foo")
					;

			igt_exit();
		}
	default:
		while (waitpid(pid, &status, 0) == -1 &&
		       errno == EINTR)
			;

		assert(WIFEXITED(status));

		return WEXITSTATUS(status);
	}
}

int main(int argc, char **argv)
{
	/* simple tests */
	simple = true;
	assert(setenv("INTEL_SIMULATION", "1", 1) == 0);
	assert(do_fork() == IGT_EXIT_SKIP);

	assert(setenv("INTEL_SIMULATION", "0", 1) == 0);
	assert(do_fork() == IGT_EXIT_SUCCESS);

	/* subtests, list mode */
	simple = false;
	list_subtests = true;

	in_fixture = false;
	assert(setenv("INTEL_SIMULATION", "1", 1) == 0);
	assert(do_fork() == IGT_EXIT_SUCCESS);

	assert(setenv("INTEL_SIMULATION", "0", 1) == 0);
	assert(do_fork() == IGT_EXIT_SUCCESS);

	in_fixture = true;
	assert(setenv("INTEL_SIMULATION", "1", 1) == 0);
	assert(do_fork() == IGT_EXIT_SUCCESS);

	assert(setenv("INTEL_SIMULATION", "0", 1) == 0);
	assert(do_fork() == IGT_EXIT_SUCCESS);

	in_fixture = false;
	in_subtest = true;
	assert(setenv("INTEL_SIMULATION", "1", 1) == 0);
	assert(do_fork() == IGT_EXIT_SUCCESS);

	assert(setenv("INTEL_SIMULATION", "0", 1) == 0);
	assert(do_fork() == IGT_EXIT_SUCCESS);

	/* subtest, run mode */
	simple = false;
	list_subtests = false;

	in_fixture = false;
	assert(setenv("INTEL_SIMULATION", "1", 1) == 0);
	assert(do_fork() == IGT_EXIT_SKIP);

	assert(setenv("INTEL_SIMULATION", "0", 1) == 0);
	assert(do_fork() == IGT_EXIT_SUCCESS);

	in_fixture = true;
	assert(setenv("INTEL_SIMULATION", "1", 1) == 0);
	assert(do_fork() == IGT_EXIT_SKIP);

	assert(setenv("INTEL_SIMULATION", "0", 1) == 0);
	assert(do_fork() == IGT_EXIT_SUCCESS);

	in_fixture = false;
	in_subtest = true;
	assert(setenv("INTEL_SIMULATION", "1", 1) == 0);
	assert(do_fork() == IGT_EXIT_SKIP);

	assert(setenv("INTEL_SIMULATION", "0", 1) == 0);
	assert(do_fork() == IGT_EXIT_SUCCESS);

	return 0;
}
