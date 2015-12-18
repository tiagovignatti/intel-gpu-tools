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
#include "drm.h"

static int done;
static int fd;

struct consumer {
	pthread_t thread;

	int wait;

	igt_stats_t stats;
	struct producer *producer;
};

struct producer {
	pthread_t thread;
	uint32_t ctx;
	struct drm_i915_gem_exec_object2 exec[2];
	struct drm_i915_gem_relocation_entry reloc[3];

	pthread_mutex_t lock;
	pthread_cond_t p_cond, c_cond;
	uint32_t *last_timestamp;
	int wait;
	int complete;
	igt_stats_t stats;

	int nconsumers;
	struct consumer *consumers;
};

#define LOCAL_EXEC_NO_RELOC (1<<11)
#define COPY_BLT_CMD		(2<<29|0x53<<22|0x6)
#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)

#define WIDTH 256
#define HEIGHT 256

#define BCS_TIMESTAMP (0x22000 + 0x358)

static void setup_batch(struct producer *p, int gen, uint32_t scratch)
{
	const int has_64bit_reloc = gen >= 8;
	uint32_t *map;
	int i = 0;

	p->exec[0].handle = scratch;
	p->exec[1].relocation_count = 3;
	p->exec[1].relocs_ptr = (uintptr_t)p->reloc;
	p->exec[1].handle = gem_create(fd, 4096);
	map = gem_mmap__cpu(fd, p->exec[1].handle, 0, 4096, PROT_WRITE);

	/* XY_SRC_COPY */
	map[i++] = COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB;
	if (has_64bit_reloc)
		map[i-1] += 2;
	map[i++] = 0xcc << 16 | 1 << 25 | 1 << 24 | (4*WIDTH);
	map[i++] = 0;
	map[i++] = HEIGHT << 16 | WIDTH;
	p->reloc[0].offset = i * sizeof(uint32_t);
	p->reloc[0].delta = 0;
	p->reloc[0].target_handle = scratch;
	p->reloc[0].read_domains = I915_GEM_DOMAIN_RENDER;
	p->reloc[0].write_domain = I915_GEM_DOMAIN_RENDER;
	p->reloc[0].presumed_offset = 0;
	map[i++] = 0;
	if (has_64bit_reloc)
		map[i++] = 0;
	map[i++] = 0;
	map[i++] = 4096;
	p->reloc[1].offset = i * sizeof(uint32_t);
	p->reloc[1].delta = 0;
	p->reloc[1].target_handle = scratch;
	p->reloc[1].read_domains = I915_GEM_DOMAIN_RENDER;
	p->reloc[1].write_domain = 0;
	p->reloc[1].presumed_offset = 0;
	map[i++] = 0;
	if (has_64bit_reloc)
		map[i++] = 0;

	/* MI_FLUSH_DW */
	map[i++] = 0x26 << 23 | 1;
	if (has_64bit_reloc)
		map[i-1]++;
	map[i++] = 0;
	map[i++] = 0;
	if (has_64bit_reloc)
		map[i++] = 0;

	/* MI_STORE_REG_MEM */
	map[i++] = 0x24 << 23 | 1;
	if (has_64bit_reloc)
		map[i-1]++;
	map[i++] = BCS_TIMESTAMP;
	p->reloc[2].offset = i * sizeof(uint32_t);
	p->reloc[2].delta = 4000;
	p->reloc[2].target_handle = p->exec[1].handle;
	p->reloc[2].read_domains = I915_GEM_DOMAIN_INSTRUCTION;
	p->reloc[2].write_domain = 0; /* We lie! */
	p->reloc[2].presumed_offset = 0;
	p->last_timestamp = &map[1000];
	map[i++] = 4000;
	if (has_64bit_reloc)
		map[i++] = 0;

	map[i++] = MI_BATCH_BUFFER_END;
}

static void measure_latency(struct producer *p, igt_stats_t *stats)
{
	gem_sync(fd, p->exec[1].handle);
	igt_stats_push(stats, INREG(BCS_TIMESTAMP) - *p->last_timestamp);
}

