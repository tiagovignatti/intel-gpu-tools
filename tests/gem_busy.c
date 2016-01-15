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
 */

#include "igt.h"

/* Exercise the busy-ioctl, ensuring the ABI is never broken */
IGT_TEST_DESCRIPTION("Basic check of busy-ioctl ABI.");

enum { TEST = 0, BUSY, BATCH };

static void __gem_busy(int fd,
		       uint32_t handle,
		       uint32_t *read,
		       uint32_t *write)
{
	struct drm_i915_gem_busy busy;

	memset(&busy, 0, sizeof(busy));
	busy.handle = handle;

	do_ioctl(fd, DRM_IOCTL_I915_GEM_BUSY, &busy);

	*write = busy.busy & 0xffff;
	*read = busy.busy >> 16;
}

static uint32_t busy_blt(int fd)
{
	const int gen = intel_gen(intel_get_drm_devid(fd));
	const int has_64bit_reloc = gen >= 8;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 object[2];
	struct drm_i915_gem_relocation_entry reloc[200], *r;
	uint32_t read, write;
	uint32_t *map;
	int factor = 100;
	int i = 0;

	memset(object, 0, sizeof(object));
	object[0].handle = gem_create(fd, 1024*1024);
	object[1].handle = gem_create(fd, 4096);

	r = memset(reloc, 0, sizeof(reloc));
	map = gem_mmap__cpu(fd, object[1].handle, 0, 4096, PROT_WRITE);
	gem_set_domain(fd, object[1].handle,
		       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);

#define COPY_BLT_CMD		(2<<29|0x53<<22|0x6)
#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)
	while (factor--) {
		/* XY_SRC_COPY */
		map[i++] = COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB;
		if (has_64bit_reloc)
			map[i-1] += 2;
		map[i++] = 0xcc << 16 | 1 << 25 | 1 << 24 | (4*1024);
		map[i++] = 0;
		map[i++] = 256 << 16 | 1024;

		r->offset = i * sizeof(uint32_t);
		r->target_handle = object[0].handle;
		r->read_domains = I915_GEM_DOMAIN_RENDER;
		r->write_domain = I915_GEM_DOMAIN_RENDER;
		r++;
		map[i++] = 0;
		if (has_64bit_reloc)
			map[i++] = 0;

		map[i++] = 0;
		map[i++] = 4096;

		r->offset = i * sizeof(uint32_t);
		r->target_handle = object[0].handle;
		r->read_domains = I915_GEM_DOMAIN_RENDER;
		r->write_domain = 0;
		r++;
		map[i++] = 0;
		if (has_64bit_reloc)
			map[i++] = 0;
	}
	map[i++] = MI_BATCH_BUFFER_END;
	igt_assert(i <= 4096/sizeof(uint32_t));
	igt_assert(r - reloc <= ARRAY_SIZE(reloc));
	munmap(map, 4096);

	object[1].relocs_ptr = (uintptr_t)reloc;
	object[1].relocation_count = r - reloc;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (unsigned long)object;
	execbuf.buffer_count = 2;
	if (gen >= 6)
		execbuf.flags = I915_EXEC_BLT;
	gem_execbuf(fd, &execbuf);

	__gem_busy(fd, object[0].handle, &read, &write);
	igt_assert_eq(read, 1 << write);
	igt_assert_eq(write, gen >= 6 ? I915_EXEC_BLT : I915_EXEC_RENDER);

	igt_debug("Created busy handle %d\n", object[0].handle);
	gem_close(fd, object[1].handle);
	return object[0].handle;
}

static int __gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *eb)
{
	int err = 0;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, eb))
		err = -errno;
	return err;
}

