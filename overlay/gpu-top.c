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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "perf.h"
#include "igfx.h"
#include "gpu-top.h"

#define RING_TAIL      0x00
#define RING_HEAD      0x04
#define ADDR_MASK      0x001FFFFC
#define RING_CTL       0x0C
#define   RING_WAIT		(1<<11)
#define   RING_WAIT_SEMAPHORE	(1<<10)

#define __I915_PERF_RING(n) (4*n)
#define I915_PERF_RING_BUSY(n) (__I915_PERF_RING(n) + 0)
#define I915_PERF_RING_WAIT(n) (__I915_PERF_RING(n) + 1)
#define I915_PERF_RING_SEMA(n) (__I915_PERF_RING(n) + 2)

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

static int perf_init(struct gpu_top *gt)
{
	const char *names[] = {
		"RCS",
		"VCS",
		"BCS",
		NULL,
	};
	int n;

	gt->fd = perf_i915_open(I915_PERF_RING_BUSY(0), -1);
	if (gt->fd < 0)
		return -1;

	if (perf_i915_open(I915_PERF_RING_WAIT(0), gt->fd) >= 0)
		gt->have_wait = 1;

	if (perf_i915_open(I915_PERF_RING_SEMA(0), gt->fd) >= 0)
		gt->have_sema = 1;

	gt->ring[0].name = names[0];
	gt->num_rings = 1;

	for (n = 1; names[n]; n++) {
		if (perf_i915_open(I915_PERF_RING_BUSY(n), gt->fd) >= 0) {
			if (gt->have_wait &&
			    perf_i915_open(I915_PERF_RING_WAIT(n), gt->fd) < 0)
				return -1;

			if (gt->have_sema &&
			    perf_i915_open(I915_PERF_RING_SEMA(n), gt->fd) < 0)
				return -1;

			gt->ring[gt->num_rings++].name = names[n];
		}
	}

	return 0;
}

struct mmio_ring {
	int id;
	uint32_t base;
	void *mmio;
	int idle, wait, sema;
};

static uint32_t mmio_ring_read(struct mmio_ring *ring, uint32_t reg)
{
	return igfx_read(ring->mmio, ring->base + reg);
}

static void mmio_ring_init(struct mmio_ring *ring, void *mmio)
{
	uint32_t ctl;

	ring->mmio = mmio;

	ctl = mmio_ring_read(ring, RING_CTL);
	if ((ctl & 1) == 0)
		ring->id = -1;
}

static void mmio_ring_reset(struct mmio_ring *ring)
{
	ring->idle = 0;
	ring->wait = 0;
	ring->sema = 0;
}

static void mmio_ring_sample(struct mmio_ring *ring)
{
	uint32_t head, tail, ctl;

	if (ring->id == -1)
		return;

	head = mmio_ring_read(ring, RING_HEAD) & ADDR_MASK;
	tail = mmio_ring_read(ring, RING_TAIL) & ADDR_MASK;
	ring->idle += head == tail;

	ctl = mmio_ring_read(ring, RING_CTL);
	ring->wait += !!(ctl & RING_WAIT);
	ring->sema += !!(ctl & RING_WAIT_SEMAPHORE);
}

static void mmio_ring_emit(struct mmio_ring *ring, int samples, union gpu_top_payload *payload)
{
	if (ring->id == -1)
		return;

	payload[ring->id].u.busy = 100 - 100 * ring->idle / samples;
	payload[ring->id].u.wait = 100 * ring->wait / samples;
	payload[ring->id].u.sema = 100 * ring->sema / samples;
}

