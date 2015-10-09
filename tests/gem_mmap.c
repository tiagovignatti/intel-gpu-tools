/*
 * Copyright © 2008 Intel Corporation
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
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "drm.h"

#define OBJECT_SIZE 16384
#define PAGE_SIZE 4096
int fd;

static void
test_huge_bo(int huge)
{
	uint64_t huge_object_size, last_offset, i;
	unsigned check = CHECK_RAM;
	char *ptr_cpu;
	char *cpu_pattern;
	uint32_t bo;
	int loop;

	switch (huge) {
	case -1:
		huge_object_size = gem_mappable_aperture_size() / 2;
		break;
	case 0:
		huge_object_size = gem_mappable_aperture_size() + PAGE_SIZE;
		break;
	case 1:
		huge_object_size = gem_aperture_size(fd) + PAGE_SIZE;
		break;
	case 2:
		huge_object_size = (intel_get_total_ram_mb() + 1) << 20;
		check |= CHECK_SWAP;
		break;
	default:
		return;
	}
	intel_require_memory(1, huge_object_size, check);

	last_offset = huge_object_size - PAGE_SIZE;

	cpu_pattern = malloc(PAGE_SIZE);
	igt_assert(cpu_pattern);
	for (i = 0; i < PAGE_SIZE; i++)
		cpu_pattern[i] = i;

	bo = gem_create(fd, huge_object_size);

	/* Obtain CPU mapping for the object. */
	ptr_cpu = __gem_mmap__cpu(fd, bo, 0, huge_object_size,
				PROT_READ | PROT_WRITE);
	igt_require(ptr_cpu);
	gem_set_domain(fd, bo, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
	gem_close(fd, bo);

	igt_debug("Exercising %'llu bytes\n", (long long)huge_object_size);

	loop = 0;
	do {
		/* Write first page through the mapping and
		 * assert reading it back works.
		 */
		memcpy(ptr_cpu, cpu_pattern, PAGE_SIZE);
		igt_assert(memcmp(ptr_cpu, cpu_pattern, PAGE_SIZE) == 0);
		memset(ptr_cpu, 0xcc, PAGE_SIZE);

		/* Write last page through the mapping and
		 * assert reading it back works.
		 */
		memcpy(ptr_cpu + last_offset, cpu_pattern, PAGE_SIZE);
		igt_assert(memcmp(ptr_cpu + last_offset, cpu_pattern, PAGE_SIZE) == 0);
		memset(ptr_cpu + last_offset, 0xcc, PAGE_SIZE);

		/* Cross check that accessing two simultaneous pages works. */
		igt_assert(memcmp(ptr_cpu, ptr_cpu + last_offset, PAGE_SIZE) == 0);

		/* Force every page to be faulted and retest */
		for (i = 0; i < huge_object_size; i += 4096)
			ptr_cpu[i] = i >> 12;
	} while (loop++ == 0);

	munmap(ptr_cpu, huge_object_size);
	free(cpu_pattern);
}

igt_main
{
	struct drm_i915_gem_mmap arg;
	uint8_t expected[OBJECT_SIZE];
	uint8_t buf[OBJECT_SIZE];
	uint8_t *addr;
	int ret;

	igt_fixture
		fd = drm_open_driver(DRIVER_INTEL);

	igt_subtest("bad-object") {
		memset(&arg, 0, sizeof(arg));
		arg.handle = 0x10101010;
		arg.offset = 0;
		arg.size = 4096;
		ret = ioctl(fd, DRM_IOCTL_I915_GEM_MMAP, &arg);
		igt_assert(ret == -1 && errno == ENOENT);
	}

	igt_subtest("basic") {
		arg.handle = gem_create(fd, OBJECT_SIZE);
		arg.offset = 0;
		arg.size = OBJECT_SIZE;
		ret = ioctl(fd, DRM_IOCTL_I915_GEM_MMAP, &arg);
		igt_assert(ret == 0);
		addr = (uint8_t *)(uintptr_t)arg.addr_ptr;

		igt_info("Testing contents of newly created object.\n");
		memset(expected, 0, sizeof(expected));
		igt_assert(memcmp(addr, expected, sizeof(expected)) == 0);

		igt_info("Testing coherency of writes and mmap reads.\n");
		memset(buf, 0, sizeof(buf));
		memset(buf + 1024, 0x01, 1024);
		memset(expected + 1024, 0x01, 1024);
		gem_write(fd, arg.handle, 0, buf, OBJECT_SIZE);
		igt_assert(memcmp(buf, addr, sizeof(buf)) == 0);

		igt_info("Testing that mapping stays after close\n");
		gem_close(fd, arg.handle);
		igt_assert(memcmp(buf, addr, sizeof(buf)) == 0);

		igt_info("Testing unmapping\n");
		munmap(addr, OBJECT_SIZE);
	}

	igt_subtest("short-mmap") {
		igt_assert(OBJECT_SIZE > 4096);
		arg.handle = gem_create(fd, OBJECT_SIZE);
		addr = __gem_mmap__cpu(fd, arg.handle, 0, 4096, PROT_WRITE);
		igt_assert(addr);
		memset(addr, 0, 4096);
		munmap(addr, 4096);
		gem_close(fd, arg.handle);
	}

	igt_subtest("basic-small-bo")
		test_huge_bo(-1);
	igt_subtest("big-bo")
		test_huge_bo(0);
	igt_subtest("huge-bo")
		test_huge_bo(1);
	igt_subtest("swap-bo")
		test_huge_bo(2);

	igt_fixture
		close(fd);
}
