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

#include <stdio.h>
#include <string.h>
#include "i915_drm.h"
#include "drmtest.h"

struct local_drm_i915_gem_context_create {
	__u32 ctx_id;
	__u32 pad;
};

#define CONTEXT_CREATE_IOCTL DRM_IOWR(DRM_COMMAND_BASE + 0x2d, struct local_drm_i915_gem_context_create)

int main(int argc, char *argv[])
{
	int ret, fd;
	struct local_drm_i915_gem_context_create create;

	create.ctx_id = rand();
	create.pad = rand();

	fd = drm_open_any();

	ret = drmIoctl(fd, CONTEXT_CREATE_IOCTL, &create);
	if (ret != 0 && (errno == ENODEV || errno == EINVAL)) {
		fprintf(stderr, "Kernel is too old, or contexts not supported: %s\n",
			strerror(errno));
		exit(77);
	} else if (ret != 0) {
		fprintf(stderr, "%s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	assert(create.ctx_id != 0);

	close(fd);

	exit(EXIT_SUCCESS);
}
