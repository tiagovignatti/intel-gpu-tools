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
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_gpu_tools.h"

#define OBJECT_SIZE 16384

#define COPY_BLT_CMD		(2<<29|0x53<<22|0x6)
#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)
#define BLT_SRC_TILED		(1<<15)
#define BLT_DST_TILED		(1<<11)

static void copy(int fd, uint32_t src, uint32_t dst, void *buf, int len, int loops)
{
	struct drm_i915_gem_relocation_entry reloc[] = {
		{ dst, 0, 4*sizeof(uint32_t), 0, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER },
		{ src, 0, 7*sizeof(uint32_t), 0, I915_GEM_DOMAIN_RENDER, 0 },
	};
	struct drm_i915_gem_exec_object2 exec[] = {
		{ src },
		{ dst },
		{ gem_create(fd, 4096), 2, (uintptr_t)reloc }
	};
	uint32_t batch[] = {
		COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB,
		0xcc << 16 | 1 << 25 | 1 << 24 | len,
		0,
		1 << 16 | (len / 4),
		0, /* dst */
		0,
		len,
		0, /* src */
		MI_BATCH_BUFFER_END,
		0
	};
	struct drm_i915_gem_execbuffer2 execbuf = {
		(uintptr_t)exec, 3,
		0, sizeof(batch),
		0, 0, 0, 0,
		HAS_BLT_RING(intel_get_drm_devid(fd)) ? I915_EXEC_BLT : 0,
	};
	gem_write(fd, exec[2].handle, 0, batch, sizeof(batch));

	while (loops--) {
		gem_write(fd, src, 0, buf, len);
		do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));
		gem_read(fd, dst, 0, buf, len);
	}

	gem_close(fd, exec[2].handle);
}

static void as_gtt_mmap(int fd, uint32_t src, uint32_t dst, void *buf, int len, int loops)
{
	struct drm_i915_gem_relocation_entry reloc[] = {
		{ dst, 0, 4*sizeof(uint32_t), 0, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER },
		{ src, 0, 7*sizeof(uint32_t), 0, I915_GEM_DOMAIN_RENDER, 0 },
	};
	struct drm_i915_gem_exec_object2 exec[] = {
		{ src },
		{ dst },
		{ gem_create(fd, 4096), 2, (uintptr_t)reloc }
	};
	uint32_t batch[] = {
		COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB,
		0xcc << 16 | 1 << 25 | 1 << 24 | len,
		0,
		1 << 16 | (len / 4),
		0, /* dst */
		0,
		len,
		0, /* src */
		MI_BATCH_BUFFER_END,
		0
	};
	struct drm_i915_gem_execbuffer2 execbuf = {
		(uintptr_t)exec, 3,
		0, sizeof(batch),
		0, 0, 0, 0,
		HAS_BLT_RING(intel_get_drm_devid(fd)) ? I915_EXEC_BLT : 0,
	};
	uint32_t *src_ptr, *dst_ptr;

	gem_write(fd, exec[2].handle, 0, batch, sizeof(batch));

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
	struct drm_i915_gem_relocation_entry reloc[] = {
		{ dst, 0, 4*sizeof(uint32_t), 0, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER },
		{ src, 0, 7*sizeof(uint32_t), 0, I915_GEM_DOMAIN_RENDER, 0 },
	};
	struct drm_i915_gem_exec_object2 exec[] = {
		{ src },
		{ dst },
		{ gem_create(fd, 4096), 2, (uintptr_t)reloc }
	};
	uint32_t batch[] = {
		COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB,
		0xcc << 16 | 1 << 25 | 1 << 24 | len,
		0,
		1 << 16 | (len / 4),
		0, /* dst */
		0,
		len,
		0, /* src */
		MI_BATCH_BUFFER_END,
		0
	};
	struct drm_i915_gem_execbuffer2 execbuf = {
		(uintptr_t)exec, 3,
		0, sizeof(batch),
		0, 0, 0, 0,
		HAS_BLT_RING(intel_get_drm_devid(fd)) ? I915_EXEC_BLT : 0,
	};
	uint32_t *src_ptr, *dst_ptr;

	gem_write(fd, exec[2].handle, 0, batch, sizeof(batch));

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
	struct drm_i915_gem_relocation_entry reloc[] = {
		{ dst, 0, 4*sizeof(uint32_t), 0, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER },
		{ src, 0, 7*sizeof(uint32_t), 0, I915_GEM_DOMAIN_RENDER, 0 },
	};
	struct drm_i915_gem_exec_object2 exec[] = {
		{ src },
		{ dst },
		{ gem_create(fd, 4096), 2, (uintptr_t)reloc }
	};
	uint32_t batch[] = {
		COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB,
		0xcc << 16 | 1 << 25 | 1 << 24 | len,
		0,
		1 << 16 | (len / 4),
		0, /* dst */
		0,
		len,
		0, /* src */
		MI_BATCH_BUFFER_END,
		0
	};
	struct drm_i915_gem_execbuffer2 execbuf = {
		(uintptr_t)exec, 3,
		0, sizeof(batch),
		0, 0, 0, 0,
		HAS_BLT_RING(intel_get_drm_devid(fd)) ? I915_EXEC_BLT : 0,
	};
	int i;

	gem_write(fd, exec[2].handle, 0, batch, sizeof(batch));

	for (i = 0; i < len/4; i++)
		buf[i] = i;

	gem_write(fd, src, 0, buf, len);
	memset(buf, 0, len);

	do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));
	gem_read(fd, dst, 0, buf, len);

	gem_close(fd, exec[2].handle);

	for (i = 0; i < len/4; i++)
		assert(buf[i] == i);
}

