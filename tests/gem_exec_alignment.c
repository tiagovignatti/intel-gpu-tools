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

/* Exercises the basic execbuffer using object alignments */

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

IGT_TEST_DESCRIPTION("Exercises the basic execbuffer using object alignments");

static int __gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *eb)
{
	return drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, eb);
}

static uint32_t find_last_bit(uint64_t x)
{
	uint32_t i = 0;
	while (x) {
		x >>= 1;
		i++;
	}
	return i;
}

static uint32_t file_max(void)
{
	static uint32_t max;
	if (max == 0) {
		FILE *file = fopen("/proc/sys/fs/file-max", "r");
		max = 80000;
		if (file) {
			igt_assert(fscanf(file, "%d", &max) == 1);
			fclose(file);
		}
		max /= 2;
	}
	return max;
}

static void many(int fd)
{
	uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_exec_object2 *execobj;
	struct drm_i915_gem_execbuffer2 execbuf;
	uint64_t gtt_size, ram_size;
	uint64_t alignment, max_alignment, count, i;

	gtt_size = gem_aperture_size(fd);
	ram_size = intel_get_total_ram_mb();
	ram_size *= 1024 * 1024;
	count = ram_size / 4096;
	if (count > file_max()) /* vfs cap */
		count = file_max();
	max_alignment = find_last_bit(gtt_size / count);
	if (max_alignment <= 12)
		max_alignment = 4096;
	else
		max_alignment = 1ull << max_alignment;
	count = gtt_size / max_alignment / 2;

	igt_info("gtt_size=%lld MiB, max-alignment=%lld, count=%lld\n",
		 (long long)gtt_size/1024/1024,
		 (long long)max_alignment,
		 (long long)count);
	intel_require_memory(count, 4096, CHECK_RAM);

	execobj = calloc(sizeof(*execobj), count + 1);
	igt_assert(execobj);

	for (i = 0; i < count; i++) {
		execobj[i].handle = gem_create(fd, 4096);
		if ((gtt_size-1) >> 32)
			execobj[i].flags = 1<<3; /* EXEC_OBJECT_SUPPORTS_48B_ADDRESS */
	}
	execobj[i].handle = gem_create(fd, 4096);
	gem_write(fd, execobj[i].handle, 0, &bbe, sizeof(bbe));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)execobj;
	execbuf.buffer_count = count + 1;
	igt_require(__gem_execbuf(fd, &execbuf) == 0);

	for (alignment = 4096; alignment < gtt_size; alignment <<= 1) {
		for (i = 0; i < count; i++)
			execobj[i].alignment = alignment;
		if (alignment > max_alignment) {
			uint64_t factor = alignment / max_alignment;
			execbuf.buffer_count = count / factor + 1;
			execbuf.buffers_ptr = (uintptr_t)(execobj + (factor - 1) * count / factor);
		}

		igt_debug("testing %lld x alignment=%#llx [%db]\n",
			  (long long)execbuf.buffer_count - 1,
			  (long long)alignment,
			  find_last_bit(alignment));
		gem_execbuf(fd, &execbuf);
		for (i = 0; i < count; i++)
			igt_assert_eq_u64(execobj[i].alignment, alignment);
	}

	for (i = 0; i < count; i++)
		gem_close(fd, execobj[i].handle);
	gem_close(fd, execobj[i].handle);
	free(execobj);
}

static void single(int fd)
{
	struct drm_i915_gem_exec_object2 execobj;
	struct drm_i915_gem_execbuffer2 execbuf;
	uint32_t batch = MI_BATCH_BUFFER_END;
	uint64_t gtt_size;
	int non_pot;

	memset(&execobj, 0, sizeof(execobj));
	execobj.handle = gem_create(fd, 4096);
	execobj.flags = 1<<3; /* EXEC_OBJECT_SUPPORTS_48B_ADDRESS */
	gem_write(fd, execobj.handle, 0, &batch, sizeof(batch));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&execobj;
	execbuf.buffer_count = 1;

	gtt_size = gem_aperture_size(fd);
	if (__gem_execbuf(fd, &execbuf)) {
		execobj.flags = 0;
		gtt_size = 1ull << 32;
	}
	gem_execbuf(fd, &execbuf);

	execobj.alignment = 3*4096;
	non_pot = __gem_execbuf(fd, &execbuf) == 0;
	igt_debug("execbuffer() accepts non-power-of-two alignment? %s\n",
		  non_pot ? "yes" : "no");

	for (execobj.alignment = 4096;
	     execobj.alignment <= 64<<20;
	     execobj.alignment += 4096) {
		if (!non_pot && execobj.alignment & -execobj.alignment)
			continue;

		igt_debug("starting offset: %#llx, next alignment: %#llx\n",
			  (long long)execobj.offset,
			  (long long)execobj.alignment);
		gem_execbuf(fd, &execbuf);
		igt_assert_eq_u64(execobj.offset % execobj.alignment, 0);
	}

	for (execobj.alignment = 4096;
	     execobj.alignment < gtt_size;
	     execobj.alignment <<= 1) {
		igt_debug("starting offset: %#llx, next alignment: %#llx [%db]\n",
			  (long long)execobj.offset,
			  (long long)execobj.alignment,
			  find_last_bit(execobj.alignment));
		gem_execbuf(fd, &execbuf);
		igt_assert_eq_u64(execobj.offset % execobj.alignment, 0);
	}

	gem_close(fd, execobj.handle);
}

igt_simple_main
{
	int fd;

	igt_skip_on_simulation();
	fd = drm_open_driver(DRIVER_INTEL);

	single(fd);
	many(fd);

}
