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
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "drm.h"

#define OBJECT_SIZE 16384

#define COPY_BLT_CMD		(2<<29|0x53<<22|0x6)
#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)
#define BLT_SRC_TILED		(1<<15)
#define BLT_DST_TILED		(1<<11)

static void do_gem_write(int fd, uint32_t handle, void *buf, int len, int loops)
{
	while (loops--)
		gem_write(fd, handle, 0, buf, len);
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

static void test_big_cpu(int fd, int scale)
{
	uint64_t offset, size;
	uint32_t handle;

	size = scale * gem_aperture_size(fd) >> 2;
	intel_require_memory(1, size, CHECK_RAM);

	igt_require(gem_mmap__has_wc(fd));

	handle = gem_create(fd, size);
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);

	for (offset = 0; offset < size; offset += 4096) {
		int suboffset = (offset >> 12) % (4096 - sizeof(offset));
		uint64_t tmp;

		gem_write(fd, handle, offset + suboffset , &offset, sizeof(offset));
		gem_read(fd, handle, offset + suboffset, &tmp, sizeof(tmp));
		igt_assert_eq(offset, tmp);
	}

	gem_close(fd, handle);
}

static void test_big_gtt(int fd, int scale)
{
	uint64_t offset, size;
	uint64_t *ptr;
	uint32_t handle;

	size = scale * gem_aperture_size(fd) >> 2;
	intel_require_memory(1, size, CHECK_RAM);

	igt_require(gem_mmap__has_wc(fd));

	handle = gem_create(fd, size);
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);

	ptr = gem_mmap__wc(fd, handle, 0, size, PROT_READ);

	for (offset = 0; offset < size; offset += 4096) {
		int suboffset = (offset >> 12) % (4096 / sizeof(offset) - 1) * sizeof(offset);

		gem_write(fd, handle, offset + suboffset, &offset, sizeof(offset));
		igt_assert_eq(ptr[(offset + suboffset)/sizeof(offset)], offset);
	}

	munmap(ptr, size);
	gem_close(fd, handle);
}

uint32_t *src, dst;
uint32_t *src_user, dst_stolen;
int fd;

int main(int argc, char **argv)
{
	int object_size = 0;
	double usecs;
	const char* bps;
	char buf[100];
	int count;
	const struct {
		int level;
		const char *name;
	} cache[] = {
		{ 0, "uncached" },
		{ 1, "snoop" },
		{ 2, "display" },
		{ -1 },
	}, *c;

	igt_skip_on_simulation();

	igt_subtest_init(argc, argv);

	if (argc > 1 && atoi(argv[1]))
		object_size = atoi(argv[1]);
	if (object_size == 0)
		object_size = OBJECT_SIZE;
	object_size = (object_size + 3) & -4;

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);

		dst = gem_create(fd, object_size);
		src = malloc(object_size);
		dst_stolen = gem_create_stolen(fd, object_size);
		src_user = malloc(object_size);
	}

	igt_subtest("basic") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			do_gem_write(fd, dst, src, object_size, count);
			gettimeofday(&end, NULL);
			usecs = elapsed(&start, &end, count);
			bps = bytes_per_sec(buf, object_size/usecs*1e6);
			igt_info("Time to pwrite %d bytes x %6d:	%7.3fµs, %s\n",
				 object_size, count, usecs, bps);
			fflush(stdout);
		}
	}

	for (c = cache; c->level != -1; c++) {
		igt_subtest(c->name) {
			gem_set_caching(fd, dst, c->level);

			for (count = 1; count <= 1<<17; count <<= 1) {
				struct timeval start, end;

				gettimeofday(&start, NULL);
				do_gem_write(fd, dst, src, object_size, count);
				gettimeofday(&end, NULL);
				usecs = elapsed(&start, &end, count);
				bps = bytes_per_sec(buf, object_size/usecs*1e6);
				igt_info("Time to %s pwrite %d bytes x %6d:	%7.3fµs, %s\n",
					 c->name, object_size, count, usecs, bps);
				fflush(stdout);
			}
		}
	}

	igt_subtest("stolen-normal") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			do_gem_write(fd, dst_stolen, src_user,
				     object_size, count);
			gettimeofday(&end, NULL);
			usecs = elapsed(&start, &end, count);
			bps = bytes_per_sec(buf, object_size/usecs*1e6);
			igt_info("Time to pwrite %d bytes x %6d:        %7.3fµs, %s\n",
				 object_size, count, usecs, bps);
			fflush(stdout);
		}
	}

	for (c = cache; c->level != -1; c++) {
		igt_subtest_f("stolen-%s", c->name) {
			gem_set_caching(fd, dst, c->level);
			for (count = 1; count <= 1<<17; count <<= 1) {
				struct timeval start, end;

				gettimeofday(&start, NULL);
				do_gem_write(fd, dst_stolen, src_user,
					     object_size, count);
				gettimeofday(&end, NULL);
				bps = bytes_per_sec(buf,
						    object_size/usecs*1e6);
				igt_info("Time to stolen-%s pwrite %d bytes x %6d:     %7.3fµs, %s\n",
					 c->name, object_size, count,
					 usecs, bps);
				fflush(stdout);
			}
		}
	}

	igt_fixture {
		free(src);
		gem_close(fd, dst);
		free(src_user);
		gem_close(fd, dst_stolen);
	}

	igt_subtest("big-cpu")
		test_big_cpu(fd, 3);
	igt_subtest("big-gtt")
		test_big_gtt(fd, 3);

	igt_subtest("huge-cpu")
		test_big_cpu(fd, 6);
	igt_subtest("huge-gtt")
		test_big_gtt(fd, 6);

	igt_fixture
		close(fd);

	igt_exit();
}
