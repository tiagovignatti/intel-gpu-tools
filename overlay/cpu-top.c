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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "cpu-top.h"

int cpu_top_init(struct cpu_top *cpu)
{
	memset(cpu, 0, sizeof(*cpu));

	cpu->nr_cpu = sysconf(_SC_NPROCESSORS_ONLN);

	return 0;
}

int cpu_top_update(struct cpu_top *cpu)
{
	struct cpu_stat *s = &cpu->stat[cpu->count++&1];
	struct cpu_stat *d = &cpu->stat[cpu->count&1];
	uint64_t d_total, d_idle;
	char buf[4096], *b;
	int fd, len = -1;

	fd = open("/proc/stat", 0);
	if (fd < 0)
		return errno;

	len = read(fd, buf, sizeof(buf)-1);
	close(fd);

	if (len < 0)
		return EIO;
	buf[len] = '\0';

#ifdef __x86_64__
	sscanf(buf, "cpu %lu %lu %lu %lu",
	       &s->user, &s->nice, &s->sys, &s->idle);
#else
	sscanf(buf, "cpu %llu %llu %llu %llu",
	       &s->user, &s->nice, &s->sys, &s->idle);
#endif

	b = strstr(buf, "procs_running");
	if (b)
		cpu->nr_running = atoi(b+sizeof("procs_running")) - 1;

	s->total = s->user + s->nice + s->sys + s->idle;
	if (cpu->count == 1)
		return EAGAIN;

	d_total = s->total - d->total;
	d_idle = s->idle - d->idle;
	cpu->busy = 100 - 100 * d_idle / d_total;

	return 0;
}
