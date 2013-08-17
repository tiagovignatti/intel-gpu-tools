#include <stdint.h>
#include <stdbool.h>
#include <linux/perf_event.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include "gpu-perf.h"

#if defined(__i386__)
#define rmb()           asm volatile("lock; addl $0,0(%%esp)" ::: "memory")
#endif

#if defined(__x86_64__)
#define rmb()           asm volatile("lfence" ::: "memory")
#endif

#define TRACING_EVENT_PATH "/sys/kernel/debug/tracing/events"
#define N_PAGES 32

struct sample_event {
	struct perf_event_header header;
	uint32_t pid, tid;
	uint64_t time;
	uint64_t id;
	uint32_t raw_size;
	uint32_t raw[0];
};

static int
perf_event_open(struct perf_event_attr *attr,
		pid_t pid,
		int cpu,
		int group_fd,
		unsigned long flags)
{
#ifndef __NR_perf_event_open
#if defined(__i386__)
#define __NR_perf_event_open 336
#elif defined(__x86_64__)
#define __NR_perf_event_open 298
#else
#define __NR_perf_event_open 0
#endif
#endif

    attr->size = sizeof(*attr);
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

static uint64_t tracepoint_id(const char *sys, const char *name)
{
	char buf[1024];
	int fd, n;

	snprintf(buf, sizeof(buf), "%s/%s/%s/id", TRACING_EVENT_PATH, sys, name);
	fd = open(buf, 0);
	if (fd < 0)
		return 0;
	n = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if (n < 0)
		return 0;

	buf[n] = '\0';
	return strtoull(buf, 0, 0);
}

static int perf_tracepoint_open(struct gpu_perf *gp,
				const char *sys, const char *name,
				int (*func)(struct gpu_perf *, const void *))
{
	struct perf_event_attr attr;
	struct gpu_perf_sample *sample;
	int n, *fd;

	memset(&attr, 0, sizeof (attr));

	attr.type = PERF_TYPE_TRACEPOINT;
	attr.config = tracepoint_id(sys, name);
	if (attr.config == 0)
		return ENOENT;

	attr.sample_period = 1;
	attr.sample_type = (PERF_SAMPLE_TIME | PERF_SAMPLE_STREAM_ID | PERF_SAMPLE_TID | PERF_SAMPLE_RAW);
	attr.read_format = PERF_FORMAT_ID;

	attr.exclude_guest = 1;
	attr.mmap = gp->nr_events == 0;
	attr.comm = gp->nr_events == 0;

	n = gp->nr_cpus * (gp->nr_events+1);
	fd = realloc(gp->fd, n*sizeof(int));
	sample = realloc(gp->sample, n*sizeof(*gp->sample));
	if (fd == NULL || sample == NULL)
		return ENOMEM;
	gp->fd = fd;
	gp->sample = sample;

	fd += gp->nr_events * gp->nr_cpus;
	sample += gp->nr_events * gp->nr_cpus;
	for (n = 0; n < gp->nr_cpus; n++) {
		uint64_t track[2];

		fd[n] = perf_event_open(&attr, -1, n, -1, 0);
		if (fd[n] == -1)
			return errno;

		/* read back the event to establish id->tracepoint */
		read(fd[n], track, sizeof(track));
		sample[n].id = track[1];
		sample[n].func = func;

		if (gp->nr_events)
			ioctl(fd[n], PERF_EVENT_IOC_SET_OUTPUT, gp->fd[n]);

	}

	gp->nr_events++;
	return 0;
}

static int perf_mmap(struct gpu_perf *gp)
{
	int size = (1 + N_PAGES) * gp->page_size;
	int n;

	gp->map = malloc(sizeof(void *)*gp->nr_cpus);
	if (gp->map == NULL)
		return ENOMEM;

	for (n = 0; n < gp->nr_cpus; n++) {
		gp->map[n] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, gp->fd[n], 0);
		if (gp->map[n] == (void *)-1)
			goto err;
	}

	return 0;

err:
	while (--n > 0)
		munmap(gp->map[n], size);
	free(gp->map);
	gp->map = NULL;
	return EINVAL;
}

static int seqno_start(struct gpu_perf *gp, const void *event)
{
	return 0;
}

static int seqno_end(struct gpu_perf *gp, const void *event)
{
	return 0;
}

static int flip_complete(struct gpu_perf *gp, const void *event)
{
	gp->flip_complete++;
	return 1;
}

void gpu_perf_init(struct gpu_perf *gp, unsigned flags)
{
	memset(gp, 0, sizeof(*gp));
	gp->nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	gp->page_size = getpagesize();

	if (perf_tracepoint_open(gp, "i915", "i915_gem_ring_complete", seqno_end) == 0)
		perf_tracepoint_open(gp, "i915", "i915_gem_ring_dispatch", seqno_start);

	perf_tracepoint_open(gp, "i915", "i915_flip_complete", flip_complete);

	if (gp->nr_events == 0)
		return;

	if (perf_mmap(gp))
		return;
}

static int process_sample(struct gpu_perf *gp,
			  const struct perf_event_header *header)
{
	const struct sample_event *sample = (const struct sample_event *)header;
	int n, update = 0;

	/* hash me! */
	for (n = 0; n < gp->nr_cpus * gp->nr_events; n++) {
		if (gp->sample[n].id != sample->id)
			continue;

		update = 1;
		if (gp->sample[n].func)
			update = gp->sample[n].func(gp, sample);
		break;
	}

	return update;
}

int gpu_perf_update(struct gpu_perf *gp)
{
	const int size = N_PAGES * gp->page_size;
	const int mask = size - 1;
	uint8_t *buffer = NULL;
	int buffer_size = 0;
	int n, update = 0;

	if (gp->map == NULL)
		return 0;

	for (n = 0; n < gp->nr_cpus; n++) {
		struct perf_event_mmap_page *mmap = gp->map[n];
		const uint8_t *data;
		uint64_t head, tail;

		tail = mmap->data_tail;
		head = mmap->data_head;
		rmb();

		if (head < tail)
			head += size;

		data = (uint8_t *)mmap + gp->page_size;
		while (head - tail >= sizeof (struct perf_event_header)) {
			const struct perf_event_header *header;

			header = (const struct perf_event_header *)(data + (tail & mask));
			if (header->size > head - tail)
				break;

			if ((const uint8_t *)header + header->size > data + size) {
				int before;

				if (header->size > buffer_size) {
					uint8_t *b = realloc(buffer, header->size);
					if (b == NULL)
						break;

					buffer = b;
					buffer_size = header->size;
				}

				before = data + size - (const uint8_t *)header;

				memcpy(buffer, header, before);
				memcpy(buffer + before, data, header->size - before);

				header = (struct perf_event_header *)buffer;
			}

			if (header->type == PERF_RECORD_SAMPLE)
				update += process_sample(gp, header);
			tail += header->size;
		}

		mmap->data_tail = tail & mask;
	}

	free(buffer);
	return update;
}
