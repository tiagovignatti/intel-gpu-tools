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

#include "igt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "drm.h"

IGT_TEST_DESCRIPTION("Exercises the basic execbuffer using the handle LUT"
		     " interface.");

#define BATCH_SIZE		(1024*1024)

#define LOCAL_I915_EXEC_NO_RELOC (1<<11)
#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

#define MAX_NUM_EXEC 2048
#define MAX_NUM_RELOC 4096

#define SKIP_RELOC 0x1
#define NO_RELOC 0x2
#define CYCLE_BATCH 0x4
#define FAULT 0x8

int target[MAX_NUM_RELOC];
struct drm_i915_gem_exec_object2 gem_exec[MAX_NUM_EXEC+1];
struct drm_i915_gem_relocation_entry mem_reloc[MAX_NUM_RELOC];

static uint32_t state = 0x12345678;

static uint32_t
hars_petruska_f54_1_random (void)
{
#define rol(x,k) ((x << k) | (x >> (32-k)))
    return state = (state ^ rol (state, 5) ^ rol (state, 24)) + 0x37798849;
#undef rol
}

static int has_exec_lut(int fd)
{
	struct drm_i915_gem_execbuffer2 execbuf;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)(gem_exec + MAX_NUM_EXEC);
	execbuf.buffer_count = 1;
	execbuf.flags = LOCAL_I915_EXEC_HANDLE_LUT;

	return drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) == 0;
}

