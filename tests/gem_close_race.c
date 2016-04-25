/*
 * Copyright © 2013 Intel Corporation
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
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
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
#include <sys/syscall.h>
#include "drm.h"

#define OBJECT_SIZE (256 * 1024)

#define COPY_BLT_CMD		(2<<29|0x53<<22|0x6)
#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)

static char device[80];
static uint32_t devid;
static bool has_64bit_relocations;

#define gettid() syscall(__NR_gettid)
#define sigev_notify_thread_id _sigev_un._tid

static void selfcopy(int fd, uint32_t handle, int loops)
{
	struct drm_i915_gem_relocation_entry reloc[2];
	struct drm_i915_gem_exec_object2 gem_exec[2];
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_pwrite gem_pwrite;
	struct drm_i915_gem_create create;
	uint32_t buf[16], *b = buf;

	memset(reloc, 0, sizeof(reloc));

	*b = COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB;
	if (has_64bit_relocations)
		*b += 2;
	b++;
	*b++ = 0xcc << 16 | 1 << 25 | 1 << 24 | (4*1024);
	*b++ = 0;
	*b++ = 1 << 16 | 1024;

	reloc[0].offset = (b - buf) * sizeof(*b);
	reloc[0].target_handle = handle;
	reloc[0].read_domains = I915_GEM_DOMAIN_RENDER;
	reloc[0].write_domain = I915_GEM_DOMAIN_RENDER;
	*b++ = 0;
	if (has_64bit_relocations)
		*b++ = 0;

	*b++ = 512 << 16;
	*b++ = 4*1024;

	reloc[1].offset = (b - buf) * sizeof(*b);
	reloc[1].target_handle = handle;
	reloc[1].read_domains = I915_GEM_DOMAIN_RENDER;
	reloc[1].write_domain = 0;
	*b++ = 0;
	if (has_64bit_relocations)
		*b++ = 0;

	*b++ = MI_BATCH_BUFFER_END;
	*b++ = 0;

	memset(gem_exec, 0, sizeof(gem_exec));
	gem_exec[0].handle = handle;

	memset(&create, 0, sizeof(create));
	create.handle = 0;
	create.size = 4096;
	drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);
	gem_exec[1].handle = create.handle;
	gem_exec[1].relocation_count = 2;
	gem_exec[1].relocs_ptr = (uintptr_t)reloc;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 2;
	execbuf.batch_len = (b - buf) * sizeof(*b);
	if (HAS_BLT_RING(devid))
		execbuf.flags |= I915_EXEC_BLT;

	memset(&gem_pwrite, 0, sizeof(gem_pwrite));
	gem_pwrite.handle = create.handle;
	gem_pwrite.offset = 0;
	gem_pwrite.size = sizeof(buf);
	gem_pwrite.data_ptr = (uintptr_t)buf;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite) == 0) {
		while (loops-- &&
		       drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf) == 0)
			;
	}

	drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &create.handle);
}

static uint32_t load(int fd)
{
	uint32_t handle;

	handle = gem_create(fd, OBJECT_SIZE);
	if (handle == 0)
		return 0;

	selfcopy(fd, handle, 100);
	return handle;
}

static void process(int child)
{
	uint32_t handle;
	int fd;

	fd = open(device, O_RDWR);
	igt_assert_neq(fd, -1);

	handle = load(fd);
	if ((child & 63) == 63)
		gem_read(fd, handle, 0, &handle, sizeof(handle));
}

struct crashme {
	int fd;
} crashme;

static void crashme_now(int sig)
{
	close(crashme.fd);
}

#define usec(x) (1000*(x))
#define msec(x) usec(1000*(x))

static void threads(int timeout)
{
	struct sigevent sev;
	struct sigaction act;
	struct drm_gem_open name;
	struct itimerspec its;
	timer_t timer;
	int fd;

	memset(&act, 0, sizeof(act));
	act.sa_handler = crashme_now;
	igt_assert(sigaction(SIGRTMIN, &act, NULL) == 0);

	memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL | SIGEV_THREAD_ID;
	sev.sigev_notify_thread_id = gettid();
	sev.sigev_signo = SIGRTMIN;
	igt_assert(timer_create(CLOCK_MONOTONIC, &sev, &timer) == 0);

	fd = open(device, O_RDWR);
	name.name = gem_flink(fd, gem_create(fd, OBJECT_SIZE));

	igt_timeout(timeout) {
		crashme.fd = open(device, O_RDWR);

		memset(&its, 0, sizeof(its));
		its.it_value.tv_nsec = msec(1) + (rand() % msec(10));
		igt_assert(timer_settime(timer, 0, &its, NULL) == 0);

		do {
			if (drmIoctl(crashme.fd, DRM_IOCTL_GEM_OPEN, &name))
				break;

			selfcopy(crashme.fd, name.handle, 100);
			drmIoctl(crashme.fd, DRM_IOCTL_GEM_CLOSE, &name.handle);
		} while (1);
	}

	timer_delete(timer);
	close(fd);
}

igt_main
{
	igt_skip_on_simulation();

	igt_fixture {
		int fd;

		sprintf(device, "/dev/dri/card%d", drm_get_card());
		fd = open(device, O_RDWR);

		igt_assert(fd != -1);
		devid = intel_get_drm_devid(fd);
		has_64bit_relocations = intel_gen(devid) >= 8;

		igt_fork_hang_detector(fd);
		close(fd);
	}

	igt_subtest("basic-process") {
		igt_fork(child, 1)
			process(child);
		igt_waitchildren();
	}

	igt_subtest("basic-threads")
		threads(10);

	igt_subtest("process-exit") {
		igt_fork(child, 768)
			process(child);
		igt_waitchildren();
	}

	igt_subtest("gem-close-race")
		threads(150);

	igt_stop_hang_detector();
}
