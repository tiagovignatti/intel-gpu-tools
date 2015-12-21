/*
 * Copyright © 2015 Intel Corporation
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
 * Authors:
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

#define _GNU_SOURCE
#include <pthread.h>

#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "drm.h"

static int done;
static int fd;
static volatile uint32_t *timestamp_reg;

#define REG(x) (volatile uint32_t *)((volatile char *)igt_global_mmio + x)
#define REG_OFFSET(x) ((volatile char *)(x) - (volatile char *)igt_global_mmio)

static uint32_t read_timestamp_unlocked(void)
{
	return *timestamp_reg;
}
static uint32_t (*read_timestamp)(void) = read_timestamp_unlocked;

#ifdef __USE_XOPEN2K
static pthread_spinlock_t timestamp_lock;
static uint32_t read_timestamp_locked(void)
{
	uint32_t t;

	pthread_spin_lock(&timestamp_lock);
	t = *timestamp_reg;
	pthread_spin_unlock(&timestamp_lock);

	return t;
}
static int setup_timestamp_locked(void)
{
	if (pthread_spin_init(&timestamp_lock, 0))
		return 0;

	read_timestamp = read_timestamp_locked;
	return 1;
}
#else
static int setup_timestamp_locked(void)
{
	return 0;
}
#endif

struct consumer {
	pthread_t thread;

	int go;

	igt_stats_t latency;
	struct producer *producer;
};

struct producer {
	pthread_t thread;
	uint32_t ctx;
	struct {
		struct drm_i915_gem_exec_object2 exec[1];
		struct drm_i915_gem_execbuffer2 execbuf;
	} nop_dispatch;
	struct {
		struct drm_i915_gem_exec_object2 exec[2];
		struct drm_i915_gem_execbuffer2 execbuf;
	} workload_dispatch;
	struct {
		struct drm_i915_gem_exec_object2 exec[1];
		struct drm_i915_gem_relocation_entry reloc[1];
		struct drm_i915_gem_execbuffer2 execbuf;
	} latency_dispatch;

	pthread_mutex_t lock;
	pthread_cond_t p_cond, c_cond;
	uint32_t *last_timestamp;
	int wait;
	int complete;
	int done;
	igt_stats_t latency, dispatch;

	int nop;
	int nconsumers;
	struct consumer *consumers;
};

#define LOCAL_EXEC_NO_RELOC (1<<11)
#define COPY_BLT_CMD		(2<<29|0x53<<22|0x6)
#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)

#define WIDTH 1024
#define HEIGHT 1024

#define RCS_TIMESTAMP (0x2000 + 0x358)
#define BCS_TIMESTAMP (0x22000 + 0x358)
#define CYCLES_TO_NS(x) (80.*(x))
#define CYCLES_TO_US(x) (CYCLES_TO_NS(x)/1000.)

static uint32_t create_workload(int gen, int factor)
{
	const int has_64bit_reloc = gen >= 8;
	uint32_t handle = gem_create(fd, 4096);
	uint32_t *map = gem_mmap__cpu(fd, handle, 0, 4096, PROT_WRITE);
	int i = 0;

	while (factor--) {
		/* XY_SRC_COPY */
		map[i++] = COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB;
		if (has_64bit_reloc)
			map[i-1] += 2;
		map[i++] = 0xcc << 16 | 1 << 25 | 1 << 24 | (4*WIDTH);
		map[i++] = 0;
		map[i++] = HEIGHT << 16 | WIDTH;
		map[i++] = 0;
		if (has_64bit_reloc)
			map[i++] = 0;
		map[i++] = 0;
		map[i++] = 4096;
		map[i++] = 0;
		if (has_64bit_reloc)
			map[i++] = 0;
	}
	map[i++] = MI_BATCH_BUFFER_END;
	munmap(map, 4096);

	return handle;
}

