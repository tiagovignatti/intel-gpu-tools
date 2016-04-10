/*
 * Copyright © 2016 Intel Corporation
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
 */

/** @file gem_shrink.c
 *
 * Exercise the shrinker by overallocating GEM objects
 */

#include "igt.h"
#include "igt_gt.h"

#define igt_timeout(T) \
	for (struct timespec t__={}; igt_seconds_elapsed(&t__) < (T); )

#ifndef MADV_FREE
#define MADV_FREE 8
#endif

static void get_pages(int fd, uint64_t alloc)
{
	uint32_t handle = gem_create(fd, alloc);
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, 0);
	gem_madvise(fd, handle, I915_MADV_DONTNEED);
}

static void pwrite_(int fd, uint64_t alloc)
{
	uint32_t tmp;
	uint32_t handle = gem_create(fd, alloc);
	for (int page = 0; page < alloc>>12; page++)
		gem_write(fd, handle, (page + page % 4095) & ~3, &tmp, 4);
	gem_madvise(fd, handle, I915_MADV_DONTNEED);
}

static void pread_(int fd, uint64_t alloc)
{
	uint32_t tmp;
	uint32_t handle = gem_create(fd, alloc);
	for (int page = 0; page < alloc>>12; page++)
		gem_read(fd, handle, (page + page % 4095) & ~3, &tmp, 4);
	gem_madvise(fd, handle, I915_MADV_DONTNEED);
}

static void mmap_gtt(int fd, uint64_t alloc)
{
	uint32_t handle = gem_create(fd, alloc);
	uint32_t *ptr = gem_mmap__gtt(fd, handle, alloc, PROT_WRITE);
	for (int page = 0; page < alloc>>12; page++)
		ptr[page<<10] = 0;
	munmap(ptr, alloc);
	gem_madvise(fd, handle, I915_MADV_DONTNEED);
}

static void mmap_cpu(int fd, uint64_t alloc)
{
	uint32_t handle = gem_create(fd, alloc);
	uint32_t *ptr = gem_mmap__cpu(fd, handle, 0, alloc, PROT_WRITE);
	for (int page = 0; page < alloc>>12; page++)
		ptr[page<<10] = 0;
	munmap(ptr, alloc);
	gem_madvise(fd, handle, I915_MADV_DONTNEED);
}

static void execbuf1(int fd, uint64_t alloc)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_exec_object2 obj;
	struct drm_i915_gem_execbuffer2 execbuf;

	memset(&obj, 0, sizeof(obj));
	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&obj;
	execbuf.buffer_count = 1;

	obj.handle = gem_create(fd, alloc);
	gem_write(fd, obj.handle, 0, &bbe, sizeof(bbe));
	gem_execbuf(fd, &execbuf);
	gem_madvise(fd, obj.handle, I915_MADV_DONTNEED);
}

static void execbufN(int fd, uint64_t alloc)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_exec_object2 *obj;
	struct drm_i915_gem_execbuffer2 execbuf;
	int count = alloc >> 20;

	obj = calloc(alloc + 1, sizeof(*obj));
	memset(&execbuf, 0, sizeof(execbuf));

	obj[count].handle = gem_create(fd, 4096);
	gem_write(fd, obj[count].handle, 0, &bbe, sizeof(bbe));

	for (int i = 1; i <= count; i++) {
		int j = count - i;

		obj[j].handle = gem_create(fd, 1 << 20);
		execbuf.buffers_ptr = (uintptr_t)&obj[j];
		execbuf.buffer_count = i + 1;
		gem_execbuf(fd, &execbuf);
	}

	for (int i = 0; i <= count; i++)
		gem_madvise(fd, obj[i].handle, I915_MADV_DONTNEED);
	free(obj);
}

static void hang(int fd, uint64_t alloc)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_exec_object2 *obj;
	struct drm_i915_gem_execbuffer2 execbuf;
	int count = alloc >> 20;

	obj = calloc(alloc + 1, sizeof(*obj));
	memset(&execbuf, 0, sizeof(execbuf));

	obj[count].handle = gem_create(fd, 4096);
	gem_write(fd, obj[count].handle, 0, &bbe, sizeof(bbe));

	for (int i = 1; i <= count; i++) {
		int j = count - i;

		obj[j].handle = gem_create(fd, 1 << 20);
		execbuf.buffers_ptr = (uintptr_t)&obj[j];
		execbuf.buffer_count = i + 1;
		gem_execbuf(fd, &execbuf);
	}

	gem_close(fd, igt_hang_ring(fd, 0).handle);
	for (int i = 0; i <= count; i++)
		gem_madvise(fd, obj[i].handle, I915_MADV_DONTNEED);
	free(obj);
}