static void mmio_init(struct gpu_top *gt)
{
	struct mmio_ring render_ring = {
		.base = 0x2030,
		.id = 0,
	}, bsd_ring = {
		.base = 0x4030,
		.id = 1,
	}, bsd6_ring = {
		.base = 0x12030,
		.id = 1,
	}, blt_ring = {
		.base = 0x22030,
		.id = 2,
	};
	const struct igfx_info *info;
	struct pci_device *igfx;
	void *mmio;
	int fd[2], i;

	igfx = igfx_get();
	if (!igfx)
		return;

	if (pipe(fd) < 0)
		return;

	info = igfx_get_info(igfx);

	switch (fork()) {
	case -1: return;
	default:
		 fcntl(fd[0], F_SETFL, fcntl(fd[0], F_GETFL) | O_NONBLOCK);
		 gt->fd = fd[0];
		 gt->type = MMIO;
		 gt->ring[0].name = "render";
		 gt->num_rings = 1;
		 if (info->gen >= 040) {
			 gt->ring[1].name = "bitstream";
			 gt->num_rings++;
		 }
		 if (info->gen >= 060) {
			 gt->ring[2].name = "blt";
			 gt->num_rings++;
		 }
		 close(fd[1]);
		 return;
	case 0:
		 close(fd[0]);
		 break;
	}

	mmio = igfx_get_mmio(igfx);
	if (mmio == NULL)
		exit(127);

	mmio_ring_init(&render_ring, mmio);
	if (info->gen >= 060) {
		bsd_ring = bsd6_ring;
		mmio_ring_init(&blt_ring, mmio);
	}
	if (info->gen >= 040) {
		mmio_ring_init(&bsd_ring, mmio);
	}

	for (;;) {
		union gpu_top_payload payload[MAX_RINGS];

		mmio_ring_reset(&render_ring);
		mmio_ring_reset(&bsd_ring);
		mmio_ring_reset(&blt_ring);

		for (i = 0; i < 1000; i++) {
			mmio_ring_sample(&render_ring);
			mmio_ring_sample(&bsd_ring);
			mmio_ring_sample(&blt_ring);
			usleep(1000);
		}

		mmio_ring_emit(&render_ring, 1000, payload);
		mmio_ring_emit(&bsd_ring, 1000, payload);
		mmio_ring_emit(&blt_ring, 1000, payload);

		write(fd[1], payload, sizeof(payload));
	}
}

void gpu_top_init(struct gpu_top *gt)
{
	memset(gt, 0, sizeof(*gt));
	gt->fd = -1;

	if (perf_init(gt) == 0)
		return;

	mmio_init(gt);
}

int gpu_top_update(struct gpu_top *gt)
{
	uint32_t data[1024];
	int update, len;

	if (gt->fd < 0)
		return 0;

	if (gt->type == PERF) {
		struct gpu_top_stat *s = &gt->stat[gt->count++&1];
		struct gpu_top_stat *d = &gt->stat[gt->count&1];
		uint64_t *sample, d_time;
		int n, m;

		len = read(gt->fd, data, sizeof(data));
		if (len < 0)
			return 0;

		sample = (uint64_t *)data + 1;

		s->time = *sample++;
		for (n = m = 0; n < gt->num_rings; n++) {
			s->busy[n] = sample[m++];
			if (gt->have_wait)
				s->wait[n] = sample[m++];
			if (gt->have_sema)
				s->sema[n] = sample[m++];
		}

		if (gt->count == 1)
			return 0;

		d_time = s->time - d->time;
		for (n = 0; n < gt->num_rings; n++) {
			gt->ring[n].u.u.busy = (100 * (s->busy[n] - d->busy[n]) + d_time/2) / d_time;
			if (gt->have_wait)
				gt->ring[n].u.u.wait = (100 * (s->wait[n] - d->wait[n]) + d_time/2) / d_time;
			if (gt->have_sema)
				gt->ring[n].u.u.sema = (100 * (s->sema[n] - d->sema[n]) + d_time/2) / d_time;

			/* in case of rounding + sampling errors, fudge */
			if (gt->ring[n].u.u.busy > 100)
				gt->ring[n].u.u.busy = 100;
			if (gt->ring[n].u.u.wait > 100)
				gt->ring[n].u.u.wait = 100;
			if (gt->ring[n].u.u.sema > 100)
				gt->ring[n].u.u.sema = 100;
		}

		update = 1;
	} else {
		while ((len = read(gt->fd, data, sizeof(data))) > 0) {
			uint32_t *ptr = &data[len/sizeof(uint32_t) - MAX_RINGS];
			gt->ring[0].u.payload = ptr[0];
			gt->ring[1].u.payload = ptr[1];
			gt->ring[2].u.payload = ptr[2];
			gt->ring[3].u.payload = ptr[3];
			update = 1;
		}
	}

	return update;
}
