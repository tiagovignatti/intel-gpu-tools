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

#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>

#include "drm.h"

#define LOCAL_I915_EXEC_NO_RELOC (1<<11)
#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

#define COPY_BLT_CMD		(2<<29|0x53<<22|0x6)
#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)
#define BLT_SRC_TILED		(1<<15)
#define BLT_DST_TILED		(1<<11)

#define OBJECT 1024*1024

static int has_64bit_reloc;

static int gem_linear_blt(int fd,
			  uint32_t *batch,
			  int offset,
			  uint32_t src,
			  uint32_t dst,
			  uint32_t length,
			  struct drm_i915_gem_relocation_entry *reloc)
{
	uint32_t *b = batch + offset/4;
	int height = length / (16 * 1024);

	igt_assert_lte(height, 1 << 16);

	if (height) {
		int i = 0;
		b[i++] = COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB;
		if (has_64bit_reloc)
			b[i-1]+=2;
		b[i++] = 0xcc << 16 | 1 << 25 | 1 << 24 | (16*1024);
		b[i++] = 0;
		b[i++] = height << 16 | (4*1024);
		b[i++] = 0;
		reloc->offset = (b-batch+4) * sizeof(uint32_t);
		reloc->delta = 0;
		reloc->target_handle = dst;
		reloc->read_domains = I915_GEM_DOMAIN_RENDER;
		reloc->write_domain = I915_GEM_DOMAIN_RENDER;
		reloc->presumed_offset = 0;
		reloc++;
		if (has_64bit_reloc)
			b[i++] = 0; /* FIXME */

		b[i++] = 0;
		b[i++] = 16*1024;
		b[i++] = 0;
		reloc->offset = (b-batch+7) * sizeof(uint32_t);
		if (has_64bit_reloc)
			reloc->offset += sizeof(uint32_t);
		reloc->delta = 0;
		reloc->target_handle = src;
		reloc->read_domains = I915_GEM_DOMAIN_RENDER;
		reloc->write_domain = 0;
		reloc->presumed_offset = 0;
		reloc++;
		if (has_64bit_reloc)
			b[i++] = 0; /* FIXME */

		b += i;
		length -= height * 16*1024;
	}

	if (length) {
		int i = 0;
		b[i++] = COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB;
		if (has_64bit_reloc)
			b[i-1]+=2;
		b[i++] = 0xcc << 16 | 1 << 25 | 1 << 24 | (16*1024);
		b[i++] = height << 16;
		b[i++] = (1+height) << 16 | (length / 4);
		b[i++] = 0;
		reloc->offset = (b-batch+4) * sizeof(uint32_t);
		reloc->delta = 0;
		reloc->target_handle = dst;
		reloc->read_domains = I915_GEM_DOMAIN_RENDER;
		reloc->write_domain = I915_GEM_DOMAIN_RENDER;
		reloc->presumed_offset = 0;
		reloc++;
		if (has_64bit_reloc)
			b[i++] = 0; /* FIXME */

		b[i++] = height << 16;
		b[i++] = 16*1024;
		b[i++] = 0;
		reloc->offset = (b-batch+7) * sizeof(uint32_t);
		if (has_64bit_reloc)
			reloc->offset += sizeof(uint32_t);
		reloc->delta = 0;
		reloc->target_handle = src;
		reloc->read_domains = I915_GEM_DOMAIN_RENDER;
		reloc->write_domain = 0;
		reloc->presumed_offset = 0;
		reloc++;
		if (has_64bit_reloc)
			b[i++] = 0; /* FIXME */

		b += i;
	}

	b[0] = MI_BATCH_BUFFER_END;
	b[1] = 0;

	return (b+2 - batch) * sizeof(uint32_t);
}

static int __gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *execbuf)
{
	int err = 0;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, execbuf))
		err = -errno;
	return err;
}

static void waiter(int child)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec[3];
	struct drm_i915_gem_relocation_entry reloc[2*4096/32];
	uint32_t *buf, handle, src, dst;
	int fd, len, gen, nreloc, last = 0;
	int ring;

	fd = drm_open_driver(DRIVER_INTEL);
	handle = gem_create(fd, 4096);
	buf = gem_mmap__cpu(fd, handle, 0, 4096, PROT_WRITE);

	gen = intel_gen(intel_get_drm_devid(fd));
	has_64bit_reloc = gen >= 8;

	src = gem_create(fd, OBJECT);
	dst = gem_create(fd, OBJECT);

	len = gem_linear_blt(fd, buf, 0, 0, 1, OBJECT, reloc);

	memset(exec, 0, sizeof(exec));
	exec[0].handle = src;
	exec[1].handle = dst;

	exec[2].handle = handle;
	if (has_64bit_reloc)
		exec[2].relocation_count = len > 56 ? 4 : 2;
	else
		exec[2].relocation_count = len > 40 ? 4 : 2;
	exec[2].relocs_ptr = (uintptr_t)reloc;

	ring = 0;
	if (gen >= 6)
		ring = I915_EXEC_BLT;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)exec;
	execbuf.buffer_count = 3;
	execbuf.batch_len = len;
	execbuf.flags = ring;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;

	if (__gem_execbuf(fd, &execbuf)) {
		gem_set_domain(fd, handle, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
		len = gem_linear_blt(fd, buf, 0, src, dst, OBJECT, reloc);
		igt_assert(len == execbuf.batch_len);
		execbuf.flags = ring;
		gem_execbuf(fd, &execbuf);
	}
	gem_sync(fd, handle);

	if (execbuf.flags & LOCAL_I915_EXEC_HANDLE_LUT) {
		src = 0;
		dst = 1;
	}

	nreloc = exec[2].relocation_count;
	while (execbuf.batch_len + len <= 4096) {
		last = len - 8;
		len = gem_linear_blt(fd, buf, len - 8,
				src, dst, OBJECT,
				reloc + exec[2].relocation_count);
		exec[2].relocation_count += nreloc;
	}
	munmap(buf, 4096);

	execbuf.batch_len = len;
	gem_execbuf(fd, &execbuf);
	usleep(0);

	execbuf.batch_len = len - last;
	execbuf.batch_start_offset = last;
	for (int loop = 0; loop < 16*1024; loop++)
		gem_execbuf(fd, &execbuf);
	usleep(0);

	execbuf.batch_len = len;
	execbuf.batch_start_offset = 0;
	gem_execbuf(fd, &execbuf);

	gem_sync(fd, handle);

	close(fd);
}

static void *thread(void *arg)
{
	uint64_t *c = arg;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while (1)
		__sync_fetch_and_add(c, 1);

	return NULL;
}

static int run(int num_waiters)
{
	int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	pthread_t *threads = calloc(num_cpus, sizeof(pthread_t));
	uint64_t *counters = calloc(num_cpus, 8*sizeof(uint64_t));
	uint64_t count;

	for (int n = 0; n < num_cpus; n++)
		pthread_create(&threads[n], NULL, thread, &counters[8*n]);

	igt_fork(child, num_waiters)
		waiter(child);
	igt_waitchildren();

	count = 0;
	for (int n = 0; n < num_cpus; n++) {
		pthread_cancel(threads[n]);
		__sync_synchronize();
		count += counters[8*n];
	}
	printf("%llu\n", (long long unsigned)count);

	return 0;
}

int main(int argc, char **argv)
{
	int num_waiters = 128;
	int c;

	while ((c = getopt (argc, argv, "w:")) != -1) {
		switch (c) {
		case 'w':
			num_waiters = atoi(optarg);
			if (num_waiters < 1)
				num_waiters = 1;
			break;
		}
	}

	return run(num_waiters);
}