static void userptr(int fd, uint64_t alloc)
{
	struct local_i915_gem_userptr userptr;
	void *ptr;

	igt_assert((alloc & 4095) == 0);

	ptr = mmap(NULL, alloc,
		   PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE,
		   -1, 0);
	igt_assert(ptr != (void *)-1);

	memset(&userptr, 0, sizeof(userptr));
	userptr.user_size = alloc;
	userptr.user_ptr = (uintptr_t)ptr;
	do_ioctl(fd, LOCAL_IOCTL_I915_GEM_USERPTR, &userptr);

	gem_set_domain(fd, userptr.handle, I915_GEM_DOMAIN_GTT, 0);

	madvise(ptr, alloc, MADV_FREE);
}

static bool has_userptr(void)
{
	struct local_i915_gem_userptr userptr;
	int fd = drm_open_driver(DRIVER_INTEL);
	int err;

	memset(&userptr, 0, sizeof(userptr));
	userptr.user_size = 8192;
	userptr.user_ptr = -4096;

	err = 0;
	if (drmIoctl(fd, LOCAL_IOCTL_I915_GEM_USERPTR, &userptr))
		err = errno;

	close(fd);

	return err == EFAULT;
}

static void leak(int fd, uint64_t alloc)
{
	char *ptr;

	ptr = mmap(NULL, alloc, PROT_READ | PROT_WRITE,
		   MAP_ANON | MAP_PRIVATE | MAP_POPULATE,
		   -1, 0);
	if (ptr != (char *)-1)
		return;

	while (alloc) {
		alloc -= 4096;
		ptr[alloc] = 0;
	}
}

#define SOLO 1
#define USERPTR 2
#define OOM 4

static void run_test(int nchildren, uint64_t alloc,
		     void (*func)(int, uint64_t), unsigned flags)
{
	const int timeout = flags & SOLO ? 1 : 20;

	/* Each pass consumes alloc bytes and doesn't drop
	 * its reference to object (i.e. calls
	 * gem_madvise(DONTNEED) instead of gem_close()).
	 * After nchildren passes we expect each process
	 * to have enough objects to consume all of memory
	 * if left unchecked.
	 */

	if (flags & SOLO)
		nchildren = 1;

	/* Background load */
	if (flags & OOM) {
		igt_fork(child, nchildren) {
			igt_timeout(timeout) {
				int fd = drm_open_driver(DRIVER_INTEL);
				for (int pass = 0; pass < nchildren; pass++)
					leak(fd, alloc);
				close(fd);
			}
		}
	}

	if (flags & USERPTR) {
		igt_require(has_userptr());
		igt_fork(child, (nchildren + 1)/2) {
			igt_timeout(timeout) {
				int fd = drm_open_driver(DRIVER_INTEL);
				for (int pass = 0; pass < nchildren; pass++)
					userptr(fd, alloc);
				close(fd);
			}
		}
		nchildren = (nchildren + 1)/2;
	}

	/* Exercise major ioctls */
	igt_fork(child, nchildren) {
		igt_timeout(timeout) {
			int fd = drm_open_driver(DRIVER_INTEL);
			for (int pass = 0; pass < nchildren; pass++)
				func(fd, alloc);
			close(fd);
		}
	}
	igt_waitchildren();
}

igt_main
{
	const struct test {
		const char *name;
		void (*func)(int, uint64_t);
	} tests[] = {
		{ "get-pages", get_pages },
		{ "pwrite", pwrite_ },
		{ "pread", pread_ },
		{ "mmap-gtt", mmap_gtt },
		{ "mmap-cpu", mmap_cpu },
		{ "execbuf1", execbuf1 },
		{ "execbufN", execbufN },
		{ "hang", hang },
		{ NULL },
	};
	const struct mode {
		const char *suffix;
		unsigned flags;
	} modes[] = {
		{ "-sanitycheck", SOLO },
		{ "", 0 },
		{ "-userptr", USERPTR },
		{ "-oom", USERPTR | OOM },
		{ NULL },
	};
	uint64_t alloc_size = 0;
	int num_processes = 0;

	igt_skip_on_simulation();

	igt_fixture {
		uint64_t mem_size = intel_get_total_ram_mb();

		/* Spawn enough processes to use all memory, but each only
		 * uses half the available mappable aperture ~128MiB.
		 * Individually the processes would be ok, but en masse
		 * we expect the shrinker to start purging objects,
		 * and possibly fail.
		 */
		alloc_size = gem_mappable_aperture_size() / 2;
		num_processes = 1 + (mem_size / (alloc_size >> 20));

		igt_info("Using %d processes and %'lluMiB per process\n",
			 num_processes, (long long)(alloc_size >> 20));

		intel_require_memory(num_processes, alloc_size,
				     CHECK_SWAP | CHECK_RAM);
	}

	for(const struct test *t = tests; t->name; t++) {
		for(const struct mode *m = modes; m->suffix; m++) {
			igt_subtest_f("%s%s", t->name, m->suffix)
				run_test(num_processes, alloc_size,
					 t->func, m->flags);
		}
	}
}
