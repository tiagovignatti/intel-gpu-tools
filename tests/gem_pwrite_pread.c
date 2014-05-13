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
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_chipset.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"

#define OBJECT_SIZE 16384

#define COPY_BLT_CMD		(2<<29|0x53<<22)
#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)
#define BLT_SRC_TILED		(1<<15)
#define BLT_DST_TILED		(1<<11)

uint32_t is_64bit;
uint32_t exec_flags;

static inline void build_batch(uint32_t *batch, int len, uint32_t *batch_len)
{
	unsigned int i = 0;

	batch[i++] = COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB | (is_64bit ? 8 : 6);
	batch[i++] = 0xcc << 16 | 1 << 25 | 1 << 24 | len;
	batch[i++] = 0;
	batch[i++] = 1 << 16 | (len / 4);
	batch[i++] = 0; /* dst */
	if (is_64bit)
		batch[i++] = 0;
	batch[i++] = 0;
	batch[i++] = len;
	batch[i++] = 0; /* src */
	if (is_64bit)
		batch[i++] = 0;
	batch[i++] = MI_BATCH_BUFFER_END;
	batch[i++] = 0;

	*batch_len = i * 4;
}

#define BUILD_EXEC \
	uint32_t batch[12]; \
	struct drm_i915_gem_relocation_entry reloc[] = { \
		{ dst, 0, 4*sizeof(uint32_t), 0, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER }, \
		{ src, 0, (is_64bit ? 8 : 7)*sizeof(uint32_t), 0, I915_GEM_DOMAIN_RENDER, 0 }, \
	}; \
	struct drm_i915_gem_exec_object2 exec[] = { \
		{ src }, \
		{ dst }, \
		{ gem_create(fd, 4096), 2, (uintptr_t)reloc } \
	}; \
	struct drm_i915_gem_execbuffer2 execbuf = { \
		(uintptr_t)exec, 3, \
		0, 0, \
		0, 0, 0, 0, \
		exec_flags, \
	}; \
	build_batch(batch, len, &execbuf.batch_len); \
	gem_write(fd, exec[2].handle, 0, batch, execbuf.batch_len);


static void copy(int fd, uint32_t src, uint32_t dst, void *buf, int len, int loops)
{
	BUILD_EXEC;

	while (loops--) {
		gem_write(fd, src, 0, buf, len);
		do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));
		gem_read(fd, dst, 0, buf, len);
	}

	gem_close(fd, exec[2].handle);
}

static void as_gtt_mmap(int fd, uint32_t src, uint32_t dst, void *buf, int len, int loops)
{
	uint32_t *src_ptr, *dst_ptr;
	BUILD_EXEC;

	src_ptr = gem_mmap__gtt(fd, src, OBJECT_SIZE, PROT_WRITE);
	dst_ptr = gem_mmap__gtt(fd, dst, OBJECT_SIZE, PROT_READ);

	while (loops--) {
		gem_set_domain(fd, src,
			       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
		memcpy(src_ptr, buf, len);

		do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));
		gem_set_domain(fd, dst,
			       I915_GEM_DOMAIN_GTT, 0);
		memcpy(buf, dst_ptr, len);
	}

	munmap(dst_ptr, len);
	munmap(src_ptr, len);
	gem_close(fd, exec[2].handle);
}


static void as_cpu_mmap(int fd, uint32_t src, uint32_t dst, void *buf, int len, int loops)
{
	uint32_t *src_ptr, *dst_ptr;
	BUILD_EXEC;

	src_ptr = gem_mmap__cpu(fd, src, OBJECT_SIZE, PROT_WRITE);
	dst_ptr = gem_mmap__cpu(fd, dst, OBJECT_SIZE, PROT_READ);

	while (loops--) {
		gem_set_domain(fd, src,
			       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
		memcpy(src_ptr, buf, len);

		do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));
		gem_set_domain(fd, dst,
			       I915_GEM_DOMAIN_CPU, 0);
		memcpy(buf, dst_ptr, len);
	}

	munmap(dst_ptr, len);
	munmap(src_ptr, len);
	gem_close(fd, exec[2].handle);
}

