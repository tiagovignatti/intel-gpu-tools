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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gem-interrupts.h"
#include "debugfs.h"
#include "perf.h"

static int perf_open(void)
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof (attr));

	attr.type = i915_type_id();
	if (attr.type == 0)
		return -ENOENT;
	attr.config = I915_PERF_INTERRUPTS;

	return perf_event_open(&attr, -1, 0, -1, 0);
}

static int debugfs_open(void)
{
	char buf[1024];
	struct stat st;

	sprintf(buf, "%s/i915_gem_interrupt", debugfs_dri_path);
	if (stat(buf, &st))
		return errno;

	return 0;
}

int gem_interrupts_init(struct gem_interrupts *irqs)
{
	memset(irqs, 0, sizeof(*irqs));

	irqs->fd = perf_open();
	if (irqs->fd < 0)
		irqs->error = debugfs_open();

	return irqs->error;
}

int gem_interrupts_update(struct gem_interrupts *irqs)
{
	uint64_t val;
	int update;

	if (irqs->error)
		return irqs->error;

	if (irqs->fd < 0) {
		char buf[8192], *b;
		int fd, len;

		sprintf(buf, "%s/i915_gem_interrupt", debugfs_dri_path);
		fd = open(buf, 0);
		if (fd < 0)
			return irqs->error = errno;
		len = read(fd, buf, sizeof(buf)-1);
		close(fd);

		if (len < 0)
			return irqs->error = errno;

		buf[len] = '\0';

		b = strstr(buf, "Interrupts received:");
		if (b == NULL)
			return irqs->error = ENOENT;

		val = strtoull(b + sizeof("Interrupts received:"), 0, 0);
	} else {
		if (read(irqs->fd, &val, sizeof(val)) < 0)
			return irqs->error = errno;
	}

	update = irqs->last_count == 0;
	irqs->last_count = irqs->count;
	irqs->count = val;
	irqs->delta = irqs->count - irqs->last_count;
	return update ? EAGAIN : 0;
}
