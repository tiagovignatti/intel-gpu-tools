#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#include "power.h"

/* XXX Is this exposed through RAPL? */

int power_init(struct power *power)
{
	char buf[4096];
	int fd, len;

	memset(power, 0, sizeof(*power));

	fd = open("/sys/kernel/debug/dri/0/i915_energy_uJ", 0);
	if (fd < 0)
		return power->error = errno;

	len = read(fd, buf, sizeof(buf));
	close(fd);

	if (len < 0)
		return power->error = errno;

	return 0;
}

static uint64_t file_to_u64(const char *path)
{
	char buf[4096];
	int fd, len;

	fd = open(path, 0);
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

	s->energy = file_to_u64("/sys/kernel/debug/dri/0/i915_energy_uJ");
	s->timestamp = clock_ms_to_u64();
	if (power->count == 1)
		return EAGAIN;

	d_time = s->timestamp - d->timestamp;
	if (d_time < 900) { /* HW sample rate seems to be stable ~1Hz */
		power->count--;
		return power->count <= 1 ? EAGAIN : 0;
	}

	power->power_mW = (s->energy - d->energy) / d_time;
	power->new_sample = 1;
	return 0;
}