static bool exec_noop(int fd,
		      uint32_t *handles,
		      unsigned ring,
		      bool write)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec[3];

	memset(exec, 0, sizeof(exec));
	exec[0].handle = handles[BUSY];
	exec[1].handle = handles[TEST];
	if (write)
		exec[1].flags |= EXEC_OBJECT_WRITE;
	exec[2].handle = handles[BATCH];

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)exec;
	execbuf.buffer_count = 3;
	execbuf.flags = ring;
	igt_debug("Queuing handle for %s on ring %d\n",
		  write ? "writing" : "reading", ring & 0x7);
	return __gem_execbuf(fd, &execbuf) == 0;
}

static bool still_busy(int fd, uint32_t handle)
{
	uint32_t read, write;
	__gem_busy(fd, handle, &read, &write);
	return write;
}

static void test_ring(int fd, unsigned ring, uint32_t flags)
{
	uint32_t bbe = MI_BATCH_BUFFER_END;
	uint32_t handle[3];
	uint32_t read, write;
	uint32_t active;
	unsigned i;

	handle[TEST] = gem_create(fd, 4096);
	handle[BATCH] = gem_create(fd, 4096);
	gem_write(fd, handle[BATCH], 0, &bbe, sizeof(bbe));

	/* Create a long running batch which we can use to hog the GPU */
	handle[BUSY] = busy_blt(fd);

	/* Queue a batch after the busy, it should block and remain "busy" */
	igt_require(exec_noop(fd, handle, ring | flags, false));
	igt_assert(still_busy(fd, handle[BUSY]));
	__gem_busy(fd, handle[TEST], &read, &write);
	igt_assert_eq(read, 1 << ring);
	igt_assert_eq(write, 0);

	/* Requeue with a write */
	igt_require(exec_noop(fd, handle, ring | flags, true));
	igt_assert(still_busy(fd, handle[BUSY]));
	__gem_busy(fd, handle[TEST], &read, &write);
	igt_assert_eq(read, 1 << ring);
	igt_assert_eq(write, ring);

	/* Now queue it for a read across all available rings */
	active = 0;
	for (i = I915_EXEC_RENDER; i <= I915_EXEC_VEBOX; i++) {
		if (exec_noop(fd, handle, i | flags, false))
			active |= 1 << i;
	}
	igt_assert(still_busy(fd, handle[BUSY]));
	__gem_busy(fd, handle[TEST], &read, &write);
	igt_assert_eq(read, active);
	igt_assert_eq(write, ring); /* from the earlier write */

	/* Check that our long batch was long enough */
	igt_assert(still_busy(fd, handle[BUSY]));

	/* And make sure it becomes idle again */
	gem_sync(fd, handle[TEST]);
	__gem_busy(fd, handle[TEST], &read, &write);
	igt_assert_eq(read, 0);
	igt_assert_eq(write, 0);

	for (i = TEST; i <= BATCH; i++)
		gem_close(fd, handle[i]);
}

static bool has_semaphores(int fd)
{
	struct drm_i915_getparam gp;
	int val = -1;

	memset(&gp, 0, sizeof(gp));
	gp.param = I915_PARAM_HAS_SEMAPHORES;
	gp.value = &val;

	drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
	errno = 0;

	return val > 0;
}

igt_main
{
	int fd = -1;

	igt_skip_on_simulation();

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);
		igt_require(has_semaphores(fd));
	}

	igt_subtest("render")
		test_ring(fd, I915_EXEC_RENDER, 0);
	igt_subtest("bsd")
		test_ring(fd, I915_EXEC_BSD, 0);
	igt_subtest("bsd1")
		test_ring(fd, I915_EXEC_BSD, 1<<13 /*I915_EXEC_BSD_RING1*/);
	igt_subtest("bsd2")
		test_ring(fd, I915_EXEC_BSD, 2<<13 /*I915_EXEC_BSD_RING2*/);
	igt_subtest("blt")
		test_ring(fd, I915_EXEC_BLT, 0);
	igt_subtest("vebox")
		test_ring(fd, I915_EXEC_VEBOX, 0);

	igt_fixture
		close(fd);
}