#define ELAPSED(a,b) (1e6*((b)->tv_sec - (a)->tv_sec) + ((b)->tv_usec - (a)->tv_usec))
igt_simple_main
{
	uint32_t batch[2] = {MI_BATCH_BUFFER_END};
	uint32_t cycle[16];
	int fd, n, m, count, c;
	const struct {
		const char *name;
		unsigned int flags;
	} pass[] = {
		{ .name = "relocation", .flags = 0 },
		{ .name = "cycle-relocation", .flags = CYCLE_BATCH },
		{ .name = "fault-relocation", .flags = FAULT },
		{ .name = "skip-relocs", .flags = SKIP_RELOC },
		{ .name = "no-relocs", .flags = SKIP_RELOC | NO_RELOC },
		{ .name = NULL },
	}, *p;
	struct drm_i915_gem_relocation_entry *reloc;
	uint32_t reloc_handle;
	int size;

	igt_skip_on_simulation();

	fd = drm_open_any();

	memset(gem_exec, 0, sizeof(gem_exec));
	for (n = 0; n < MAX_NUM_EXEC; n++)
		gem_exec[n].handle = gem_create(fd, 4096);

	for (n = 0; n < 16; n++) {
		cycle[n] = gem_create(fd, 4096);
		gem_write(fd, cycle[n], 0, batch, sizeof(batch));
	}
	gem_exec[MAX_NUM_EXEC].handle = cycle[0];

	memset(mem_reloc, 0, sizeof(mem_reloc));
	for (n = 0; n < MAX_NUM_RELOC; n++) {
		mem_reloc[n].offset = 1024;
		mem_reloc[n].read_domains = I915_GEM_DOMAIN_RENDER;
	}

	size = ALIGN(sizeof(mem_reloc), 4096);
	reloc_handle = gem_create(fd, size);
	reloc = gem_mmap__cpu(fd, reloc_handle, 0, size, PROT_READ | PROT_WRITE);
	for (n = 0; n < MAX_NUM_RELOC; n++) {
		reloc[n].offset = 1024;
		reloc[n].read_domains = I915_GEM_DOMAIN_RENDER;
	}
	munmap(reloc, size);

	igt_require(has_exec_lut(fd));

	for (p = pass; p->name != NULL; p++) {
		if (p->flags & FAULT)
			igt_disable_prefault();
		for (n = 1; n <= MAX_NUM_EXEC; n *= 2) {
			double elapsed[16][2];
			double s_x, s_y, s_xx, s_xy;
			double A, B;
			int i, j;

			for (i = 0, m = 1; m <= MAX_NUM_RELOC; m *= 2, i++) {
				struct drm_i915_gem_execbuffer2 execbuf;
				struct drm_i915_gem_exec_object2 *objects;
				struct timeval start, end;

				if (p->flags & FAULT)
					reloc = gem_mmap__cpu(fd, reloc_handle, 0, size, PROT_READ | PROT_WRITE);
				else
					reloc = mem_reloc;

				gem_exec[MAX_NUM_EXEC].relocation_count = m;
				gem_exec[MAX_NUM_EXEC].relocs_ptr = (uintptr_t)reloc;
				objects = gem_exec + MAX_NUM_EXEC - n;

				memset(&execbuf, 0, sizeof(execbuf));
				execbuf.buffers_ptr = (uintptr_t)objects;
				execbuf.buffer_count = n + 1;
				execbuf.flags = LOCAL_I915_EXEC_HANDLE_LUT;
				if (p->flags & NO_RELOC)
					execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;

				for (j = 0; j < m; j++) {
					target[j] = hars_petruska_f54_1_random() % n;
					reloc[j].target_handle = target[j];
					reloc[j].presumed_offset = 0;
				}

				gem_execbuf(fd,&execbuf);
				gettimeofday(&start, NULL);
				for (count = 0; count < 1000; count++) {
					if ((p->flags & SKIP_RELOC) == 0) {
						for (j = 0; j < m; j++)
							reloc[j].presumed_offset = 0;
						if (p->flags & CYCLE_BATCH) {
							c = (c + 1) % 16;
							gem_exec[MAX_NUM_EXEC].handle = cycle[c];
						}
					}
					if (p->flags & FAULT) {
						munmap(reloc, size);
						reloc = gem_mmap__cpu(fd, reloc_handle, 0, size, PROT_READ | PROT_WRITE);
						gem_exec[MAX_NUM_EXEC].relocs_ptr = (uintptr_t)reloc;
					}
					gem_execbuf(fd, &execbuf);
				}
				gettimeofday(&end, NULL);
				c = 16;
				do
					gem_sync(fd, cycle[--c]);
				while (c != 0);
				gem_exec[MAX_NUM_EXEC].handle = cycle[c];
				elapsed[i][1] = ELAPSED(&start, &end);

				execbuf.flags &= ~LOCAL_I915_EXEC_HANDLE_LUT;
				for (j = 0; j < m; j++)
					reloc[j].target_handle = objects[target[j]].handle;

				gem_execbuf(fd,&execbuf);
				gettimeofday(&start, NULL);
				for (count = 0; count < 1000; count++) {
					if ((p->flags & SKIP_RELOC) == 0) {
						for (j = 0; j < m; j++)
							reloc[j].presumed_offset = 0;
						if (p->flags & CYCLE_BATCH) {
							c = (c + 1) % 16;
							gem_exec[MAX_NUM_EXEC].handle = cycle[c];
						}
					}
					if (p->flags & FAULT) {
						munmap(reloc, size);
						reloc = gem_mmap__cpu(fd, reloc_handle, 0, size, PROT_READ | PROT_WRITE);
						gem_exec[MAX_NUM_EXEC].relocs_ptr = (uintptr_t)reloc;
					}
					gem_execbuf(fd, &execbuf);
				}
				gettimeofday(&end, NULL);
				c = 16;
				do
					gem_sync(fd, cycle[--c]);
				while (c != 0);
				gem_exec[MAX_NUM_EXEC].handle = cycle[c];
				elapsed[i][0] = ELAPSED(&start, &end);

				if (p->flags & FAULT)
					munmap(reloc, size);
			}

			igt_info("%s: buffers=%4d:", p->name, n);

			s_x = s_y = s_xx = s_xy = 0;
			for (j = 0; j < i; j++) {
				int k = 1 << j;
				s_x += k;
				s_y += elapsed[j][0];
				s_xx += k * k;
				s_xy += k * elapsed[j][0];
			}
			B = (s_xy - s_x * s_y / j) / (s_xx - s_x * s_x / j);
			A = s_y / j - B * s_x / j;
			igt_info(" old=%7.0f + %.1f*reloc,", A, B);

			s_x = s_y = s_xx = s_xy = 0;
			for (j = 0; j < i; j++) {
				int k = 1 << j;
				s_x += k;
				s_y += elapsed[j][1];
				s_xx += k * k;
				s_xy += k * elapsed[j][1];
			}
			B = (s_xy - s_x * s_y / j) / (s_xx - s_x * s_x / j);
			A = s_y / j - B * s_x / j;
			igt_info(" lut=%7.0f + %.1f*reloc (ns)", A, B);

			igt_info("\n");
		}
		if (p->flags & FAULT)
			igt_enable_prefault();
	}
}