static void *producer(void *arg)
{
	struct producer *p = arg;
	struct drm_i915_gem_execbuffer2 execbuf;
	int n;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)p->exec;
	execbuf.buffer_count = 2;
	execbuf.flags = I915_EXEC_BLT | LOCAL_EXEC_NO_RELOC;
	execbuf.rsvd1 = p->ctx;

	while (!done) {
		int batches = 10;
		while (batches--)
			gem_execbuf(fd, &execbuf);

		pthread_mutex_lock(&p->lock);
		p->wait = p->nconsumers;
		for (n = 0; n < p->nconsumers; n++)
			p->consumers[n].wait = 1;
		pthread_cond_broadcast(&p->c_cond);
		pthread_mutex_unlock(&p->lock);

		measure_latency(p, &p->stats);

		pthread_mutex_lock(&p->lock);
		while (p->wait)
			pthread_cond_wait(&p->p_cond, &p->lock);
		pthread_mutex_unlock(&p->lock);

		p->complete++;
	}

	return NULL;
}

static void *consumer(void *arg)
{
	struct consumer *c = arg;
	struct producer *p = c->producer;

	while (!done) {
		pthread_mutex_lock(&p->lock);
		if (--p->wait == 0)
			pthread_cond_signal(&p->p_cond);
		while (!c->wait)
			pthread_cond_wait(&p->c_cond, &p->lock);
		c->wait = 0;
		pthread_mutex_unlock(&p->lock);

		measure_latency(p, &c->stats);
	}

	return NULL;
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

#define CONTEXT 1
static int run(int nproducers, int nconsumers, unsigned flags)
{
	struct producer *p;
	igt_stats_t stats;
	uint32_t handle;
	int gen, n, m;
	int complete;
	int nrun;

	fd = drm_open_driver(DRIVER_INTEL);
	gen = intel_gen(intel_get_drm_devid(fd));
	if (gen < 6)
		return 77; /* Needs BCS timestamp */

	intel_register_access_init(intel_get_pci_device(), false);

	handle = gem_create(fd, 4*WIDTH*HEIGHT);

	p = calloc(nproducers, sizeof(*p));
	for (n = 0; n < nproducers; n++) {
		setup_batch(&p[n], gen, handle);
		if (flags & CONTEXT)
			p[n].ctx = gem_context_create(fd);

		pthread_mutex_init(&p[n].lock, NULL);
		pthread_cond_init(&p[n].p_cond, NULL);
		pthread_cond_init(&p[n].c_cond, NULL);

		igt_stats_init(&p[n].stats);
		p[n].wait = nconsumers;
		p[n].nconsumers = nconsumers;
		p[n].consumers = calloc(nconsumers, sizeof(struct consumer));
		for (m = 0; m < nconsumers; m++) {
			p[n].consumers[m].producer = &p[n];
			igt_stats_init(&p[n].consumers[m].stats);
			pthread_create(&p[n].consumers[m].thread, NULL,
				       consumer, &p[n].consumers[m]);
		}
	}

	for (n = 0; n < nproducers; n++)
		pthread_create(&p[n].thread, NULL, producer, &p[n]);

	sleep(10);
	done = true;

	nrun = complete = 0;
	igt_stats_init_with_size(&stats, nconsumers*nproducers);
	for (n = 0; n < nproducers; n++) {
		pthread_cancel(p[n].thread);
		pthread_join(p[n].thread, NULL);

		if (!p[n].complete)
			continue;

		nrun++;
		complete += p[n].complete;
		igt_stats_push_float(&stats, l_estimate(&p[n].stats));

		for (m = 0; m < nconsumers; m++) {
			pthread_cancel(p[n].consumers[m].thread);
			pthread_join(p[n].consumers[m].thread, NULL);
			igt_stats_push_float(&stats, l_estimate(&p[n].consumers[m].stats));
		}
	}
	printf("%d/%d: %7.3fus\n", complete, nrun, 80/1000.*l_estimate(&stats));

	return 0;
}

int main(int argc, char **argv)
{
	int producers = 8;
	int consumers = 1;
	unsigned flags = 0;
	int c;

	while ((c = getopt (argc, argv, "p:c:sa")) != -1) {
		switch (c) {
		case 'p':
			producers = atoi(optarg);
			break;

		case 'c':
			consumers = atoi(optarg);
			break;

		case 's':
			flags |= CONTEXT;
			break;

		default:
			break;
		}
	}

	return run(producers, consumers, flags);
}
