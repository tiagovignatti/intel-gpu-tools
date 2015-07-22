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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <drm.h>
#include <xf86drm.h>
#include "drmtest.h"
#include "assert.h"

static double elapsed(const struct timespec *start,
		      const struct timespec *end,
		      int loop)
{
	return (1e6*(end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec)/1000)/loop;
}

static int crtc0_active(int fd)
{
	union drm_wait_vblank vbl;

	memset(&vbl, 0, sizeof(vbl));
	vbl.request.type = DRM_VBLANK_RELATIVE;
	return drmIoctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl) == 0;
}

static void vblank_query(int fd, int busy)
{
	union drm_wait_vblank vbl;
	struct timespec start, end;
	unsigned long seq, count = 0;
	struct drm_event_vblank event;

	memset(&vbl, 0, sizeof(vbl));

	if (busy) {
		vbl.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT;
		vbl.request.sequence = 120 + 12;
		drmIoctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
	}

	vbl.request.type = DRM_VBLANK_RELATIVE;
	vbl.request.sequence = 0;
	drmIoctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
	seq = vbl.reply.sequence;

	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		vbl.request.type = DRM_VBLANK_RELATIVE;
		vbl.request.sequence = 0;
		drmIoctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
		count++;
	} while ((vbl.reply.sequence - seq) <= 120);
	clock_gettime(CLOCK_MONOTONIC, &end);

	printf("%f\n", 1e6/elapsed(&start, &end, count));
	if (busy)
		assert(read(fd, &event, sizeof(event)) != -1);
}

static void vblank_event(int fd, int busy)
{
	union drm_wait_vblank vbl;
	struct timespec start, end;
	unsigned long seq, count = 0;
	struct drm_event_vblank event;

	memset(&vbl, 0, sizeof(vbl));

	if (busy) {
		vbl.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT;
		vbl.request.sequence = 120 + 12;
		drmIoctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
	}

	vbl.request.type = DRM_VBLANK_RELATIVE;
	vbl.request.sequence = 0;
	drmIoctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
	seq = vbl.reply.sequence;

	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		vbl.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT;
		vbl.request.sequence = 0;
		drmIoctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);

		assert(read(fd, &event, sizeof(event)) != -1);
		count++;
	} while ((event.sequence - seq) <= 120);
	clock_gettime(CLOCK_MONOTONIC, &end);

	printf("%f\n", 1e6/elapsed(&start, &end, count));
	if (busy)
		assert(read(fd, &event, sizeof(event)) != -1);
}

int main(int argc, char **argv)
{
	int fd, c;
	int busy = 0, loops = 5;
	enum what { EVENTS, QUERIES } what = EVENTS;

	while ((c = getopt (argc, argv, "b:w:r:")) != -1) {
		switch (c) {
		case 'b':
			if (strcmp(optarg, "busy") == 0)
				busy = 1;
			else if (strcmp(optarg, "idle") == 0)
				busy = 0;
			else
				abort();
			break;

		case 'w':
			if (strcmp(optarg, "event") == 0)
				what = EVENTS;
			else if (strcmp(optarg, "query") == 0)
				what = QUERIES;
			else
				abort();
			break;
		case 'r':
			loops = atoi(optarg);
			if (loops < 1)
				loops = 1;
		}
	}

	fd = drm_open_driver(DRIVER_INTEL);
	if (!crtc0_active(fd)) {
		fprintf(stderr, "CRTC/pipe 0 not active\n");
		return 77;
	}

	while (loops--) {
		switch (what) {
		case EVENTS:
			vblank_event(fd, busy);
			break;
		case QUERIES:
			vblank_query(fd, busy);
			break;
		}
	}
	return 0;
}
