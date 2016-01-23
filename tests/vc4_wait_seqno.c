/*
 * Copyright © 2016 Broadcom
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
 */

#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "vc4_drm.h"

igt_main
{
	int fd;

	igt_fixture
		fd = drm_open_driver(DRIVER_VC4);

	/* A 64-bit seqno should never hit the maximum value over the
	 * lifetime of the system.  (A submit per 1000 cycles at 1Ghz
	 * would still take 584000 years).  As a result, we can wait
	 * for it and be sure of a timeout.
	 */
	igt_subtest("bad-seqno-0ns") {
		struct drm_vc4_wait_seqno arg = {
			.seqno = ~0ull,
			.timeout_ns = 0,
		};
		do_ioctl_err(fd, DRM_IOCTL_VC4_WAIT_SEQNO, &arg, ETIME);
	}

	igt_subtest("bad-seqno-1ns") {
		struct drm_vc4_wait_seqno arg = {
			.seqno = ~0ull,
			.timeout_ns = 1,
		};
		do_ioctl_err(fd, DRM_IOCTL_VC4_WAIT_SEQNO, &arg, ETIME);
	}

	igt_fixture
		close(fd);
}
