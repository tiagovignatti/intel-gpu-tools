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

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "gpu-freq.h"
#include "debugfs.h"
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

static int perf_open(void)
{
	int fd;

	fd = perf_i915_open(I915_PERF_ACTUAL_FREQUENCY, -1);
	if (perf_i915_open(I915_PERF_REQUESTED_FREQUENCY, fd) < 0) {
		close(fd);
		fd = -1;
	}

	return fd;
}

int gpu_freq_init(struct gpu_freq *gf)
{
	char buf[4096], *s;
	int fd, len = -1;

	memset(gf, 0, sizeof(*gf));

	gf->fd = perf_open();

	sprintf(buf, "%s/i915_frequency_info", debugfs_dri_path);
	fd = open(buf, 0);
	if (fd < 0) {
		sprintf(buf, "%s/i915_cur_delayinfo", debugfs_dri_path);
		fd = open(buf, 0);
	}
	if (fd < 0)
		return gf->error = errno;

	len = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if (len < 0)
		goto err;

	buf[len] = '\0';

	if (strstr(buf, "PUNIT_REG_GPU_FREQ_STS")) {
		/* Baytrail is special, ofc. */
		gf->is_byt = 1;
		s = strstr(buf, "max");
		if (s == NULL)
			goto err;
		sscanf(s, "max GPU freq: %d MHz", &gf->max);
		sscanf(s, "min GPU freq: %d MHz", &gf->min);

		gf->rp0 = gf->rp1 = gf->max;
		gf->rpn = gf->min;
	} else {
		s = strstr(buf, "(RPN)");
		if (s == NULL)
			goto err;
		sscanf(s, "(RPN) frequency: %dMHz", &gf->rpn);

		s = strstr(s, "(RP1)");
		if (s == NULL)
			goto err;
		sscanf(s, "(RP1) frequency: %dMHz", &gf->rp1);

		s = strstr(s, "(RP0)");
		if (s == NULL)
			goto err;
		sscanf(s, "(RP0) frequency: %dMHz", &gf->rp0);

		s = strstr(s, "Max");
		if (s == NULL)
			goto err;
		sscanf(s, "Max overclocked frequency: %dMHz", &gf->max);
		gf->min = gf->rpn;
	}

	return 0;

err:
	return gf->error = EIO;
}

int gpu_freq_update(struct gpu_freq *gf)
{
	if (gf->error)
		return gf->error;

	if (gf->fd < 0) {
		char buf[4096], *s;
		int fd, len = -1;

		sprintf(buf, "%s/i915_frequency_info", debugfs_dri_path);
		fd = open(buf, 0);
		if (fd < 0) {
			sprintf(buf, "%s/i915_cur_delayinfo", debugfs_dri_path);
			fd = open(buf, 0);
		}
		if (fd < 0)
			return gf->error = errno;

		len = read(fd, buf, sizeof(buf)-1);
		close(fd);
		if (len < 0)
			return gf->error = EIO;

		buf[len] = '\0';

		if (gf->is_byt) {
			s = strstr(buf, "current");
			if (s)
				sscanf(s, "current GPU freq: %d MHz", &gf->current);
			gf->request = gf->current;
		} else {
			s = strstr(buf, "RPNSWREQ:");
			if (s)
				sscanf(s, "RPNSWREQ: %dMHz", &gf->request);

			s = strstr(buf, "CAGF:");
			if (s)
				sscanf(s, "CAGF: %dMHz", &gf->current);
		}
	} else {
		struct gpu_freq_stat *s = &gf->stat[gf->count++&1];
		struct gpu_freq_stat *d = &gf->stat[gf->count&1];
		uint64_t data[4], d_time;
		int len;

		len = read(gf->fd, data, sizeof(data));
		if (len < 0)
			return gf->error = errno;

		s->timestamp = data[1];
		s->act = data[2];
		s->req = data[3];

		if (gf->count == 1)
			return EAGAIN;

		d_time = s->timestamp - d->timestamp;
		if (d_time == 0) {
			gf->count--;
			return EAGAIN;
		}

		gf->current = (s->act - d->act) / d_time;
		gf->request = (s->req - d->req) / d_time;
	}

	return 0;
}
