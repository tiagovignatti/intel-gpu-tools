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
#include "i915_drm.h"
#include "drmtest.h"

struct local_drm_i915_context_destroy {
	__u32 ctx_id;
	__u32 pad;
};

#define CONTEXT_DESTROY_IOCTL DRM_IOWR(DRM_COMMAND_BASE + 0x2e, struct local_drm_i915_context_destroy)

static void handle_bad(int ret, int lerrno, int expected, const char *desc)
{
	if (ret != 0 && lerrno != expected) {
		fprintf(stderr, "%s - errno was %d, but should have been %d\n",
				desc, lerrno, expected);
		igt_fail(1);
	} else if (ret == 0) {
		fprintf(stderr, "%s - Command succeeded, but should have failed\n",
			desc);
		igt_fail(1);
	}
}

int main(int argc, char *argv[])
{
	struct local_drm_i915_context_destroy destroy;
	uint32_t ctx_id;
	int ret, fd;

	igt_skip_on_simulation();

	fd = drm_open_any_render();

	ctx_id = gem_context_create(fd);

	destroy.ctx_id = ctx_id;
	/* Make sure a proper destroy works first */
	ret = drmIoctl(fd, CONTEXT_DESTROY_IOCTL, &destroy);
	igt_assert(ret == 0);

	/* try double destroy */
	ret = drmIoctl(fd, CONTEXT_DESTROY_IOCTL, &destroy);
	handle_bad(ret, errno, ENOENT, "double destroy");

	/* destroy something random */
	destroy.ctx_id = 2;
	ret = drmIoctl(fd, CONTEXT_DESTROY_IOCTL, &destroy);
	handle_bad(ret, errno, ENOENT, "random destroy");

	/* Try to destroy the default context */
	destroy.ctx_id = 0;
	ret = drmIoctl(fd, CONTEXT_DESTROY_IOCTL, &destroy);
	handle_bad(ret, errno, ENOENT, "default destroy");

	close(fd);

	igt_success();
}
