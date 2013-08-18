#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "cpu-top.h"

int cpu_top_update(struct cpu_top *cpu)
{
	struct cpu_stat *s = &cpu->stat[cpu->count++&1];
	struct cpu_stat *d = &cpu->stat[cpu->count&1];
	uint64_t d_total, d_idle;
	char buf[4096];
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

	s->total = s->user + s->nice + s->sys + s->idle;
	if (cpu->count == 1)
		return EAGAIN;

	d_total = s->total - d->total;
	d_idle = s->idle - d->idle;
	cpu->busy = 100 - 100 * d_idle / d_total;

	return 0;
}
