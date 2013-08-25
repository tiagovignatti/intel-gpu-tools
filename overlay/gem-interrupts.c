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

int gem_interrupts_init(struct gem_interrupts *irqs)
{
	char buf[1024];
	struct stat st;


	memset(irqs, 0, sizeof(*irqs));

	sprintf(buf, "%s/i915_gem_interrupt", debugfs_dri_path);
	if (stat(buf, &st))
		return irqs->error = errno;

	return 0;
}

int gem_interrupts_update(struct gem_interrupts *irqs)
{
	char buf[8192], *b;
	int fd, len;

	if (irqs->error)
		return irqs->error;

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

	fd = irqs->last_count == 0;
	irqs->last_count = irqs->count;
	irqs->count = strtoull(b + sizeof("Interrupts received:"), 0, 0);
	irqs->delta = irqs->count - irqs->last_count;

	return fd ? EAGAIN : 0;
}
