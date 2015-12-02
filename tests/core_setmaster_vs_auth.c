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
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 * Based upon a test program provided by Thomas Hellstrom <thellstrom@vmware.com>
 */

/*
 * Testcase: Check that drop/setMaster correctly transfer master state
 *
 * Test approach is only checking auth state (which is part of master state) by
 * trying to authenticate a client against the wrong master.
 */

#define _GNU_SOURCE
#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef __linux__
# include <sys/syscall.h>
#else
# include <pthread.h>
#endif

IGT_TEST_DESCRIPTION("Check that drop/setMaster correctly transfer master "
		     "state");

igt_simple_main
{
	int master1, master2, client;
	drm_magic_t magic;

	master1 = drm_open_driver(DRIVER_ANY);
	do_or_die(drmSetMaster(master1));

	/* Get an authentication magic from the first master */
	client = drm_open_driver(DRIVER_ANY);
	do_or_die(drmGetMagic(client, &magic));

	/* Open an fd an make it master */
	master2 = drm_open_driver(DRIVER_ANY);
	do_or_die(drmDropMaster(master1));
	do_or_die(drmSetMaster(master2));

	/* Auth shouldn't work since we're authenticating with a different
	 * master than the one we got the magic from. */
	igt_assert_neq(drmAuthMagic(master2, magic), 0);
	igt_assert_eq(errno, EINVAL);

	close(client);
	close(master2);
	close(master1);
}
