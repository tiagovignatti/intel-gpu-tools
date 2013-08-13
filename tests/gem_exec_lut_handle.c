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

/* Exercises the basic execbuffer using theh andle LUT interface */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"

#define BATCH_SIZE		(1024*1024)

#define LOCAL_I915_EXEC_NO_RELOC (1<<11)
#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

#define MAX_NUM_EXEC 2048
#define MAX_NUM_RELOC 4096

#define USE_LUT 0x1
#define SKIP_RELOC 0x2
#define NO_RELOC 0x4

struct drm_i915_gem_exec_object2 gem_exec[MAX_NUM_EXEC+1];
struct drm_i915_gem_relocation_entry gem_reloc[MAX_NUM_RELOC];

static uint32_t state = 0x12345678;

static uint32_t
hars_petruska_f54_1_random (void)
{
#define rol(x,k) ((x << k) | (x >> (32-k)))
    return state = (state ^ rol (state, 5) ^ rol (state, 24)) + 0x37798849;
#undef rol
}


static int exec(int fd, int num_exec, int num_relocs, unsigned flags)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 *objects;
	int i;

	gem_exec[MAX_NUM_EXEC].relocation_count = num_relocs;
	gem_exec[MAX_NUM_EXEC].relocs_ptr = (uintptr_t) gem_reloc;

	objects = gem_exec + MAX_NUM_EXEC - num_exec;

	for (i = 0; i < num_relocs; i++) {
		int target = hars_petruska_f54_1_random() % num_exec;
		gem_reloc[i].offset = 1024;
		gem_reloc[i].delta = 0;
		gem_reloc[i].target_handle =
			flags & USE_LUT ? target : objects[target].handle;
		gem_reloc[i].read_domains = I915_GEM_DOMAIN_RENDER;
		gem_reloc[i].write_domain = 0;
		gem_reloc[i].presumed_offset = 0;
		if (flags & SKIP_RELOC)
			gem_reloc[i].presumed_offset = objects[target].offset;
	}

	execbuf.buffers_ptr = (uintptr_t)objects;
	execbuf.buffer_count = num_exec + 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = 8;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = 0;
	if (flags & USE_LUT)
		execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	if (flags & NO_RELOC)
		execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	return drmIoctl(fd,
			DRM_IOCTL_I915_GEM_EXECBUFFER2,
			&execbuf);
}

#define ELAPSED(a,b) (1e6*((b)->tv_sec - (a)->tv_sec) + ((b)->tv_usec - (a)->tv_usec))
int main(int argc, char **argv)
{
	uint32_t batch[2] = {MI_BATCH_BUFFER_END};
	int fd, n, m, count;
	const struct {
		const char *name;
		unsigned int flags;
	} pass[] = {
		{ .name = "relocation", .flags = 0 },
		{ .name = "skip-relocs", .flags = SKIP_RELOC },
		{ .name = "no-relocs", .flags = NO_RELOC },
		{ .name = NULL },
	}, *p;

	igt_skip_on_simulation();

	fd = drm_open_any();

	for (n = 0; n < MAX_NUM_EXEC; n++) {
		gem_exec[n].handle = gem_create(fd, 4096);
		gem_exec[n].relocation_count = 0;
		gem_exec[n].relocs_ptr = 0;
		gem_exec[n].alignment = 0;
		gem_exec[n].offset = 0;
		gem_exec[n].flags = 0;
		gem_exec[n].rsvd1 = 0;
		gem_exec[n].rsvd2 = 0;
	}

	gem_exec[n].handle =  gem_create(fd, 4096);
	gem_write(fd, gem_exec[n].handle, 0, batch, sizeof(batch));

	if (exec(fd, 1, 0, USE_LUT))
		return 77;

	for (p = pass; p->name != NULL; p++) {
		for (n = 1; n <= MAX_NUM_EXEC; n *= 2) {
			for (m = 1; m <= MAX_NUM_RELOC; m *= 2) {
				struct timeval start, end;
				double elapsed[2];

				gettimeofday(&start, NULL);
				for (count = 0; count < 1000; count++)
					do_or_die(exec(fd, n, m, 0 | p->flags));
				gettimeofday(&end, NULL);
				gem_sync(fd, gem_exec[MAX_NUM_EXEC].handle);
				elapsed[0] = ELAPSED(&start, &end) / 1000.;

				gettimeofday(&start, NULL);
				for (count = 0; count < 1000; count++)
					do_or_die(exec(fd, n, m, USE_LUT | p->flags));
				gettimeofday(&end, NULL);
				gem_sync(fd, gem_exec[MAX_NUM_EXEC].handle);
				elapsed[1] = ELAPSED(&start, &end) / 1000.;

				printf("%s: buffer_count=%d, reloc_count=%d: old=%f us, lut=%f us\n",
				       p->name, n, m, elapsed[0], elapsed[1]);
			}
		}
	}

	return 0;
}
