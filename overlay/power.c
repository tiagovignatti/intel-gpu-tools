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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#include "perf.h"
#include "power.h"
#include "debugfs.h"

/* XXX Is this exposed through RAPL? */

static int perf_open(void)
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof (attr));

	attr.type = i915_type_id();
	if (attr.type == 0)
		return -ENOENT;
	attr.config = I915_PERF_ENERGY;

	attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED;
	return perf_event_open(&attr, -1, 0, -1, 0);
}

int power_init(struct power *power)
{
	char buf[4096];
	int fd, len;

	memset(power, 0, sizeof(*power));

	power->fd = perf_open();
	if (power->fd != -1)
		return 0;

	sprintf(buf, "%s/i915_energy_uJ", debugfs_dri_path);
	fd = open(buf, 0);
	if (fd < 0)
		return power->error = errno;

	len = read(fd, buf, sizeof(buf));
	close(fd);

	if (len < 0)
		return power->error = errno;

	buf[len] = '\0';
	if (strtoull(buf, 0, 0) == 0)
		return power->error = EINVAL;

	return 0;
}

static uint64_t file_to_u64(const char *name)
{
	char buf[4096];
	int fd, len;

	sprintf(buf, "%s/%s", debugfs_dri_path, name);
	fd = open(buf, 0);
	if (fd < 0)
		return 0;

	len = read(fd, buf, sizeof(buf)-1);
	close(fd);

	if (len < 0)
		return 0;

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

int power_update(struct power *power)
{
	struct power_stat *s = &power->stat[power->count++&1];
	struct power_stat *d = &power->stat[power->count&1];
	uint64_t d_time;

	if (power->error)
		return power->error;

	if (power->fd != -1) {
		uint64_t data[2];
		int len;

		len = read(power->fd, data, sizeof(data));
		if (len < 0)
			return power->error = errno;

		s->energy = data[0];
		s->timestamp = data[1] / (1000*1000);
	} else {
		s->energy = file_to_u64("i915_energy_uJ");
		s->timestamp = clock_ms_to_u64();
	}

	if (power->count == 1)
		return EAGAIN;

	d_time = s->timestamp - d->timestamp;
	power->power_mW = (s->energy - d->energy) / d_time;
	power->new_sample = 1;
	return 0;
}
