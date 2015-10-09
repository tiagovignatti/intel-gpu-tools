/*
 * Copyright © 2012 Intel Corporation
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

/* Exercises the basic execbuffer using the handle LUT interface */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "drm.h"
#include "ioctl_wrappers.h"
#include "igt_debugfs.h"
#include "drmtest.h"

#define LOCAL_I915_EXEC_NO_RELOC (1<<11)
#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

#define SKIP_RELOC 0x1
#define NO_RELOC 0x2
#define CYCLE_BATCH 0x4
#define FAULT 0x8
#define LUT 0x10
#define SEQUENTIAL_OFFSET 0x20
#define REVERSE_OFFSET 0x40
#define RANDOM_OFFSET 0x80

static uint32_t
hars_petruska_f54_1_random (void)
{
	static uint32_t state = 0x12345678;

#define rol(x,k) ((x << k) | (x >> (32-k)))
	return state = (state ^ rol (state, 5) ^ rol (state, 24)) + 0x37798849;
#undef rol
}

#define ELAPSED(a,b) (1e6*((b)->tv_sec - (a)->tv_sec) + ((b)->tv_usec - (a)->tv_usec))
static int run(unsigned batch_size,
	       unsigned flags,
	       int num_objects,
	       int num_relocs, int reps)
{
	uint32_t batch[2] = {MI_BATCH_BUFFER_END};
	uint32_t cycle[16];
	int fd, n, count, c, size = 0;
	struct drm_i915_gem_relocation_entry *reloc = NULL;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 *objects;
	struct timeval start, end;
	uint32_t reloc_handle = 0;
	struct drm_i915_gem_exec_object2 *gem_exec;
	struct drm_i915_gem_relocation_entry *mem_reloc = NULL;
	int *target;

	gem_exec = calloc(sizeof(*gem_exec), num_objects + 1);
	mem_reloc = calloc(sizeof(*mem_reloc), num_relocs);
	target = calloc(sizeof(*target), num_relocs);

	fd = drm_open_driver(DRIVER_INTEL);

	for (n = 0; n < num_objects; n++)
		gem_exec[n].handle = gem_create(fd, 4096);

	for (n = 0; n < 16; n++) {
		cycle[n] = gem_create(fd, batch_size);
		gem_write(fd, cycle[n], 0, batch, sizeof(batch));
	}
	gem_exec[num_objects].handle = cycle[c = 0];

	for (n = 0; n < num_relocs; n++) {
		mem_reloc[n].offset = 1024;
		mem_reloc[n].read_domains = I915_GEM_DOMAIN_RENDER;
	}
	for (n = 0; n < num_relocs; n++) {
		if (flags & SEQUENTIAL_OFFSET)
			mem_reloc[n].offset = 8 + (8*n % (batch_size - 16));
		else if (flags & REVERSE_OFFSET)
			mem_reloc[n].offset = batch_size - 8 - (8*n % (batch_size - 16));
		else if (flags & RANDOM_OFFSET)
			mem_reloc[n].offset = 8 +
				8*hars_petruska_f54_1_random() % (batch_size - 16);
		else
			mem_reloc[n].offset = 1024;
		mem_reloc[n].read_domains = I915_GEM_DOMAIN_RENDER;
	}

	if (num_relocs) {
		size = ALIGN(sizeof(*mem_reloc)*num_relocs, 4096);
		reloc_handle = gem_create(fd, size);
		reloc = __gem_mmap__cpu(fd, reloc_handle, 0, size, PROT_READ | PROT_WRITE);
		memcpy(reloc, mem_reloc, sizeof(*mem_reloc)*num_relocs);
		munmap(reloc, size);

		if (flags & FAULT) {
			igt_disable_prefault();
			reloc = __gem_mmap__cpu(fd, reloc_handle, 0, size, PROT_READ | PROT_WRITE);
		} else
			reloc = mem_reloc;
	}

	gem_exec[num_objects].relocation_count = num_relocs;
	gem_exec[num_objects].relocs_ptr = (uintptr_t)reloc;
	objects = gem_exec;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)objects;
	execbuf.buffer_count = num_objects + 1;
	if (flags & LUT)
		execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	if (flags & NO_RELOC)
		execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;

	for (n = 0; n < num_relocs; n++) {
		target[n] = hars_petruska_f54_1_random() % num_objects;
		if (flags & LUT)
			reloc[n].target_handle = target[n];
		else
			reloc[n].target_handle = objects[target[n]].handle;
		reloc[n].presumed_offset = 0;
	}

	gem_execbuf(fd, &execbuf);

	while (reps--) {
		gettimeofday(&start, NULL);
		for (count = 0; count < 1000; count++) {
			if ((flags & SKIP_RELOC) == 0) {
				for (n = 0; n < num_relocs; n++)
					reloc[n].presumed_offset = 0;
				if (flags & CYCLE_BATCH) {
					c = (c + 1) % 16;
					gem_exec[num_objects].handle = cycle[c];
				}
			}
			if (flags & FAULT && reloc) {
				munmap(reloc, size);
				reloc = __gem_mmap__cpu(fd, reloc_handle, 0, size, PROT_READ | PROT_WRITE);
				gem_exec[num_objects].relocs_ptr = (uintptr_t)reloc;
			}
			gem_execbuf(fd, &execbuf);
		}
		gettimeofday(&end, NULL);
		printf("%.3f\n", ELAPSED(&start, &end));
	}

	if (flags & FAULT && reloc) {
		munmap(reloc, size);
		igt_enable_prefault();
	}

	return 0;
}

int main(int argc, char **argv)
{
	unsigned num_objects = 1, num_relocs = 0, flags = 0;
	unsigned size = 4096;
	int reps = 13;
	int c;

	while ((c = getopt (argc, argv, "b:r:s:e:l:m:o:")) != -1) {
		switch (c) {
		case 'l':
			reps = atoi(optarg);
			if (reps < 1)
				reps = 1;
			break;

		case 's':
			size = atoi(optarg);
			if (size < 4096)
				size = 4096;
			size = ALIGN(size, 4096);
			break;

		case 'e':
			if (strcmp(optarg, "busy") == 0) {
				flags |= 0;
			} else if (strcmp(optarg, "cyclic") == 0) {
				flags |= CYCLE_BATCH;
			} else if (strcmp(optarg, "fault") == 0) {
				flags |= FAULT;
			} else if (strcmp(optarg, "skip") == 0) {
				flags |= SKIP_RELOC;
			} else if (strcmp(optarg, "none") == 0) {
				flags |= SKIP_RELOC | NO_RELOC;
			} else {
				abort();
			}
			break;

		case 'm':
			if (strcmp(optarg, "old") == 0) {
				flags |= 0;
			} else if (strcmp(optarg, "lut") == 0) {
				flags |= LUT;
			} else {
				abort();
			}
			break;

		case 'o':
			if (strcmp(optarg, "constant") == 0) {
				flags |= 0;
			} else if (strcmp(optarg, "sequential") == 0) {
				flags |= SEQUENTIAL_OFFSET;
			} else if (strcmp(optarg, "reverse") == 0) {
				flags |= REVERSE_OFFSET;
			} else if (strcmp(optarg, "random") == 0) {
				flags |= RANDOM_OFFSET;
			} else {
				abort();
			}
			break;

		case 'b':
			num_objects = atoi(optarg);
			if (num_objects < 1)
				num_objects = 1;
			break;

		case 'r':
			num_relocs = atoi(optarg);
			if (num_relocs < 0)
				num_relocs = 0;
			break;
		}
	}

	return run(size, flags, num_objects, num_relocs, reps);
}
