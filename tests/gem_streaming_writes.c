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
#include "igt.h"
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

#define OBJECT_SIZE 1024*1024
#define CHUNK_SIZE 32

#define COPY_BLT_CMD		(2<<29|0x53<<22|0x6)
#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)
#define BLT_WRITE_ARGB (BLT_WRITE_ALPHA | BLT_WRITE_RGB)

#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

IGT_TEST_DESCRIPTION("Test of streaming writes into active GPU sources");

static bool __gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *eb)
{
	return drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, eb) == 0;
}

#define SRC 0
#define DST 1
#define BATCH 2

#define src exec[SRC].handle
#define src_offset exec[SRC].offset
#define dst exec[DST].handle
#define dst_offset exec[DST].offset

static void test_streaming(int fd, int mode, int sync)
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
	exec[SRC].handle = gem_create(fd, OBJECT_SIZE);
	exec[DST].handle = gem_create(fd, OBJECT_SIZE);

	switch (mode) {
	case 0: /* cpu/snoop */
		gem_set_caching(fd, src, I915_CACHING_CACHED);
		s = gem_mmap__cpu(fd, src, 0, OBJECT_SIZE,
				  PROT_READ | PROT_WRITE);
		break;
	case 1: /* gtt */
		s = gem_mmap__gtt(fd, src, OBJECT_SIZE,
				  PROT_READ | PROT_WRITE);
		break;
	case 2: /* wc */
		s = gem_mmap__wc(fd, src, 0, OBJECT_SIZE,
				 PROT_READ | PROT_WRITE);
		break;
	}
	*s = 0; /* fault the object into the mappable range first (for GTT) */

	d = gem_mmap__cpu(fd, dst, 0, OBJECT_SIZE, PROT_READ);

	gem_write(fd, dst, 0, tmp, sizeof(tmp));
	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)exec;
	execbuf.buffer_count = 2;
	execbuf.flags = LOCAL_I915_EXEC_HANDLE_LUT;
	if (!__gem_execbuf(fd, &execbuf)) {
		execbuf.flags = 0;
		igt_require(__gem_execbuf(fd, &execbuf));
	}
	/* We assume that the active objects are fixed to avoid relocations */
	__src_offset = src_offset;
	__dst_offset = dst_offset;

	memset(reloc, 0, sizeof(reloc));
	for (i = 0; i < 64; i++) {
		reloc[2*i+0].offset = 64*i + 4 * sizeof(uint32_t);
		reloc[2*i+0].delta = 0;
		reloc[2*i+0].target_handle = execbuf.flags & LOCAL_I915_EXEC_HANDLE_LUT ? DST : dst;
		reloc[2*i+0].presumed_offset = dst_offset;
		reloc[2*i+0].read_domains = I915_GEM_DOMAIN_RENDER;
		reloc[2*i+0].write_domain = I915_GEM_DOMAIN_RENDER;

		reloc[2*i+1].offset = 64*i + 7 * sizeof(uint32_t);
		if (has_64bit_reloc)
			reloc[2*i+1].offset +=  sizeof(uint32_t);
		reloc[2*i+1].delta = 0;
		reloc[2*i+1].target_handle = execbuf.flags & LOCAL_I915_EXEC_HANDLE_LUT ? SRC : src;
		reloc[2*i+1].presumed_offset = src_offset;
		reloc[2*i+1].read_domains = I915_GEM_DOMAIN_RENDER;
		reloc[2*i+1].write_domain = 0;
	}
	igt_assert(__gem_execbuf(fd, &execbuf));
	igt_assert_eq_u64(__src_offset, src_offset);
	igt_assert_eq_u64(__dst_offset, dst_offset);

	exec[DST].flags = EXEC_OBJECT_WRITE;
	exec[BATCH].relocation_count = 2;
	execbuf.buffer_count = 3;
	execbuf.flags |= I915_EXEC_NO_RELOC;
	if (gem_has_blt(fd))
		execbuf.flags |= I915_EXEC_BLT;

	batch = malloc(sizeof(*batch) * (OBJECT_SIZE / CHUNK_SIZE / 64));
	for (i = n = 0; i < OBJECT_SIZE / CHUNK_SIZE / 64; i++) {
		uint32_t *base;

		batch[i].handle = gem_create(fd, 4096);
		batch[i].offset = 0;

		base = gem_mmap__cpu(fd, batch[i].handle, 0, 4096, PROT_WRITE);
		gem_set_domain(fd, batch[i].handle,
				I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);

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

		if (pass == 0) {
			for (i = 0; i < OBJECT_SIZE/4; i++)
				s[i] = i;
		}

		/* Now copy from the src to the dst in 32byte chunks */
		for (offset = 0; offset < OBJECT_SIZE; offset += CHUNK_SIZE) {
			int b;

			if (pass) {
				if (sync)
					gem_set_domain(fd, src, domain, domain);
				for (i = 0; i < CHUNK_SIZE/4; i++)
					s[offset/4 + i] = (OBJECT_SIZE*pass + offset)/4 + i;
			}

			igt_assert(exec[DST].flags & EXEC_OBJECT_WRITE);

			b = offset / CHUNK_SIZE / 64;
			n = offset / CHUNK_SIZE % 64;
			exec[BATCH].relocs_ptr = (uintptr_t)(reloc + 2*n);
			exec[BATCH].handle = batch[b].handle;
			exec[BATCH].offset = batch[b].offset;
			execbuf.batch_start_offset = 64*n;

			gem_execbuf(fd, &execbuf);
			igt_assert_eq_u64(__src_offset, src_offset);
			igt_assert_eq_u64(__dst_offset, dst_offset);

			batch[b].offset = exec[BATCH].offset;
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

static void test_batch(int fd, int mode, int reverse)
{
	const int has_64bit_reloc = intel_gen(intel_get_drm_devid(fd)) >= 8;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec[3];
	struct drm_i915_gem_relocation_entry reloc[2];
	uint32_t tmp[] = { MI_BATCH_BUFFER_END };
	uint64_t __src_offset, __dst_offset;
	uint64_t batch_size;
	uint32_t *s, *d;
	uint32_t *base;
	uint32_t offset;

	memset(exec, 0, sizeof(exec));
	exec[DST].handle = gem_create(fd, OBJECT_SIZE);
	exec[SRC].handle = gem_create(fd, OBJECT_SIZE);

	s = gem_mmap__wc(fd, src, 0, OBJECT_SIZE, PROT_READ | PROT_WRITE);

	d = gem_mmap__cpu(fd, dst, 0, OBJECT_SIZE, PROT_READ);

	memset(reloc, 0, sizeof(reloc));
	reloc[0].offset =  4 * sizeof(uint32_t);
	reloc[0].delta = 0;
	reloc[0].target_handle = execbuf.flags & LOCAL_I915_EXEC_HANDLE_LUT ? DST : dst;
	reloc[0].presumed_offset = dst_offset;
	reloc[0].read_domains = I915_GEM_DOMAIN_RENDER;
	reloc[0].write_domain = I915_GEM_DOMAIN_RENDER;

	reloc[1].offset = 7 * sizeof(uint32_t);
	if (has_64bit_reloc)
		reloc[1].offset +=  sizeof(uint32_t);
	reloc[1].delta = 0;
	reloc[1].target_handle = execbuf.flags & LOCAL_I915_EXEC_HANDLE_LUT ? SRC : src;
	reloc[1].presumed_offset = src_offset;
	reloc[1].read_domains = I915_GEM_DOMAIN_RENDER;
	reloc[1].write_domain = 0;

	batch_size = ALIGN(OBJECT_SIZE / CHUNK_SIZE * 128, 4096);
	exec[BATCH].relocs_ptr = (uintptr_t)reloc;
	exec[BATCH].relocation_count = 2;
	exec[BATCH].handle = gem_create(fd, batch_size);

	switch (mode) {
	case 0: /* cpu/snoop */
		igt_require(gem_has_llc(fd));
		base = gem_mmap__cpu(fd, exec[BATCH].handle, 0, batch_size,
				     PROT_READ | PROT_WRITE);
		break;
	case 1: /* gtt */
		base = gem_mmap__gtt(fd, exec[BATCH].handle, batch_size,
				     PROT_READ | PROT_WRITE);
		break;
	case 2: /* wc */
		base = gem_mmap__wc(fd, exec[BATCH].handle, 0, batch_size,
				    PROT_READ | PROT_WRITE);
		break;
	}
	*base = 0; /* fault the object into the mappable range first */

	gem_write(fd, exec[BATCH].handle, 0, tmp, sizeof(tmp));
	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)exec;
	execbuf.buffer_count = 3;
	execbuf.flags = LOCAL_I915_EXEC_HANDLE_LUT;
	if (gem_has_blt(fd))
		execbuf.flags |= I915_EXEC_BLT;
	if (!__gem_execbuf(fd, &execbuf)) {
		execbuf.flags &= ~LOCAL_I915_EXEC_HANDLE_LUT;
		gem_execbuf(fd, &execbuf);
	}
	execbuf.flags |= I915_EXEC_NO_RELOC;
	exec[DST].flags = EXEC_OBJECT_WRITE;
	/* We assume that the active objects are fixed to avoid relocations */
	exec[BATCH].relocation_count = 0;
	__src_offset = src_offset;
	__dst_offset = dst_offset;

	offset = mode ? I915_GEM_DOMAIN_GTT : I915_GEM_DOMAIN_CPU;
	gem_set_domain(fd, exec[BATCH].handle, offset, offset);
	for (int pass = 0; pass < 256; pass++) {
		gem_set_domain(fd, src, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
		for (offset = 0; offset < OBJECT_SIZE/4; offset++)
			s[offset] = OBJECT_SIZE*pass/4 + offset;

		/* Now copy from the src to the dst in 32byte chunks */
		for (offset = 0; offset < OBJECT_SIZE / CHUNK_SIZE; offset++) {
			unsigned x = (offset * CHUNK_SIZE) % 4096 >> 2;
			unsigned y = (offset * CHUNK_SIZE) / 4096;
			int k;

			execbuf.batch_start_offset = 128 * offset;
			execbuf.batch_start_offset += 8 * (pass & 7);
			igt_assert(execbuf.batch_start_offset <= batch_size - 64);
			if (reverse)
				execbuf.batch_start_offset = batch_size - execbuf.batch_start_offset - 64;
			igt_assert(execbuf.batch_start_offset <= batch_size - 64);
			k = execbuf.batch_start_offset / 4;

			base[k] = COPY_BLT_CMD | BLT_WRITE_ARGB;
			if (has_64bit_reloc)
				base[k] += 2;
			k++;
			base[k++] = 0xcc << 16 | 1 << 25 | 1 << 24 | 4096;
			base[k++] = (y << 16) | x;
			base[k++] = ((y+1) << 16) | (x + (CHUNK_SIZE >> 2));
			base[k++] = dst_offset;
			if (has_64bit_reloc)
				base[k++] = dst_offset >> 32;
			base[k++] = (y << 16) | x;
			base[k++] = 4096;
			base[k++] = src_offset;
			if (has_64bit_reloc)
				base[k++] = src_offset >> 32;
			base[k++] = MI_BATCH_BUFFER_END;

			igt_assert(exec[DST].flags & EXEC_OBJECT_WRITE);
			gem_execbuf(fd, &execbuf);
			igt_assert_eq_u64(__src_offset, src_offset);
			igt_assert_eq_u64(__dst_offset, dst_offset);
		}

		gem_set_domain(fd, dst, I915_GEM_DOMAIN_CPU, 0);
		for (offset = 0; offset < OBJECT_SIZE/4; offset++)
			igt_assert_eq(pass*OBJECT_SIZE/4 + offset, d[offset]);
	}

	munmap(base, OBJECT_SIZE / CHUNK_SIZE * 128);
	gem_close(fd, exec[BATCH].handle);

	munmap(s, OBJECT_SIZE);
	gem_close(fd, src);
	munmap(d, OBJECT_SIZE);
	gem_close(fd, dst);
}

igt_main
{
	int fd, sync;

	igt_fixture
		fd = drm_open_driver(DRIVER_INTEL);

	for (sync = 2; sync--; ) {
		igt_subtest_f("cpu%s", sync ? "-sync":"")
			test_streaming(fd, 0, sync);
		igt_subtest_f("gtt%s", sync ? "-sync":"")
			test_streaming(fd, 1, sync);
		igt_subtest_f("wc%s", sync ? "-sync":"")
			test_streaming(fd, 2, sync);
	}

	igt_subtest("batch-cpu")
		test_batch(fd, 0, 0);
	igt_subtest("batch-gtt")
		test_batch(fd, 1, 0);
	igt_subtest("batch-wc")
		test_batch(fd, 2, 0);
	igt_subtest("batch-reverse-cpu")
		test_batch(fd, 0, 1);
	igt_subtest("batch-reverse-gtt")
		test_batch(fd, 1, 1);
	igt_subtest("batch-reverse-wc")
		test_batch(fd, 2, 1);

	igt_fixture
		close(fd);
}
