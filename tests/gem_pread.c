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
#define LARGE_OBJECT_SIZE 1024 * 1024
#define KGRN "\x1B[32m"
#define KRED "\x1B[31m"
#define KNRM "\x1B[0m"

static void do_gem_read(int fd, uint32_t handle, void *buf, int len, int loops)
{
	while (loops--)
		gem_read(fd, handle, 0, buf, len);
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


uint32_t *src, dst;
uint32_t *dst_user, src_stolen, large_stolen;
uint32_t *stolen_pf_user, *stolen_nopf_user;
int fd, count;

int main(int argc, char **argv)
{
	int object_size = 0;
	double usecs;
	char buf[100];
	const char* bps;
	const struct {
		int level;
		const char *name;
	} cache[] = {
		{ 0, "uncached" },
		{ 1, "snoop" },
		{ 2, "display" },
		{ -1 },
	}, *c;

	igt_subtest_init(argc, argv);
	igt_skip_on_simulation();

	if (argc > 1 && atoi(argv[1]))
		object_size = atoi(argv[1]);
	if (object_size == 0)
		object_size = OBJECT_SIZE;
	object_size = (object_size + 3) & -4;

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);

		dst = gem_create(fd, object_size);
		src = malloc(object_size);
		src_stolen = gem_create_stolen(fd, object_size);
		dst_user = malloc(object_size);
	}

	igt_subtest("basic") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			do_gem_read(fd, dst, src, object_size, count);
			gettimeofday(&end, NULL);
			usecs = elapsed(&start, &end, count);
			bps = bytes_per_sec(buf, object_size/usecs*1e6);
			igt_info("Time to pread %d bytes x %6d:	%7.3fµs, %s\n",
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
				do_gem_read(fd, dst, src, object_size, count);
				gettimeofday(&end, NULL);
				usecs = elapsed(&start, &end, count);
				bps = bytes_per_sec(buf, object_size/usecs*1e6);
				igt_info("Time to %s pread %d bytes x %6d:	%7.3fµs, %s\n",
					 c->name, object_size, count, usecs, bps);
				fflush(stdout);
			}
		}
	}

	igt_subtest("stolen-normal") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			do_gem_read(fd, src_stolen, dst_user, object_size, count);
			gettimeofday(&end, NULL);
			usecs = elapsed(&start, &end, count);
			bps = bytes_per_sec(buf, object_size/usecs*1e6);
			igt_info("Time to pread %d bytes x %6d:	%7.3fµs, %s\n",
				 object_size, count, usecs, bps);
			fflush(stdout);
		}
	}
	for (c = cache; c->level != -1; c++) {
		igt_subtest_f("stolen-%s", c->name) {
			gem_set_caching(fd, src_stolen, c->level);

			for (count = 1; count <= 1<<17; count <<= 1) {
				struct timeval start, end;

				gettimeofday(&start, NULL);
				do_gem_read(fd, src_stolen, dst_user,
					    object_size, count);
				gettimeofday(&end, NULL);
				usecs = elapsed(&start, &end, count);
				bps = bytes_per_sec(buf, object_size/usecs*1e6);
				igt_info("Time to stolen-%s pread %d bytes x %6d:      %7.3fµs, %s\n",
					 c->name, object_size, count, usecs, bps);
				fflush(stdout);
			}
		}
	}

	/* List the time taken in pread operation for stolen objects, with
	 * and without the overhead of page fault handling on accessing the
	 * user space buffer
	 */
	igt_subtest("pagefault-pread") {
		large_stolen = gem_create_stolen(fd, LARGE_OBJECT_SIZE);
		stolen_nopf_user = (uint32_t *) mmap(NULL, LARGE_OBJECT_SIZE,
						PROT_WRITE,
						MAP_ANONYMOUS|MAP_PRIVATE,
						-1, 0);
		igt_assert(stolen_nopf_user);

		for (count = 1; count <= 10; count ++) {
			struct timeval start, end;
			double t_elapsed = 0;

			gettimeofday(&start, NULL);
			do_gem_read(fd, large_stolen, stolen_nopf_user,
				    LARGE_OBJECT_SIZE, 1);
			gettimeofday(&end, NULL);
			t_elapsed = elapsed(&start, &end, count);
			bps = bytes_per_sec(buf, object_size/t_elapsed*1e6);
			igt_info("Pagefault-N - Time to pread %d bytes: %7.3fµs, %s\n",
				 LARGE_OBJECT_SIZE, t_elapsed, bps);

			stolen_pf_user = (uint32_t *) mmap(NULL, LARGE_OBJECT_SIZE,
						      PROT_WRITE,
						      MAP_ANONYMOUS|MAP_PRIVATE,
						      -1, 0);
			igt_assert(stolen_pf_user);

			gettimeofday(&start, NULL);
			do_gem_read(fd, large_stolen, stolen_pf_user,
				    LARGE_OBJECT_SIZE, 1);
			gettimeofday(&end, NULL);
			usecs = elapsed(&start, &end, count);
			bps = bytes_per_sec(buf, object_size/usecs*1e6);
			igt_info("Pagefault-Y - Time to pread %d bytes: %7.3fµs, %s%s%s\n",
				 LARGE_OBJECT_SIZE, usecs,
				 t_elapsed < usecs ? KGRN : KRED, bps, KNRM);
			fflush(stdout);
			munmap(stolen_pf_user, LARGE_OBJECT_SIZE);
		}
		munmap(stolen_nopf_user, LARGE_OBJECT_SIZE);
		gem_close(fd, large_stolen);
	}


	igt_fixture {
		free(src);
		gem_close(fd, dst);
		free(dst_user);
		gem_close(fd, src_stolen);

		close(fd);
	}

	igt_exit();
}
