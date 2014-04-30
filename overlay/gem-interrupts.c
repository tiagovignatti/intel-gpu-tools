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
#include <ctype.h>

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

static long long debugfs_read(void)
{
	char buf[8192], *b;
	int fd, len;

	sprintf(buf, "%s/i915_gem_interrupt", debugfs_dri_path);
	fd = open(buf, 0);
	if (fd < 0)
		return -1;

	len = read(fd, buf, sizeof(buf)-1);
	close(fd);

	if (len < 0)
		return -1;

	buf[len] = '\0';

	b = strstr(buf, "Interrupts received:");
	if (b == NULL)
		return -1;

	return strtoull(b + sizeof("Interrupts received:"), 0, 0);
}

static long long procfs_read(void)
{
	char buf[8192], *b;
	int fd, len;
	unsigned long long val;

/* 44:         51      42446          0          0   PCI-MSI-edge      i915*/
	fd = open("/proc/interrupts", 0);
	if (fd < 0)
		return -1;

	len = read(fd, buf, sizeof(buf)-1);
	close(fd);

	if (len < 0)
		return -1;

	buf[len] = '\0';

	b = strstr(buf, "i915");
	if (b == NULL)
		return -1;
	while (*--b != ':')
		;

	val = 0;
	do {
		while (isspace(*++b))
			;
		if (!isdigit(*b))
			break;

		val += strtoull(b, &b, 0);
	} while(1);

	return val;
}

static long long interrupts_read(void)
{
	long long val;

	val = debugfs_read();
	if (val < 0)
		val = procfs_read();
	return val;
}

int gem_interrupts_init(struct gem_interrupts *irqs)
{
	memset(irqs, 0, sizeof(*irqs));

	irqs->fd = perf_open();
	if (irqs->fd < 0 && interrupts_read() < 0)
		irqs->error = ENODEV;

	return irqs->error;
}

int gem_interrupts_update(struct gem_interrupts *irqs)
{
	uint64_t val;
	int update;

	if (irqs->error)
		return irqs->error;

	if (irqs->fd < 0) {
		val = interrupts_read();
		if (val < 0)
			return irqs->error = ENODEV;
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