static void test_copy(int fd, uint32_t src, uint32_t dst, uint32_t *buf, int len)
{
	int i;
	BUILD_EXEC;

	for (i = 0; i < len/4; i++)
		buf[i] = i;

	gem_write(fd, src, 0, buf, len);
	memset(buf, 0, len);

	do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));
	gem_read(fd, dst, 0, buf, len);

	gem_close(fd, exec[2].handle);

	for (i = 0; i < len/4; i++)
		igt_assert(buf[i] == i);
}

static void test_as_gtt_mmap(int fd, uint32_t src, uint32_t dst, int len)
{
	uint32_t *src_ptr, *dst_ptr;
	int i;
	BUILD_EXEC;

	src_ptr = gem_mmap__gtt(fd, src, OBJECT_SIZE, PROT_WRITE);
	dst_ptr = gem_mmap__gtt(fd, dst, OBJECT_SIZE, PROT_READ);

	gem_set_domain(fd, src, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
	for (i = 0; i < len/4; i++)
		src_ptr[i] = i;

	do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));
	gem_close(fd, exec[2].handle);

	gem_set_domain(fd, dst, I915_GEM_DOMAIN_GTT, 0);
	for (i = 0; i < len/4; i++)
		igt_assert(dst_ptr[i] == i);

	munmap(dst_ptr, len);
	munmap(src_ptr, len);
}

static void test_as_cpu_mmap(int fd, uint32_t src, uint32_t dst, int len)
{
	uint32_t *src_ptr, *dst_ptr;
	int i;
	BUILD_EXEC;

	src_ptr = gem_mmap__cpu(fd, src, OBJECT_SIZE, PROT_WRITE);
	dst_ptr = gem_mmap__cpu(fd, dst, OBJECT_SIZE, PROT_READ);

	gem_set_domain(fd, src, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
	for (i = 0; i < len/4; i++)
		src_ptr[i] = i;

	do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));
	gem_close(fd, exec[2].handle);

	gem_set_domain(fd, dst, I915_GEM_DOMAIN_CPU, 0);
	for (i = 0; i < len/4; i++)
		igt_assert(dst_ptr[i] == i);

	munmap(dst_ptr, len);
	munmap(src_ptr, len);
}

static double elapsed(const struct timeval *start,
		      const struct timeval *end,
		      int loop)
{
	return (1e6*(end->tv_sec - start->tv_sec) + (end->tv_usec - start->tv_usec))/loop;
}

static const char *bytes_per_sec(char *buf, double v)
{
	const char *order[] = {
		"",
		"KiB",
		"MiB",
		"GiB",
		"TiB",
		NULL,
	}, **o = order;

	while (v > 1000 && o[1]) {
		v /= 1000;
		o++;
	}
	sprintf(buf, "%.1f%s/s", v, *o);
	return buf;
}

uint32_t *tmp, src, dst;
int fd;

