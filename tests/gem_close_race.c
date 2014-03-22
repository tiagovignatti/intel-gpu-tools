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

#include <pthread.h>
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

#define OBJECT_SIZE 1024*1024*4

#define COPY_BLT_CMD		(2<<29|0x53<<22|0x6)
#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)

static char device[80];
static uint32_t devid;
static bool has_64bit_relocations;

static void selfcopy(int fd, uint32_t handle, int loops)
{
	struct drm_i915_gem_relocation_entry reloc[2];
	struct drm_i915_gem_exec_object2 gem_exec[2];
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_pwrite gem_pwrite;
	struct drm_i915_gem_create create;
	uint32_t buf[12], *b = buf;

	memset(reloc, 0, sizeof(reloc));
	memset(gem_exec, 0, sizeof(gem_exec));
	memset(&execbuf, 0, sizeof(execbuf));

	*b = COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB;
	if (has_64bit_relocations)
		*b += 2;
	b++;
	*b++ = 0xcc << 16 | 1 << 25 | 1 << 24 | (4*1024);
	*b++ = 0;
	*b++ = 512 << 16 | 1024;

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

	gem_exec[0].handle = handle;

	create.handle = 0;
	create.size = 4096;
	drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);
	gem_exec[1].handle = create.handle;
	gem_exec[1].relocation_count = 2;
	gem_exec[1].relocs_ptr = (uintptr_t)reloc;

	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 2;
	execbuf.batch_len = (b - buf) * sizeof(*b);
	if (HAS_BLT_RING(devid))
		execbuf.flags |= I915_EXEC_BLT;

	gem_pwrite.handle = gem_exec[1].handle;
	gem_pwrite.offset = 0;
	gem_pwrite.size = execbuf.batch_len;
	gem_pwrite.data_ptr = (uintptr_t)buf;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite) == 0) {
		while (loops--)
			drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
	}

	drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &create.handle);
}

static uint32_t load(int fd)
{
	uint32_t handle;

	handle = gem_create(fd, OBJECT_SIZE);
	if (handle == 0)
		return 0;

	selfcopy(fd, handle, 30);
	return handle;
}

static void run(int child)
{
	uint32_t handle;
	int fd;

	fd = open(device, O_RDWR);
	igt_assert(fd != -1);

	handle = load(fd);
	if ((child & 63) == 63)
		gem_read(fd, handle, 0, &handle, sizeof(handle));
}

#define NUM_FD 768

struct thread {
	pthread_mutex_t mutex;
	int fds[NUM_FD];
	int done;
};

static void *thread_run(void *_data)
{
	struct thread *t = _data;

	pthread_mutex_lock(&t->mutex);
	while (!t->done) {
		pthread_mutex_unlock(&t->mutex);

		for (int n = 0; n < NUM_FD; n++) {
			struct drm_i915_gem_create create;

			create.handle = 0;
			create.size = OBJECT_SIZE;
			drmIoctl(t->fds[n], DRM_IOCTL_I915_GEM_CREATE, &create);
			if (create.handle == 0)
				continue;

			selfcopy(t->fds[n], create.handle, 10);

			drmIoctl(t->fds[n], DRM_IOCTL_GEM_CLOSE, &create.handle);
		}

		pthread_mutex_lock(&t->mutex);
	}
	pthread_mutex_unlock(&t->mutex);

	return 0;
}

static void *thread_busy(void *_data)
{
	struct thread *t = _data;
	int n;

	pthread_mutex_lock(&t->mutex);
	while (!t->done) {
		struct drm_i915_gem_create create;
		struct drm_i915_gem_busy busy;

		pthread_mutex_unlock(&t->mutex);

		n  = rand() % NUM_FD;

		create.handle = 0;
		create.size = OBJECT_SIZE;
		drmIoctl(t->fds[n], DRM_IOCTL_I915_GEM_CREATE, &create);
		if (create.handle == 0)
			continue;

		selfcopy(t->fds[n], create.handle, 1);

		busy.handle = create.handle;
		drmIoctl(t->fds[n], DRM_IOCTL_I915_GEM_BUSY, &busy);

		drmIoctl(t->fds[n], DRM_IOCTL_GEM_CLOSE, &create.handle);

		usleep(10*1000);

		pthread_mutex_lock(&t->mutex);
	}
	pthread_mutex_unlock(&t->mutex);

	return 0;
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
		close(fd);
	}

	igt_subtest("process-exit") {
		igt_fork(child, NUM_FD)
			run(child);
		igt_waitchildren();
	}

	igt_subtest("gem-close-race") {
		pthread_t thread[2];
		struct thread *data = calloc(1, sizeof(struct thread));
		int n;

		igt_assert(data);

		pthread_mutex_init(&data->mutex, NULL);
		for (n = 0; n < NUM_FD; n++)
			data->fds[n] = open(device, O_RDWR);

		pthread_create(&thread[0], NULL, thread_run, data);
		pthread_create(&thread[1], NULL, thread_busy, data);

		for (n = 0; n < 1000*NUM_FD; n++) {
			int i = rand() % NUM_FD;
			if (data->fds[i] == -1) {
				data->fds[i] = open(device, O_RDWR);
			} else{
				close(data->fds[i]);
				data->fds[i] = -1;
			}
		}

		pthread_mutex_lock(&data->mutex);
		data->done = 1;
		pthread_mutex_unlock(&data->mutex);

		pthread_join(thread[1], NULL);
		pthread_join(thread[0], NULL);

		for (n = 0; n < NUM_FD; n++)
			close(data->fds[n]);
		free(data);
	}
}
