/*
 * Copyright © 2013 Intel Corporation
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
 *
 */

#include "igt.h"

IGT_TEST_DESCRIPTION("Template test.");

/*
 * Note that test function (and code called by them) should generally not return
 * a variable indicating success/failure. Instead use the igt_require/igt_assert
 * macros to skip out of the entire subtest.
 *
 * Also, helper functions should only return a status code if the callers have a
 * real need to differentiate. If the only thing they do is call igt_assert or a
 * similar macro then it'll result in simpler code when the check is moved
 * completely into the helper.
 */
static void test_A(int fd)
{
}

static void test_B(int fd)
{
}

/*
 * Variables which are written to in igt_fixtures/subtest blocks need to be
 * allocated outside of the relevant function scope, otherwise gcc will wreak
 * havoc (since these magic blocks use setjmp/longjmp internally).
 *
 * Common practice is to put variables used in the main test function into
 * global scope, but only right above the main function itself (to avoid leaking
 * it into other functions).
 */

int drm_fd;

igt_main
{
	igt_fixture {
		drm_fd = drm_open_driver(DRIVER_INTEL);
		igt_require(drm_fd >= 0);

		/* Set up other interesting stuff shared by all tests. */
	}

	igt_subtest("A")
		test_A(drm_fd);
	igt_subtest("B")
		test_B(drm_fd);
	/*
	 * Note that subtest names can be programatically generated. See the
	 * various uses of igt_subtest_f for a few neat ideas.
	 */

	igt_fixture {
		close(drm_fd);
	}
}