static void setup_workload(struct producer *p, int gen,
			   uint32_t scratch,
			   uint32_t batch,
			   int factor)
{
	struct drm_i915_gem_execbuffer2 *eb;
	const int has_64bit_reloc = gen >= 8;
	struct drm_i915_gem_relocation_entry *reloc;
	int offset;

	reloc = calloc(sizeof(*reloc), 2*factor);

	p->workload_dispatch.exec[0].handle = scratch;
	p->workload_dispatch.exec[1].relocation_count = 2*factor;
	p->workload_dispatch.exec[1].relocs_ptr = (uintptr_t)reloc;
	p->workload_dispatch.exec[1].handle = batch;

	offset = 0;
	while (factor--) {
		reloc->offset = (offset+4) * sizeof(uint32_t);
		reloc->target_handle = scratch;
		reloc->read_domains = I915_GEM_DOMAIN_RENDER;
		reloc->write_domain = I915_GEM_DOMAIN_RENDER;
		reloc++;

		reloc->offset = (offset+7) * sizeof(uint32_t);
		if (has_64bit_reloc)
			reloc->offset += sizeof(uint32_t);
		reloc->target_handle = scratch;
		reloc->read_domains = I915_GEM_DOMAIN_RENDER;
		reloc++;

		offset += 8;
		if (has_64bit_reloc)
			offset += 2;
	}

	eb = memset(&p->workload_dispatch.execbuf, 0, sizeof(*eb));
	eb->buffers_ptr = (uintptr_t)p->workload_dispatch.exec;
	eb->buffer_count = 2;
	eb->flags = I915_EXEC_BLT | LOCAL_EXEC_NO_RELOC;
	eb->rsvd1 = p->ctx;
}

static void setup_latency(struct producer *p, int gen)
{
	struct drm_i915_gem_execbuffer2 *eb;
	const int has_64bit_reloc = gen >= 8;
	uint32_t handle;
	uint32_t *map;
	int i = 0;

	handle = gem_create(fd, 4096);
	if (gem_has_llc(fd))
		map = gem_mmap__cpu(fd, handle, 0, 4096, PROT_WRITE);
	else
		map = gem_mmap__gtt(fd, handle, 4096, PROT_WRITE);

	p->latency_dispatch.exec[0].relocation_count = 1;
	p->latency_dispatch.exec[0].relocs_ptr =
		(uintptr_t)p->latency_dispatch.reloc;
	p->latency_dispatch.exec[0].handle = handle;

	/* MI_STORE_REG_MEM */
	map[i++] = 0x24 << 23 | 1;
	if (has_64bit_reloc)
		map[i-1]++;
	map[i++] = REG_OFFSET(timestamp_reg);
	p->latency_dispatch.reloc[0].offset = i * sizeof(uint32_t);
	p->latency_dispatch.reloc[0].delta = 4000;
	p->latency_dispatch.reloc[0].target_handle = handle;
	p->latency_dispatch.reloc[0].read_domains = I915_GEM_DOMAIN_INSTRUCTION;
	p->latency_dispatch.reloc[0].write_domain = 0; /* We lie! */
	p->latency_dispatch.reloc[0].presumed_offset = 0;
	p->last_timestamp = &map[1000];
	map[i++] = 4000;
	if (has_64bit_reloc)
		map[i++] = 0;

	map[i++] = MI_BATCH_BUFFER_END;

	eb = memset(&p->latency_dispatch.execbuf, 0, sizeof(*eb));
	eb->buffers_ptr = (uintptr_t)p->latency_dispatch.exec;
	eb->buffer_count = 1;
	eb->flags = I915_EXEC_BLT | LOCAL_EXEC_NO_RELOC;
	eb->rsvd1 = p->ctx;
}

static uint32_t create_nop(void)
{
	uint32_t buf = MI_BATCH_BUFFER_END;
	uint32_t handle;

	handle = gem_create(fd, 4096);
	gem_write(fd, handle, 0, &buf, sizeof(buf));

	return handle;
}

static void setup_nop(struct producer *p, uint32_t batch)
{
	struct drm_i915_gem_execbuffer2 *eb;

	p->nop_dispatch.exec[0].handle = batch;

	eb = memset(&p->nop_dispatch.execbuf, 0, sizeof(*eb));
	eb->buffers_ptr = (uintptr_t)p->nop_dispatch.exec;
	eb->buffer_count = 1;
	eb->flags = I915_EXEC_BLT | LOCAL_EXEC_NO_RELOC;
	eb->rsvd1 = p->ctx;
}

static void measure_latency(struct producer *p, igt_stats_t *stats)
{
	gem_sync(fd, p->latency_dispatch.exec[0].handle);
	igt_stats_push(stats, read_timestamp() - *p->last_timestamp);
}