int main(int argc, char **argv)
{
	int object_size = 0;
	uint32_t buf[20];
	int count;

	igt_subtest_init(argc, argv);
	igt_skip_on_simulation();

	if (argc > 1)
		object_size = atoi(argv[1]);
	if (object_size == 0)
		object_size = OBJECT_SIZE;
	object_size = (object_size + 3) & -4;

	igt_fixture {
		uint32_t devid;

		fd = drm_open_any();

		dst = gem_create(fd, object_size);
		src = gem_create(fd, object_size);
		tmp = malloc(object_size);

		gem_set_caching(fd, src, 0);
		gem_set_caching(fd, dst, 0);

		devid = intel_get_drm_devid(fd);
		is_64bit = intel_gen(devid) >= 8;
		exec_flags = HAS_BLT_RING(devid) ? I915_EXEC_BLT : 0;
	}

	igt_subtest("uncached-copy-correctness")
		test_copy(fd, src, dst, tmp, object_size);
	igt_subtest("uncached-copy-performance") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			copy(fd, src, dst, tmp, object_size, count);
			gettimeofday(&end, NULL);
			igt_info("Time to uncached copy %d bytes x %6d:	%7.3fµs, %s\n",
				 object_size, count,
				 elapsed(&start, &end, count),
				 bytes_per_sec((char *)buf, object_size/elapsed(&start, &end, count)*1e6));
			fflush(stdout);
		}
	}

	igt_subtest("uncached-pwrite-blt-gtt_mmap-correctness")
		test_as_gtt_mmap(fd, src, dst, object_size);
	igt_subtest("uncached-pwrite-blt-gtt_mmap-performance") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			as_gtt_mmap(fd, src, dst, tmp, object_size, count);
			gettimeofday(&end, NULL);
			igt_info("** mmap uncached copy %d bytes x %6d:	%7.3fµs, %s\n",
				 object_size, count,
				 elapsed(&start, &end, count),
				 bytes_per_sec((char *)buf, object_size/elapsed(&start, &end, count)*1e6));
			fflush(stdout);
		}
	}

	igt_fixture {
		gem_set_caching(fd, src, 1);
		gem_set_caching(fd, dst, 1);
	}

	igt_subtest("snooped-copy-correctness")
		test_copy(fd, src, dst, tmp, object_size);
	igt_subtest("snooped-copy-performance") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			copy(fd, src, dst, tmp, object_size, count);
			gettimeofday(&end, NULL);
			igt_info("Time to snooped copy %d bytes x %6d:	%7.3fµs, %s\n",
				 object_size, count,
				 elapsed(&start, &end, count),
				 bytes_per_sec((char *)buf, object_size/elapsed(&start, &end, count)*1e6));
			fflush(stdout);
		}
	}

	igt_subtest("snooped-pwrite-blt-cpu_mmap-correctness")
		test_as_cpu_mmap(fd, src, dst, object_size);
	igt_subtest("snooped-pwrite-blt-cpu_mmap-performance") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			as_cpu_mmap(fd, src, dst, tmp, object_size, count);
			gettimeofday(&end, NULL);
			igt_info("** mmap snooped copy %d bytes x %6d:	%7.3fµs, %s\n",
				 object_size, count,
				 elapsed(&start, &end, count),
				 bytes_per_sec((char *)buf, object_size/elapsed(&start, &end, count)*1e6));
			fflush(stdout);
		}
	}

	igt_fixture {
		gem_set_caching(fd, src, 2);
		gem_set_caching(fd, dst, 2);
	}

	igt_subtest("display-copy-correctness")
		test_copy(fd, src, dst, tmp, object_size);
	igt_subtest("display-copy-performance") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			copy(fd, src, dst, tmp, object_size, count);
			gettimeofday(&end, NULL);
			igt_info("Time to display copy %d bytes x %6d:	%7.3fµs, %s\n",
				 object_size, count,
				 elapsed(&start, &end, count),
				 bytes_per_sec((char *)buf, object_size/elapsed(&start, &end, count)*1e6));
			fflush(stdout);
		}
	}

	igt_subtest("display-pwrite-blt-gtt_mmap-correctness")
		test_as_gtt_mmap(fd, src, dst, object_size);
	igt_subtest("display-pwrite-blt-gtt_mmap-performance") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			as_gtt_mmap(fd, src, dst, tmp, object_size, count);
			gettimeofday(&end, NULL);
			igt_info("** mmap display copy %d bytes x %6d:	%7.3fµs, %s\n",
				 object_size, count,
				 elapsed(&start, &end, count),
				 bytes_per_sec((char *)buf, object_size/elapsed(&start, &end, count)*1e6));
			fflush(stdout);
		}
	}

	igt_fixture {
		free(tmp);
		gem_close(fd, src);
		gem_close(fd, dst);

		close(fd);
	}

	igt_exit();
}