static void test_as_gtt_mmap(int fd, uint32_t src, uint32_t dst, int len)
{
	struct drm_i915_gem_relocation_entry reloc[] = {
		{ dst, 0, 4*sizeof(uint32_t), 0, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER },
		{ src, 0, 7*sizeof(uint32_t), 0, I915_GEM_DOMAIN_RENDER, 0 },
	};
	struct drm_i915_gem_exec_object2 exec[] = {
		{ src },
		{ dst },
		{ gem_create(fd, 4096), 2, (uintptr_t)reloc }
	};
	uint32_t batch[] = {
		COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB,
		0xcc << 16 | 1 << 25 | 1 << 24 | len,
		0,
		1 << 16 | (len / 4),
		0, /* dst */
		0,
		len,
		0, /* src */
		MI_BATCH_BUFFER_END,
		0
	};
	struct drm_i915_gem_execbuffer2 execbuf = {
		(uintptr_t)exec, 3,
		0, sizeof(batch),
		0, 0, 0, 0,
		HAS_BLT_RING(intel_get_drm_devid(fd)) ? I915_EXEC_BLT : 0,
	};
	uint32_t *src_ptr, *dst_ptr;
	int i;

	gem_write(fd, exec[2].handle, 0, batch, sizeof(batch));

	src_ptr = gem_mmap__gtt(fd, src, OBJECT_SIZE, PROT_WRITE);
	dst_ptr = gem_mmap__gtt(fd, dst, OBJECT_SIZE, PROT_READ);

	gem_set_domain(fd, src, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
	for (i = 0; i < len/4; i++)
		src_ptr[i] = i;

	do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));
	gem_close(fd, exec[2].handle);

	gem_set_domain(fd, dst, I915_GEM_DOMAIN_GTT, 0);
	for (i = 0; i < len/4; i++)
		assert(dst_ptr[i] == i);

	munmap(dst_ptr, len);
	munmap(src_ptr, len);
}

