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
 *    Derek Morton <derek.j.morton@intel.com>
 *
 */

/*
 * Testcase: Test the framework catches a segfault and returns an error.
 *
 * 1. Test a crashing simple test is reported.
 * 2. Test a crashing subtest is reported.
 * 3. Test a crashing subtest following a passing subtest is reported.
 * 4. Test a crashing subtest preceeding a passing subtest is reported.
 */

#include <signal.h>
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
bool runa;
bool runc;
char test[] = "test";
char *argv_run[] = { test };

static void crashme(void)
{
	raise(SIGSEGV);
}

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
			crashme();

			igt_exit();
		} else {

			argc = 1;
			igt_subtest_init(argc, argv_run);

			if(runa)
				igt_subtest("A")
					;

			igt_subtest("B")
				crashme();

			if(runc)
				igt_subtest("C")
					;

			igt_exit();
		}
	default:
		while (waitpid(pid, &status, 0) == -1 &&
		       errno == EINTR)
			;

		if(WIFSIGNALED(status))
			return WTERMSIG(status) + 128;

		return WEXITSTATUS(status);
	}
}

int main(int argc, char **argv)
{
	/* Test Crash in simple test is reported */
	simple = true;
	runa=false;
	runc=false;
	igt_info("Simple test.\n");
	fflush(stdout);
	internal_assert(do_fork() == SIGSEGV + 128);

	/* Test crash in a single subtest is reported */
	simple = false;
	igt_info("Single subtest.\n");
	fflush(stdout);
	internal_assert(do_fork() == SIGSEGV + 128);

	/* Test crash in a subtest following a pass is reported */
	simple = false;
	runa=true;
	igt_info("Passing then crashing subtest.\n");
	fflush(stdout);
	internal_assert(do_fork() == SIGSEGV + 128);

	/* Test crash in a subtest preceeding a pass is reported */
	simple = false;
	runa=false;
	runc=true;
	igt_info("Crashing then passing subtest.\n");
	fflush(stdout);
	internal_assert(do_fork() == SIGSEGV + 128);

	return 0;
}

