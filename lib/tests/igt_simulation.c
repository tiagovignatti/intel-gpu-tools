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

/*
 * We need to hide assert from the cocci igt test refactor spatch.
 *
 * IMPORTANT: Test infrastructure tests are the only valid places where using
 * assert is allowed.
 */
#define internal_assert assert

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
	int argc;

	switch (pid = fork()) {
	case -1:
		internal_assert(0);
	case 0:
		if (simple) {
			argc = 1;
			igt_simple_init(argc, argv_run);

			igt_skip_on_simulation();

			igt_exit();
		} else {
			if (list_subtests) {
				argc = 2;
				igt_subtest_init(argc, argv_list);
			} else {
				argc = 1;
				igt_subtest_init(argc, argv_run);
			}

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

		internal_assert(WIFEXITED(status));

		return WEXITSTATUS(status);
	}
}

int main(int argc, char **argv)
{
	/* simple tests */
	simple = true;
	internal_assert(setenv("INTEL_SIMULATION", "1", 1) == 0);
	internal_assert(do_fork() == IGT_EXIT_SKIP);

	internal_assert(setenv("INTEL_SIMULATION", "0", 1) == 0);
	internal_assert(do_fork() == IGT_EXIT_SUCCESS);

	/* subtests, list mode */
	simple = false;
	list_subtests = true;

	in_fixture = false;
	internal_assert(setenv("INTEL_SIMULATION", "1", 1) == 0);
	internal_assert(do_fork() == IGT_EXIT_SUCCESS);

	internal_assert(setenv("INTEL_SIMULATION", "0", 1) == 0);
	internal_assert(do_fork() == IGT_EXIT_SUCCESS);

	in_fixture = true;
	internal_assert(setenv("INTEL_SIMULATION", "1", 1) == 0);
	internal_assert(do_fork() == IGT_EXIT_SUCCESS);

	internal_assert(setenv("INTEL_SIMULATION", "0", 1) == 0);
	internal_assert(do_fork() == IGT_EXIT_SUCCESS);

	in_fixture = false;
	in_subtest = true;
	internal_assert(setenv("INTEL_SIMULATION", "1", 1) == 0);
	internal_assert(do_fork() == IGT_EXIT_SUCCESS);

	internal_assert(setenv("INTEL_SIMULATION", "0", 1) == 0);
	internal_assert(do_fork() == IGT_EXIT_SUCCESS);

	/* subtest, run mode */
	simple = false;
	list_subtests = false;

	in_fixture = false;
	internal_assert(setenv("INTEL_SIMULATION", "1", 1) == 0);
	internal_assert(do_fork() == IGT_EXIT_SKIP);

	internal_assert(setenv("INTEL_SIMULATION", "0", 1) == 0);
	internal_assert(do_fork() == IGT_EXIT_SUCCESS);

	in_fixture = true;
	internal_assert(setenv("INTEL_SIMULATION", "1", 1) == 0);
	internal_assert(do_fork() == IGT_EXIT_SKIP);

	internal_assert(setenv("INTEL_SIMULATION", "0", 1) == 0);
	internal_assert(do_fork() == IGT_EXIT_SUCCESS);

	in_fixture = false;
	in_subtest = true;
	internal_assert(setenv("INTEL_SIMULATION", "1", 1) == 0);
	internal_assert(do_fork() == IGT_EXIT_SKIP);

	internal_assert(setenv("INTEL_SIMULATION", "0", 1) == 0);
	internal_assert(do_fork() == IGT_EXIT_SUCCESS);

	return 0;
}
