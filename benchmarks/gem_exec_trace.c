/*
 * Copyright © 2011 Intel Corporation
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
#include <time.h>

#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_io.h"
#include "igt_stats.h"

enum {
	ADD_BO = 0,
	DEL_BO,
	EXEC,
};

struct trace_add_bo {
	uint32_t handle;
	uint64_t size;
} __attribute__((packed));

struct trace_del_bo {
	uint32_t handle;
} __attribute__((packed));

struct trace_exec {
	uint32_t object_count;
	uint64_t flags;
} __attribute__((packed));
struct trace_exec_object {
	uint32_t handle;
	uint32_t relocation_count;
	uint64_t alignment;
	uint64_t flags;
	uint64_t rsvd1;
	uint64_t rsvd2;
} __attribute__((packed));
struct trace_exec_relocation {
	uint32_t target_handle;
	uint32_t delta;
	uint64_t offset;
	uint32_t read_domains;
	uint32_t write_domain;
} __attribute__((packed));

static double elapsed(const struct timespec *start, const struct timespec *end)
{
	return 1e3*(end->tv_sec - start->tv_sec) + 1e-6*(end->tv_nsec - start->tv_nsec);
}

static void replay(const char *filename)
{
	struct timespec t_start, t_end;
	struct drm_i915_gem_execbuffer2 eb = {};
	struct bo {
		uint32_t handle;
		uint64_t offset;

		struct drm_i915_gem_relocation_entry *relocs;
		uint32_t max_relocs;
	} *bo = NULL, **offsets = NULL;
	int num_bo = 0;
	struct drm_i915_gem_exec_object2 *exec_objects = NULL;
	int max_objects = 0;
	struct stat st;
	uint8_t *ptr, *end;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return;

	if (fstat(fd, &st) < 0)
		return;

	ptr = mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);

	if (ptr == MAP_FAILED)
		return;

	madvise(ptr, st.st_size, MADV_SEQUENTIAL);

	end = ptr + st.st_size;
	fd = drm_open_driver(DRIVER_INTEL);

	clock_gettime(CLOCK_MONOTONIC, &t_start);
	do {
		switch (*ptr++) {
		case ADD_BO: {
				     uint32_t bb = 0xa << 23;
				     struct trace_add_bo *t = (void *)ptr;
				     ptr = (void *)(t + 1);

				     if (t->handle >= num_bo) {
					     int new_bo = (t->handle + 4096) & -4096;
					     bo = realloc(bo, sizeof(*bo)*new_bo);
					     memset(bo + num_bo, 0, sizeof(*bo)*(new_bo - num_bo));
					     num_bo = new_bo;
				     }

				     bo[t->handle].handle = gem_create(fd, t->size);
				     gem_write(fd, bo[t->handle].handle, 0, &bb, sizeof(bb));
				     break;
			     }
		case DEL_BO: {
				     struct trace_del_bo *t = (void *)ptr;
				     ptr = (void *)(t + 1);

				     gem_close(fd, bo[t->handle].handle);
				     bo[t->handle].handle = 0;

				     free(bo[t->handle].relocs);
				     bo[t->handle].relocs = NULL;
				     bo[t->handle].max_relocs = 0;
				     break;
			     }
		case EXEC: {
				   struct trace_exec *t = (void *)ptr;
				   uint32_t i, j;
				   ptr = (void *)(t + 1);

				   eb.buffer_count = t->object_count;
				   eb.flags = t->flags & ~I915_EXEC_RING_MASK;

				   if (eb.buffer_count > max_objects) {
					   free(exec_objects);
					   free(offsets);

					   max_objects = ALIGN(eb.buffer_count, 4096);

					   exec_objects = malloc(max_objects*sizeof(*exec_objects));
					   offsets = malloc(max_objects*sizeof(*offsets));

					   eb.buffers_ptr = (uintptr_t)exec_objects;
				   }

				   for (i = 0; i < eb.buffer_count; i++) {
					   struct drm_i915_gem_relocation_entry *relocs;
					   struct trace_exec_object *to = (void *)ptr;
					   ptr = (void *)(to + 1);

					   offsets[i] = &bo[to->handle];

					   exec_objects[i].handle = bo[to->handle].handle;
					   exec_objects[i].offset = bo[to->handle].offset;
					   exec_objects[i].alignment = to->alignment;
					   exec_objects[i].flags = to->flags;
					   exec_objects[i].rsvd1 = to->rsvd1;
					   exec_objects[i].rsvd2 = to->rsvd2;

					   exec_objects[i].relocation_count = to->relocation_count;
					   if (!to->relocation_count)
						   continue;

					   if (to->relocation_count > bo[to->handle].max_relocs) {
						   free(bo[to->handle].relocs);

						   bo[to->handle].max_relocs = ALIGN(to->relocation_count, 128);
						   bo[to->handle].relocs = malloc(sizeof(*bo[to->handle].relocs)*bo[to->handle].max_relocs);
					   }
					   relocs = bo[to->handle].relocs;
					   exec_objects[i].relocs_ptr = (uintptr_t)relocs;

					   for (j = 0; j < to->relocation_count; j++) {
						   struct trace_exec_relocation *tr = (void *)ptr;
						   ptr = (void *)(tr + 1);

						   if (eb.flags & I915_EXEC_HANDLE_LUT) {
							   uint32_t handle;

							   relocs[j].target_handle = tr->target_handle;

							   handle = exec_objects[tr->target_handle].handle;
							   relocs[j].presumed_offset = bo[handle].offset;
						   } else {
							   relocs[j].target_handle = bo[tr->target_handle].handle;
							   relocs[j].presumed_offset = bo[tr->target_handle].offset;
						   }
						   relocs[j].delta = tr->delta;
						   relocs[j].offset = tr->offset;
						   relocs[j].read_domains = tr->read_domains;
						   relocs[j].write_domain = tr->write_domain;
					   }
				   }

				   gem_execbuf(fd, &eb);

				   for (i = 0; i < eb.buffer_count; i++)
					   offsets[i]->offset = exec_objects[i].offset;

				   break;
			   }
		}
	} while (ptr < end);
	clock_gettime(CLOCK_MONOTONIC, &t_end);
	close(fd);
	munmap(end-st.st_size, st.st_size);

	for (fd = 0; fd < num_bo; fd++)
		free(bo[fd].relocs);
	free(bo);
	free(offsets);

	printf("%s: %.3f\n", filename, elapsed(&t_start, &t_end));
}

int main(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++)
		replay(argv[i]);

	return 0;
}
