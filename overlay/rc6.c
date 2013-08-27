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
 */

#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#include "rc6.h"
#include "perf.h"

static int perf_i915_open(int config, int group)
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof (attr));

	attr.type = i915_type_id();
	if (attr.type == 0)
		return -ENOENT;
	attr.config = config;

	attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED;
	if (group == -1)
		attr.read_format |= PERF_FORMAT_GROUP;

	return perf_event_open(&attr, -1, 0, group, 0);
}

#define RC6	(1<<0)
#define RC6p	(1<<1)
#define RC6pp	(1<<2)

static int perf_open(unsigned *flags)
{
	int fd;

	fd = perf_i915_open(I915_PERF_RC6_RESIDENCY, -1);
	if (fd < 0)
		return -1;

	*flags |= RC6;
	if (perf_i915_open(I915_PERF_RC6p_RESIDENCY, fd) >= 0)
		*flags |= RC6p;

	if (perf_i915_open(I915_PERF_RC6pp_RESIDENCY, fd) >= 0)
		*flags |= RC6pp;

	return fd;
}

int rc6_init(struct rc6 *rc6)
{
	memset(rc6, 0, sizeof(*rc6));

	rc6->fd = perf_open(&rc6->flags);
	if (rc6->fd == -1) {
		struct stat st;
		if (stat("/sys/class/drm/card0/power", &st) < 0)
			return rc6->error = errno;
	}

	return 0;
}

static uint64_t file_to_u64(const char *path)
{
	char buf[4096];
	int fd, len;

	fd = open(path, 0);
	if (fd < 0)
		return -1;

	len = read(fd, buf, sizeof(buf)-1);
	close(fd);

	if (len < 0)
		return -1;

	buf[len] = '\0';

	return strtoull(buf, 0, 0);
}

static uint64_t clock_ms_to_u64(void)
{
	struct timespec tv;

	if (clock_gettime(CLOCK_MONOTONIC, &tv) < 0)
		return 0;

	return (uint64_t)tv.tv_sec * 1000 + tv.tv_nsec / 1000000;
}

int rc6_update(struct rc6 *rc6)
{
	struct rc6_stat *s = &rc6->stat[rc6->count++&1];
	struct rc6_stat *d = &rc6->stat[rc6->count&1];
	uint64_t d_time, d_rc6, d_rc6p, d_rc6pp;

	if (rc6->error)
		return rc6->error;

	if (rc6->fd == -1) {
		struct stat st;

		if (stat("/sys/class/drm/card0/power/rc6_residency_ms", &st) < 0)
			return rc6->error = ENOENT;

		s->rc6_residency = file_to_u64("/sys/class/drm/card0/power/rc6_residency_ms");
		s->rc6p_residency = file_to_u64("/sys/class/drm/card0/power/rc6p_residency_ms");
		s->rc6pp_residency = file_to_u64("/sys/class/drm/card0/power/rc6pp_residency_ms");
		s->timestamp = clock_ms_to_u64();
	} else {
		uint64_t data[5];
		int len;

		len = read(rc6->fd, data, sizeof(data));
		if (len < 0)
			return rc6->error = errno;

		s->timestamp = data[1] / (1000*1000);

		len = 2;
		if (rc6->flags & RC6)
			s->rc6_residency = data[len++];
		if (rc6->flags & RC6p)
			s->rc6p_residency = data[len++];
		if (rc6->flags & RC6pp)
			s->rc6pp_residency = data[len++];
	}

	if (rc6->count == 1)
		return EAGAIN;

	d_time = s->timestamp - d->timestamp;
	if (d_time == 0) {
		rc6->count--;
		return EAGAIN;
	}

	d_rc6 = s->rc6_residency - d->rc6_residency;
	rc6->rc6 = (100 * d_rc6 + d_time/2) / d_time;

	d_rc6p = s->rc6p_residency - d->rc6p_residency;
	rc6->rc6p = (100 * d_rc6p + d_time/2) / d_time;

	d_rc6pp = s->rc6pp_residency - d->rc6pp_residency;
	rc6->rc6pp = (100 * d_rc6pp + d_time/2) / d_time;

	rc6->rc6_combined = (100 * (d_rc6 + d_rc6p + d_rc6pp) + d_time/2) / d_time;
	return 0;
}
