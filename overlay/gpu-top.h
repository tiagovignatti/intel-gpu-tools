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

#ifndef GPU_TOP_H
#define GPU_TOP_H

#define MAX_RINGS 4

#include <stdint.h>

struct gpu_top {
	enum { PERF, MMIO } type;
	int fd;

	int num_rings;
	int have_wait;
	int have_sema;

	struct gpu_top_ring {
		const char *name;
		union gpu_top_payload {
			struct {
				uint8_t busy;
				uint8_t wait;
				uint8_t sema;
			} u;
			uint32_t payload;
		} u;
	} ring[MAX_RINGS];

	struct gpu_top_stat {
		uint64_t time;
		uint64_t busy[MAX_RINGS];
		uint64_t wait[MAX_RINGS];
		uint64_t sema[MAX_RINGS];
	} stat[2];
	int count;
};

void gpu_top_init(struct gpu_top *gt);
int gpu_top_update(struct gpu_top *gt);

#endif /* GPU_TOP_H */
