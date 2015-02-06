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

/*
 * Negative test cases for destroy contexts
  */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"

IGT_TEST_DESCRIPTION("Negative test cases for destroy contexts.");

igt_simple_main
{
	uint32_t ctx_id;
	int fd;

	igt_skip_on_simulation();

	fd = drm_open_any_render();

	ctx_id = gem_context_create(fd);

	/* Make sure a proper destroy works first */
	gem_context_destroy(fd, ctx_id);

	/* try double destroy */
	igt_assert(__gem_context_destroy(fd, ctx_id) == -ENOENT);

	/* destroy something random */
	igt_assert(__gem_context_destroy(fd, 2) == -ENOENT);

	/* Try to destroy the default context */
	igt_assert(__gem_context_destroy(fd, 0) == -ENOENT);

	close(fd);
}
