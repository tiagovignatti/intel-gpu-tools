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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_aux.h"
#include "intel_chipset.h"

#define OBJECT_SIZE 1024*1024
#define CHUNK_SIZE 32

#define COPY_BLT_CMD		(2<<29|0x53<<22|0x6)
#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)
#define BLT_WRITE_ARGB (BLT_WRITE_ALPHA | BLT_WRITE_RGB)

IGT_TEST_DESCRIPTION("Test of streaming writes into active GPU sources");

static void test_streaming(int fd, int mode)
{
	const int has_64bit_reloc = intel_gen(intel_get_drm_devid(fd)) >= 8;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec[3];
	struct drm_i915_gem_relocation_entry reloc[128];
	uint32_t tmp[] = { MI_BATCH_BUFFER_END };
	uint64_t __src_offset, __dst_offset;
	uint32_t *s, *d;
	uint32_t offset;
	struct {
		uint32_t handle;
		uint64_t offset;
	} *batch;
	int i, n;

	memset(exec, 0, sizeof(exec));
	exec[0].handle = gem_create(fd, OBJECT_SIZE);
#define dst exec[0].handle
#define dst_offset exec[0].offset
	exec[1].handle = gem_create(fd, OBJECT_SIZE);
#define src exec[1].handle
#define src_offset exec[1].offset

	switch (mode) {
	case 0: /* cpu/snoop */
		gem_set_caching(fd, src, I915_CACHING_CACHED);
		s = gem_mmap__cpu(fd, src, 0, OBJECT_SIZE, PROT_READ | PROT_WRITE);
		break;
	case 1: /* gtt */
		s = gem_mmap__gtt(fd, src, OBJECT_SIZE, PROT_READ | PROT_WRITE);
		break;
	case 2: /* wc */
		s = gem_mmap__wc(fd, src, 0, OBJECT_SIZE, PROT_READ | PROT_WRITE);
		break;
	}
	igt_assert(s);

	d = gem_mmap__cpu(fd, dst, 0, OBJECT_SIZE, PROT_READ);
	igt_assert(d);

	gem_write(fd, src, 0, tmp, sizeof(tmp));
	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)exec;
	execbuf.buffer_count = 2;
	gem_execbuf(fd, &execbuf);
	/* We assume that the active objects are fixed to avoid relocations */
	__src_offset = src_offset;
	__dst_offset = dst_offset;

	memset(reloc, 0, sizeof(reloc));
	for (i = 0; i < 64; i++) {
		reloc[2*i+0].offset = 64*i + 4 * sizeof(uint32_t);
		reloc[2*i+0].delta = 0;
		reloc[2*i+0].target_handle = dst;
		reloc[2*i+0].presumed_offset = dst_offset;
		reloc[2*i+0].read_domains = I915_GEM_DOMAIN_RENDER;
		reloc[2*i+0].write_domain = I915_GEM_DOMAIN_RENDER;

		reloc[2*i+1].offset = 64*i + 7 * sizeof(uint32_t);
		if (has_64bit_reloc)
			reloc[2*i+1].offset +=  sizeof(uint32_t);
		reloc[2*i+1].delta = 0;
		reloc[2*i+1].target_handle = src;
		reloc[2*i+1].presumed_offset = src_offset;
		reloc[2*i+1].read_domains = I915_GEM_DOMAIN_RENDER;
		reloc[2*i+1].write_domain = 0;
	}

	exec[2].relocation_count = 2;
	execbuf.buffer_count = 3;
	execbuf.flags = I915_EXEC_NO_RELOC;
	if (gem_has_blt(fd))
		execbuf.flags |= I915_EXEC_BLT;

	batch = malloc(sizeof(*batch) * (OBJECT_SIZE / CHUNK_SIZE / 64));
	for (i = n = 0; i < OBJECT_SIZE / CHUNK_SIZE / 64; i++) {
		uint32_t *base;

		batch[i].handle = gem_create(fd, 4096);
		batch[i].offset = 0;

		base = gem_mmap__cpu(fd, batch[i].handle, 0, 4096, PROT_WRITE);
		igt_assert(base);

		for (int j = 0; j < 64; j++) {
			unsigned x = (n * CHUNK_SIZE) % 4096 >> 2;
			unsigned y = (n * CHUNK_SIZE) / 4096;
			uint32_t *b = base + 16 * j;
			int k = 0;

			b[k] = COPY_BLT_CMD | BLT_WRITE_ARGB;
			if (has_64bit_reloc)
				b[k] += 2;
			k++;
			b[k++] = 0xcc << 16 | 1 << 25 | 1 << 24 | 4096;
			b[k++] = (y << 16) | x;
			b[k++] = ((y+1) << 16) | (x + (CHUNK_SIZE >> 2));
			b[k++] = dst_offset;
			if (has_64bit_reloc)
				b[k++] = dst_offset >> 32;
			b[k++] = (y << 16) | x;
			b[k++] = 4096;
			b[k++] = src_offset;
			if (has_64bit_reloc)
				b[k++] = src_offset >> 32;
			b[k++] = MI_BATCH_BUFFER_END;

			n++;
		}

		munmap(base, 4096);
	}

	for (int pass = 0; pass < 256; pass++) {
		int domain = mode ? I915_GEM_DOMAIN_GTT : I915_GEM_DOMAIN_CPU;
		gem_set_domain(fd, src, domain, domain);

		/* Now copy from the src to the dst in 32byte chunks */
		for (offset = 0; offset < OBJECT_SIZE; offset += CHUNK_SIZE) {
			int b;

			for (i = 0; i < CHUNK_SIZE/4; i++)
				s[offset/4 + i] = (OBJECT_SIZE*pass + offset)/4 + i;

			b = offset / CHUNK_SIZE / 64;
			n = offset / CHUNK_SIZE % 64;
			exec[2].relocs_ptr = (uintptr_t)(reloc + 2*n);
			exec[2].handle = batch[b].handle;
			exec[2].offset = batch[b].offset;
			execbuf.batch_start_offset = 64*n;

			gem_execbuf(fd, &execbuf);
			igt_assert(__src_offset == src_offset);
			igt_assert(__dst_offset == dst_offset);

			batch[b].offset = exec[2].offset;
		}

		gem_set_domain(fd, dst, I915_GEM_DOMAIN_CPU, 0);
		for (offset = 0; offset < OBJECT_SIZE/4; offset++)
			igt_assert_eq(pass*OBJECT_SIZE/4 + offset, d[offset]);
	}

	for (i = 0; i < OBJECT_SIZE / CHUNK_SIZE / 64; i++)
		gem_close(fd, batch[i].handle);
	free(batch);

	munmap(s, OBJECT_SIZE);
	gem_close(fd, src);
	munmap(d, OBJECT_SIZE);
	gem_close(fd, dst);
}

igt_main
{
	int fd;

	igt_fixture
		fd = drm_open_any();

	igt_subtest("cpu")
		test_streaming(fd, 0);
	igt_subtest("gtt")
		test_streaming(fd, 1);
	igt_subtest("wc")
		test_streaming(fd, 2);

	igt_fixture
		close(fd);
}