static void *producer(void *arg)
{
	struct producer *p = arg;
	int n;

	while (!done) {
		uint32_t start = read_timestamp();
		int batches;

		/* Control the amount of work we do, similar to submitting
		 * empty buffers below, except this time we will load the
		 * GPU with a small amount of real work - so there is a small
		 * period between execution and interrupts.
		 */
		gem_execbuf(fd, &p->workload_dispatch.execbuf);

		/* Submitting a set of empty batches has a two fold effect:
		 * - increases contention on execbuffer, i.e. measure dispatch
		 *   latency with number of clients.
		 * - generates lots of spurious interrupts (if someone is
		 *   waiting).
		 */
		batches = p->nop;
		while (batches--)
			gem_execbuf(fd, &p->nop_dispatch.execbuf);

		/* Finally, execute a batch that just reads the current
		 * TIMESTAMP so we can measure the latency.
		 */
		gem_execbuf(fd, &p->latency_dispatch.execbuf);

		/* Wake all the associated clients to wait upon our batch */
		p->wait = p->nconsumers;
		for (n = 0; n < p->nconsumers; n++)
			p->consumers[n].go = 1;
		pthread_cond_broadcast(&p->c_cond);

		/* Wait for this batch to finish and record how long we waited,
		 * and how long it took for the batch to be submitted
		 * (including the nop delays).
		 */
		measure_latency(p, &p->latency);
		igt_stats_push(&p->dispatch, *p->last_timestamp - start);

		/* Tidy up all the extra threads before we submit again. */
		pthread_mutex_lock(&p->lock);
		while (p->wait)
			pthread_cond_wait(&p->p_cond, &p->lock);
		pthread_mutex_unlock(&p->lock);

		p->complete++;
	}

	pthread_mutex_lock(&p->lock);
	p->wait = p->nconsumers;
	p->done = true;
	for (n = 0; n < p->nconsumers; n++)
		p->consumers[n].go = 1;
	pthread_cond_broadcast(&p->c_cond);
	pthread_mutex_unlock(&p->lock);

	return NULL;
}

static void *consumer(void *arg)
{
	struct consumer *c = arg;
	struct producer *p = c->producer;

	/* Sit around waiting for the "go" signal from the producer, then
	 * wait upon the batch to finish. This is to add extra waiters to
	 * the same request - increasing wakeup contention.
	 */
	do {
		pthread_mutex_lock(&p->lock);
		if (--p->wait == 0)
			pthread_cond_signal(&p->p_cond);
		while (!c->go)
			pthread_cond_wait(&p->c_cond, &p->lock);
		c->go = 0;
		pthread_mutex_unlock(&p->lock);
		if (p->done)
			return NULL;

		measure_latency(p, &c->latency);
	} while (1);
}

static double l_estimate(igt_stats_t *stats)
{
	if (stats->n_values > 9)
		return igt_stats_get_trimean(stats);
	else if (stats->n_values > 5)
		return igt_stats_get_median(stats);
	else
		return igt_stats_get_mean(stats);
}

static double cpu_time(const struct rusage *r)
{
	return 10e6*(r->ru_utime.tv_sec + r->ru_stime.tv_sec) +
		(r->ru_utime.tv_usec + r->ru_stime.tv_usec);
}