static void test_as_cpu_mmap(int fd, uint32_t src, uint32_t dst, int len)
{
	struct drm_i915_gem_relocation_entry reloc[] = {
		{ dst, 0, 4*sizeof(uint32_t), 0, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER },
		{ src, 0, 7*sizeof(uint32_t), 0, I915_GEM_DOMAIN_RENDER, 0 },
	};
	struct drm_i915_gem_exec_object2 exec[] = {
		{ src },
		{ dst },
		{ gem_create(fd, 4096), 2, (uintptr_t)reloc }
	};
	uint32_t batch[] = {
		COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB,
		0xcc << 16 | 1 << 25 | 1 << 24 | len,
		0,
		1 << 16 | (len / 4),
		0, /* dst */
		0,
		len,
		0, /* src */
		MI_BATCH_BUFFER_END,
		0
	};
	struct drm_i915_gem_execbuffer2 execbuf = {
		(uintptr_t)exec, 3,
		0, sizeof(batch),
		0, 0, 0, 0,
		HAS_BLT_RING(intel_get_drm_devid(fd)) ? I915_EXEC_BLT : 0,
	};
	uint32_t *src_ptr, *dst_ptr;
	int i;

	gem_write(fd, exec[2].handle, 0, batch, sizeof(batch));

	src_ptr = gem_mmap__cpu(fd, src, OBJECT_SIZE, PROT_WRITE);
	dst_ptr = gem_mmap__cpu(fd, dst, OBJECT_SIZE, PROT_READ);

	gem_set_domain(fd, src, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
	for (i = 0; i < len/4; i++)
		src_ptr[i] = i;

	do_or_die(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf));
	gem_close(fd, exec[2].handle);

	gem_set_domain(fd, dst, I915_GEM_DOMAIN_CPU, 0);
	for (i = 0; i < len/4; i++)
		assert(dst_ptr[i] == i);

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

int main(int argc, char **argv)
{
	int object_size = 0;
	uint32_t buf[20];
	uint32_t *tmp, src, dst;
	int fd, count;

	drmtest_subtest_init(argc, argv);
	drmtest_skip_on_simulation();

	if (argc > 1)
		object_size = atoi(argv[1]);
	if (object_size == 0)
		object_size = OBJECT_SIZE;
	object_size = (object_size + 3) & -4;

	fd = drm_open_any();

	dst = gem_create(fd, object_size);
	src = gem_create(fd, object_size);
	tmp = malloc(object_size);

	gem_set_caching(fd, src, 0);
	gem_set_caching(fd, dst, 0);

	drmtest_subtest("uncached-copy-correctness")
		test_copy(fd, src, dst, tmp, object_size);
	drmtest_subtest("uncached-copy-performance") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			copy(fd, src, dst, tmp, object_size, count);
			gettimeofday(&end, NULL);
			printf("Time to uncached copy %d bytes x %6d:	%7.3fµs, %s\n",
			       object_size, count,
			       elapsed(&start, &end, count),
			       bytes_per_sec((char *)buf, object_size/elapsed(&start, &end, count)*1e6));
			fflush(stdout);
		}
	}

	drmtest_subtest("uncached-pwrite-blt-gtt_mmap-correctness")
		test_as_gtt_mmap(fd, src, dst, object_size);
	drmtest_subtest("uncached-pwrite-blt-gtt_mmap-performance") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			as_gtt_mmap(fd, src, dst, tmp, object_size, count);
			gettimeofday(&end, NULL);
			printf("** mmap uncached copy %d bytes x %6d:	%7.3fµs, %s\n",
			       object_size, count,
			       elapsed(&start, &end, count),
			       bytes_per_sec((char *)buf, object_size/elapsed(&start, &end, count)*1e6));
			fflush(stdout);
		}
	}

	gem_set_caching(fd, src, 1);
	gem_set_caching(fd, dst, 1);

	drmtest_subtest("snooped-copy-correctness")
		test_copy(fd, src, dst, tmp, object_size);
	drmtest_subtest("snooped-copy-performance") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			copy(fd, src, dst, tmp, object_size, count);
			gettimeofday(&end, NULL);
			printf("Time to snooped copy %d bytes x %6d:	%7.3fµs, %s\n",
			       object_size, count,
			       elapsed(&start, &end, count),
			       bytes_per_sec((char *)buf, object_size/elapsed(&start, &end, count)*1e6));
			fflush(stdout);
		}
	}

	drmtest_subtest("snooped-pwrite-blt-cpu_mmap-correctness")
		test_as_cpu_mmap(fd, src, dst, object_size);
	drmtest_subtest("snooped-pwrite-blt-cpu_mmap-performance") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			as_cpu_mmap(fd, src, dst, tmp, object_size, count);
			gettimeofday(&end, NULL);
			printf("** mmap snooped copy %d bytes x %6d:	%7.3fµs, %s\n",
			       object_size, count,
			       elapsed(&start, &end, count),
			       bytes_per_sec((char *)buf, object_size/elapsed(&start, &end, count)*1e6));
			fflush(stdout);
		}
	}

	gem_set_caching(fd, src, 2);
	gem_set_caching(fd, dst, 2);

	drmtest_subtest("display-copy-correctness")
		test_copy(fd, src, dst, tmp, object_size);
	drmtest_subtest("display-copy-performance") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			copy(fd, src, dst, tmp, object_size, count);
			gettimeofday(&end, NULL);
			printf("Time to display copy %d bytes x %6d:	%7.3fµs, %s\n",
			       object_size, count,
			       elapsed(&start, &end, count),
			       bytes_per_sec((char *)buf, object_size/elapsed(&start, &end, count)*1e6));
			fflush(stdout);
		}
	}

	drmtest_subtest("display-pwrite-blt-gtt_mmap-correctness")
		test_as_gtt_mmap(fd, src, dst, object_size);
	drmtest_subtest("display-pwrite-blt-gtt_mmap-performance") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			as_gtt_mmap(fd, src, dst, tmp, object_size, count);
			gettimeofday(&end, NULL);
			printf("** mmap display copy %d bytes x %6d:	%7.3fµs, %s\n",
			       object_size, count,
			       elapsed(&start, &end, count),
			       bytes_per_sec((char *)buf, object_size/elapsed(&start, &end, count)*1e6));
			fflush(stdout);
		}
	}

	free(tmp);
	gem_close(fd, src);
	gem_close(fd, dst);

	close(fd);

	return drmtest_retval();
}
