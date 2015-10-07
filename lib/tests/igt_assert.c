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
 */

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "igt_core.h"

/*
 * We need to hide assert from the cocci igt test refactor spatch.
 *
 * IMPORTANT: Test infrastructure tests are the only valid places where using
 * assert is allowed.
 */
#define internal_assert assert

char test[] = "test";
char *argv_run[] = { test };
void (*test_to_run)(void) = NULL;

/*
 * A really tedious way of making sure we execute every negative test, and that
 * they all really fail.
 */
#define CHECK_NEG(x) { \
	igt_subtest_f("XFAIL_simple_%d", __LINE__) { \
		(*exec_before)++; \
		x; \
		raise(SIGBUS); \
	} \
	exec_total++; \
}

static int do_fork(void)
{
	int pid, status;
	int argc;

	switch (pid = fork()) {
	case -1:
		internal_assert(0);
	case 0:
		argc = 1;
		igt_simple_init(argc, argv_run);
		test_to_run();
		igt_exit();
	default:
		while (waitpid(pid, &status, 0) == -1 &&
		       errno == EINTR)
			;

		if(WIFSIGNALED(status))
			return WTERMSIG(status) + 128;

		return WEXITSTATUS(status);
	}
}

static void test_cmpint_negative(void)
{
	int *exec_before = calloc(1, sizeof(int));
	int exec_total = 0;

	CHECK_NEG(igt_assert_eq(INT_MIN, INT_MAX));

	CHECK_NEG(igt_assert_eq_u32(0xfffffffeUL, 0xffffffffUL));

	CHECK_NEG(igt_assert_eq_u64(0xfffeffffffffffffULL, 0xffffffffffffffffULL));
	CHECK_NEG(igt_assert_eq_u64(0xfffffffeffffffffULL, 0xffffffffffffffffULL));
	CHECK_NEG(igt_assert_eq_u64(0xfffffffffffeffffULL, 0xffffffffffffffffULL));

	CHECK_NEG(igt_assert_eq_double(0.0, DBL_MAX));
	CHECK_NEG(igt_assert_eq_double(DBL_MAX, nexttoward(DBL_MAX, 0.0)));

	if (*exec_before != exec_total)
		raise(SIGSEGV);
}

static void test_cmpint(void)
{
	igt_assert_eq(0, 0);
	igt_assert_eq(INT_MAX, INT_MAX);
	igt_assert_eq(INT_MAX, INT_MAX);
	igt_assert_neq(INT_MIN, INT_MAX);

	igt_assert_eq_u32(0, 0);
	igt_assert_eq_u32(0xffffffffUL, 0xffffffffUL);
	igt_assert_neq_u32(0xfffffffeUL, 0xffffffffUL);

	igt_assert_eq_u64(0, 0);
	igt_assert_eq_u64(0xffffffffffffffffULL, 0xffffffffffffffffULL);
	igt_assert_neq_u64(0xfffffffffffffffeULL, 0xffffffffffffffffULL);

	igt_assert_eq_double(0.0, 0.0);
	igt_assert_eq_double(DBL_MAX, DBL_MAX);
	igt_assert_neq_double(0.0, DBL_MAX);
}

static void test_fd_negative(void)
{
	int *exec_before = calloc(1, sizeof(int));
	int exec_total = 0;

	CHECK_NEG(igt_assert_fd(-1));
	CHECK_NEG(igt_assert_fd(INT_MIN));

	if (*exec_before != exec_total)
		raise(SIGSEGV);
}

static void test_fd(void)
{
	igt_assert_fd(0);
	igt_assert_fd(1);
	igt_assert_fd(INT_MAX);
}

igt_main
{
	int ret;

	igt_subtest("igt_cmpint")
		test_cmpint();

	/*
	 * The awkward subtest dance here is because we really want to use
	 * subtests in our negative tests, to ensure we actually execute all
	 * the subtests. But we can't begin a subtest within a subtest, and
	 * we inherit the state from the parent, so ...
	 */
	test_to_run = test_cmpint_negative;
	ret = do_fork();
	igt_subtest("igt_cmpint_negative")
		internal_assert(ret == IGT_EXIT_FAILURE);

	igt_subtest("igt_assert_fd")
		test_fd();

	test_to_run = test_fd_negative;
	ret = do_fork();
	igt_subtest("igt_assert_fd_negative")
		internal_assert(ret == IGT_EXIT_FAILURE);
}