#define CONTEXT 1
#define REALTIME 2
static int run(int seconds,
	       int nproducers,
	       int nconsumers,
	       int nop,
	       int workload,
	       unsigned flags)
{
	pthread_attr_t attr;
	struct producer *p;
	igt_stats_t platency, latency, dispatch;
	struct rusage rused;
	uint32_t nop_batch;
	uint32_t workload_batch;
	uint32_t scratch;
	int gen, n, m;
	int complete;
	int nrun;

#if 0
	printf("producers=%d, consumers=%d, nop=%d, workload=%d, flags=%x\n",
	       nproducers, nconsumers, nop, workload, flags);
#endif

	fd = drm_open_driver(DRIVER_INTEL);
	gen = intel_gen(intel_get_drm_devid(fd));
	if (gen < 6)
		return IGT_EXIT_SKIP; /* Needs BCS timestamp */

	intel_register_access_init(intel_get_pci_device(), false);

	if (gen == 6)
		timestamp_reg = REG(RCS_TIMESTAMP);
	else
		timestamp_reg = REG(BCS_TIMESTAMP);

	if (gen < 8 && !setup_timestamp_locked())
		return IGT_EXIT_SKIP;

	nrun = read_timestamp();
	usleep(1);
	if (read_timestamp() == nrun)
		return IGT_EXIT_SKIP;

	scratch = gem_create(fd, 4*WIDTH*HEIGHT);
	nop_batch = create_nop();
	workload_batch = create_workload(gen, workload);

	p = calloc(nproducers, sizeof(*p));
	for (n = 0; n < nproducers; n++) {
		if (flags & CONTEXT)
			p[n].ctx = gem_context_create(fd);

		setup_nop(&p[n], nop_batch);
		setup_workload(&p[n], gen, scratch, workload_batch, workload);
		setup_latency(&p[n], gen);

		pthread_mutex_init(&p[n].lock, NULL);
		pthread_cond_init(&p[n].p_cond, NULL);
		pthread_cond_init(&p[n].c_cond, NULL);

		igt_stats_init(&p[n].latency);
		igt_stats_init(&p[n].dispatch);
		p[n].wait = nconsumers;
		p[n].nop = nop;
		p[n].nconsumers = nconsumers;
		p[n].consumers = calloc(nconsumers, sizeof(struct consumer));
		for (m = 0; m < nconsumers; m++) {
			p[n].consumers[m].producer = &p[n];
			igt_stats_init(&p[n].consumers[m].latency);
			pthread_create(&p[n].consumers[m].thread, NULL,
				       consumer, &p[n].consumers[m]);
		}
		pthread_mutex_lock(&p->lock);
		while (p->wait)
			pthread_cond_wait(&p->p_cond, &p->lock);
		pthread_mutex_unlock(&p->lock);
	}

	pthread_attr_init(&attr);
	if (flags & REALTIME) {
#ifdef PTHREAD_EXPLICIT_SCHED
		struct sched_param param = { .sched_priority = 99 };
		pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
		pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
		pthread_attr_setschedparam(&attr, &param);
#else
		return IGT_EXIT_SKIP;
#endif
	}
	for (n = 0; n < nproducers; n++)
		pthread_create(&p[n].thread, &attr, producer, &p[n]);

	sleep(seconds);
	done = true;

	nrun = complete = 0;
	igt_stats_init_with_size(&dispatch, nproducers);
	igt_stats_init_with_size(&platency, nproducers);
	igt_stats_init_with_size(&latency, nconsumers*nproducers);
	for (n = 0; n < nproducers; n++) {
		pthread_join(p[n].thread, NULL);

		if (!p[n].complete)
			continue;

		nrun++;
		complete += p[n].complete;
		igt_stats_push_float(&latency, l_estimate(&p[n].latency));
		igt_stats_push_float(&platency, l_estimate(&p[n].latency));
		igt_stats_push_float(&dispatch, l_estimate(&p[n].dispatch));

		for (m = 0; m < nconsumers; m++) {
			pthread_join(p[n].consumers[m].thread, NULL);
			igt_stats_push_float(&latency, l_estimate(&p[n].consumers[m].latency));
		}
	}

	getrusage(RUSAGE_SELF, &rused);

	switch ((flags >> 8) & 0xf) {
	default:
		printf("%d/%d: %7.3fus %7.3fus %7.3fus %7.3fus\n",
		       complete, nrun,
		       CYCLES_TO_US(l_estimate(&dispatch)),
		       CYCLES_TO_US(l_estimate(&latency)),
		       CYCLES_TO_US(l_estimate(&platency)),
		       cpu_time(&rused) / complete);
		break;
	case 1:
		printf("%f\n", CYCLES_TO_US(l_estimate(&dispatch)));
		break;
	case 2:
		printf("%f\n", CYCLES_TO_US(l_estimate(&latency)));
		break;
	case 3:
		printf("%f\n", CYCLES_TO_US(l_estimate(&platency)));
		break;
	case 4:
		printf("%f\n", cpu_time(&rused) / complete);
		break;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int time = 10;
	int producers = 1;
	int consumers = 0;
	int nop = 0;
	int workload = 0;
	unsigned flags = 0;
	int c;

	while ((c = getopt(argc, argv, "p:c:n:w:t:f:sR")) != -1) {
		switch (c) {
		case 'p':
			/* How many threads generate work? */
			producers = atoi(optarg);
			if (producers < 1)
				producers = 1;
			break;

		case 'c':
			/* How many threads wait upon each piece of work? */
			consumers = atoi(optarg);
			if (consumers < 0)
				consumers = 0;
			break;

		case 'n':
			/* Extra dispatch contention + interrupts */
			nop = atoi(optarg);
			if (nop < 0)
				nop = 0;
			break;

		case 'w':
			/* Control the amount of real work done */
			workload = atoi(optarg);
			if (workload < 0)
				workload = 0;
			if (workload > 100)
				workload = 100;
			break;

		case 't':
			/* How long to run the benchmark for (seconds) */
			time = atoi(optarg);
			if (time < 1)
				time = 1;
			break;

		case 'f':
			/* Select an output field */
			flags |= atoi(optarg) << 8;
			break;

		case 's':
			/* Assign each producer to its own context, adding
			 * context switching into the mix (e.g. execlists
			 * can amalgamate requests from one context, so
			 * having each producer submit in different contexts
			 * should force more execlist interrupts).
			 */
			flags |= CONTEXT;
			break;

		case 'R':
			/* Run the producers at RealTime priority */
			flags |= REALTIME;
			break;

		default:
			break;
		}
	}

	return run(time, producers, consumers, nop, workload, flags);
}
