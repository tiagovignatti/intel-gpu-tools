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

#ifndef GPU_PERF_H
#define GPU_PERF_H

#include <stdint.h>

#define MAX_RINGS 4

struct gpu_perf {
	const char *error;
	int page_size;
	int nr_cpus;
	int nr_events;
	int *fd;
	void **map;
	struct gpu_perf_sample {
		uint64_t id;
		int (*func)(struct gpu_perf *, const void *);
	} *sample;

	unsigned flip_complete[MAX_RINGS];
	unsigned ctx_switch[MAX_RINGS];

	struct gpu_perf_comm {
		struct gpu_perf_comm *next;
		char name[256];
		pid_t pid;
		int nr_requests[4];
		void *user_data;

		uint64_t wait_time;
		uint32_t nr_sema;

		time_t show;
	} *comm;
	struct gpu_perf_time {
		struct gpu_perf_time *next;
		struct gpu_perf_comm *comm;
		uint32_t seqno;
		uint64_t time;
	} *wait[MAX_RINGS];
};

void gpu_perf_init(struct gpu_perf *gp, unsigned flags);
int gpu_perf_update(struct gpu_perf *gp);

#endif /* GPU_PERF_H */
