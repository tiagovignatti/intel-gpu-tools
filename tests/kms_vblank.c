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
 */

/** @file kms_vblank.c
 *
 * This is a test of performance of drmWaitVblank.
 */

#include "igt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <drm.h>

#include "intel_bufmgr.h"

IGT_TEST_DESCRIPTION("Test speed of WaitVblank.");

static double elapsed(const struct timespec *start,
		      const struct timespec *end,
		      int loop)
{
	return (1e6*(end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec)/1000)/loop;
}

static bool crtc0_active(int fd)
{
	union drm_wait_vblank vbl;

	memset(&vbl, 0, sizeof(vbl));
	vbl.request.type = DRM_VBLANK_RELATIVE;
	return drmIoctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl) == 0;
}

static void accuracy(int fd)
{
	union drm_wait_vblank vbl;
	unsigned long target;
	int n;

	memset(&vbl, 0, sizeof(vbl));

	vbl.request.type = DRM_VBLANK_RELATIVE;
	vbl.request.sequence = 1;
	do_ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);

	target = vbl.reply.sequence + 60;
	for (n = 0; n < 60; n++) {
		vbl.request.type = DRM_VBLANK_RELATIVE;
		vbl.request.sequence = 1;
		do_ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);

		vbl.request.type = DRM_VBLANK_ABSOLUTE | DRM_VBLANK_EVENT;
		vbl.request.sequence = target;
		do_ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
	}
	vbl.request.type = DRM_VBLANK_RELATIVE;
	vbl.request.sequence = 0;
	do_ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
	igt_assert_eq(vbl.reply.sequence, target);

	for (n = 0; n < 60; n++) {
		struct drm_event_vblank ev;
		igt_assert_eq(read(fd, &ev, sizeof(ev)), sizeof(ev));
		igt_assert_eq(ev.sequence, target);
	}
}

static void vblank_query(int fd, bool busy)
{
	union drm_wait_vblank vbl;
	struct timespec start, end;
	unsigned long sq, count = 0;
	struct drm_event_vblank buf;

	memset(&vbl, 0, sizeof(vbl));

	if (busy) {
		vbl.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT;
		vbl.request.sequence = 72;
		do_ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
	}

	vbl.request.type = DRM_VBLANK_RELATIVE;
	vbl.request.sequence = 0;
	do_ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);

	sq = vbl.reply.sequence;

	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		vbl.request.type = DRM_VBLANK_RELATIVE;
		vbl.request.sequence = 0;
		do_ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
		count++;
	} while ((vbl.reply.sequence - sq) <= 60);
	clock_gettime(CLOCK_MONOTONIC, &end);

	igt_info("Time to query current counter (%s):		%7.3fµs\n",
		 busy ? "busy" : "idle", elapsed(&start, &end, count));

	if (busy)
		igt_assert_eq(read(fd, &buf, sizeof(buf)), sizeof(buf));
}

static void vblank_wait(int fd, bool busy)
{
	union drm_wait_vblank vbl;
	struct timespec start, end;
	unsigned long sq, count = 0;
	struct drm_event_vblank buf;

	memset(&vbl, 0, sizeof(vbl));

	if (busy) {
		vbl.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT;
		vbl.request.sequence = 72;
		do_ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
	}

	vbl.request.type = DRM_VBLANK_RELATIVE;
	vbl.request.sequence = 0;
	do_ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);

	sq = vbl.reply.sequence;

	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		vbl.request.type = DRM_VBLANK_RELATIVE;
		vbl.request.sequence = 1;
		do_ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
		count++;
	} while ((vbl.reply.sequence - sq) <= 60);
	clock_gettime(CLOCK_MONOTONIC, &end);

	igt_info("Time to wait for %ld/%d vblanks (%s):		%7.3fµs\n",
		 count, (int)(vbl.reply.sequence - sq),
		 busy ? "busy" : "idle",
		 elapsed(&start, &end, count));

	if (busy)
		igt_assert_eq(read(fd, &buf, sizeof(buf)), sizeof(buf));
}

igt_main
{
	int fd;

	igt_skip_on_simulation();

	igt_fixture {
		fd = drm_open_any();
		igt_require(crtc0_active(fd));
	}

	igt_subtest("accuracy")
		accuracy(fd);

	igt_subtest("query-idle")
		vblank_query(fd, false);

	igt_subtest("query-busy")
		vblank_query(fd, true);

	igt_subtest("wait-idle")
		vblank_wait(fd, false);

	igt_subtest("wait-busy")
		vblank_wait(fd, true);
}
