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
 *    Vinay Belgaumkar <vinay.belgaumkar@intel.com>
 *    Thomas Daniel <thomas.daniel@intel.com>
 *
 */

#include "igt.h"

#define EXEC_OBJECT_PINNED	(1<<4)
#define EXEC_OBJECT_SUPPORTS_48B_ADDRESS (1<<3)

/* gen8_canonical_addr
 * Used to convert any address into canonical form, i.e. [63:48] == [47].
 * Based on kernel's sign_extend64 implementation.
 * @address - a virtual address
*/
#define GEN8_HIGH_ADDRESS_BIT 47
static uint64_t gen8_canonical_addr(uint64_t address)
{
	__u8 shift = 63 - GEN8_HIGH_ADDRESS_BIT;
	return (__s64)(address << shift) >> shift;
}

static void test_invalid(int fd)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 object;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&object;
	execbuf.buffer_count = 1;

	memset(&object, 0, sizeof(object));
	object.handle = gem_create(fd, 2*4096);
	object.flags = EXEC_OBJECT_SUPPORTS_48B_ADDRESS | EXEC_OBJECT_PINNED;
	gem_write(fd, object.handle, 0, &bbe, sizeof(bbe));

	/* Check invalid alignment */
	object.offset = 4096;
	object.alignment = 64*1024;
	igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);
	object.alignment = 0;

	/* Check wraparound */
	object.offset = -4096ULL;
	igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);

	/* Check beyond bounds of aperture */
	object.offset = gem_aperture_size(fd) - 4096;
	object.offset = gen8_canonical_addr(object.offset);
	igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);

	/* Check gen8 canonical addressing */
	if (gem_aperture_size(fd) > 1ull<<GEN8_HIGH_ADDRESS_BIT) {
		object.offset = 1ull << GEN8_HIGH_ADDRESS_BIT;
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);

		object.offset = gen8_canonical_addr(object.offset);
		igt_assert_eq(__gem_execbuf(fd, &execbuf), 0);
	}

	/* Check extended range */
	if (gem_aperture_size(fd) > 1ull<<32) {
		object.flags = EXEC_OBJECT_PINNED;
		object.offset = 1ull<<32;
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);

		object.offset = gen8_canonical_addr(object.offset);
		object.flags |= EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
		igt_assert_eq(__gem_execbuf(fd, &execbuf), 0);
	}
}

static void test_softpin(int fd)
{
	const uint32_t size = 1024 * 1024;
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 object;
	uint64_t offset, end;
	uint32_t last_handle;
	int loop;

	last_handle = gem_create(fd, size);

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&object;
	execbuf.buffer_count = 1;
	for (loop = 0; loop < 1024; loop++) {
		memset(&object, 0, sizeof(object));
		object.handle = gem_create(fd, 2*size);
		gem_write(fd, object.handle, 0, &bbe, sizeof(bbe));

		/* Find a hole */
		gem_execbuf(fd, &execbuf);
		gem_close(fd, object.handle);
		gem_close(fd, last_handle);

		igt_debug("Made a 2 MiB hole: %08llx\n",
			  object.offset);

		object.handle = gem_create(fd, size);
		gem_write(fd, object.handle, 0, &bbe, sizeof(bbe));
		object.flags |= EXEC_OBJECT_PINNED;

		end = object.offset + size;
		for (offset = object.offset; offset <= end; offset += 4096) {
			object.offset = offset;
			gem_execbuf(fd, &execbuf);
			igt_assert_eq_u64(object.offset, offset);
		}

		last_handle = object.handle;
	}
}

static void test_overlap(int fd)
{
	const uint32_t size = 1024 * 1024;
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 object[2];
	uint64_t offset;
	uint32_t handle;

	handle = gem_create(fd, 3*size);
	gem_write(fd, handle, 0, &bbe, sizeof(bbe));

	memset(object, 0, sizeof(object));
	object[0].handle = handle;

	/* Find a hole */
	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)object;
	execbuf.buffer_count = 1;
	gem_execbuf(fd, &execbuf);

	igt_debug("Made a 3x1 MiB hole: %08llx\n",
		  object[0].offset);

	object[0].handle = gem_create(fd, size);
	object[0].offset += size;
	object[0].flags |= EXEC_OBJECT_PINNED;
	object[1].handle = gem_create(fd, size);
	object[1].flags |= EXEC_OBJECT_PINNED;
	gem_write(fd, object[1].handle, 0, &bbe, sizeof(bbe));
	execbuf.buffer_count = 2;

	/* Check that we fit into our hole */
	object[1].offset = object[0].offset - size;
	gem_execbuf(fd, &execbuf);
	igt_assert_eq_u64(object[1].offset + size, object[0].offset);

	object[1].offset = object[0].offset + size;
	gem_execbuf(fd, &execbuf);
	igt_assert_eq_u64(object[1].offset - size, object[0].offset);

	/* Try all possible page-aligned overlaps */
	for (offset = object[0].offset - size + 4096;
	     offset < object[0].offset + size;
	     offset += 4096) {
		object[1].offset = offset;
		igt_debug("[0]=[%08llx - %08llx] [1]=[%08llx - %08llx]\n",
			  (long long)object[0].offset,
			  (long long)object[0].offset + size,
			  (long long)object[1].offset,
			  (long long)object[1].offset + size);
		igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);
		igt_assert_eq_u64(object[1].offset, offset);
	}

	gem_close(fd, object[1].handle);
	gem_close(fd, object[0].handle);
	gem_close(fd, handle);
}

static uint64_t busy_batch(int fd)
{
	const int gen = intel_gen(intel_get_drm_devid(fd));
	const int has_64bit_reloc = gen >= 8;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 object[2];
	uint32_t *map;
	int factor = 10;
	int i = 0;

	memset(object, 0, sizeof(object));
	object[0].handle = gem_create(fd, 1024*1024);
	object[1].handle = gem_create(fd, 4096);
	map = gem_mmap__cpu(fd, object[1].handle, 0, 4096, PROT_WRITE);
	gem_set_domain(fd, object[1].handle,
		       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);

	*map = MI_BATCH_BUFFER_END;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)object;
	execbuf.buffer_count = 2;
	if (gen >= 6)
		execbuf.flags = I915_EXEC_BLT;
	gem_execbuf(fd, &execbuf);

	igt_debug("Active offsets = [%08llx, %08llx]\n",
		  object[0].offset, object[1].offset);

#define COPY_BLT_CMD		(2<<29|0x53<<22|0x6)
#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)
	gem_set_domain(fd, object[1].handle,
		       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
	while (factor--) {
		/* XY_SRC_COPY */
		map[i++] = COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB;
		if (has_64bit_reloc)
			map[i-1] += 2;
		map[i++] = 0xcc << 16 | 1 << 25 | 1 << 24 | (4*1024);
		map[i++] = 0;
		map[i++] = 256 << 16 | 1024;
		map[i++] = object[0].offset;
		if (has_64bit_reloc)
			map[i++] = object[0].offset >> 32;
		map[i++] = 0;
		map[i++] = 4096;
		map[i++] = object[0].offset;
		if (has_64bit_reloc)
			map[i++] = object[0].offset >> 32;
	}
	map[i++] = MI_BATCH_BUFFER_END;
	munmap(map, 4096);

	object[0].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_WRITE;
	object[1].flags = EXEC_OBJECT_PINNED;
	gem_execbuf(fd, &execbuf);
	gem_close(fd, object[0].handle);
	gem_close(fd, object[1].handle);

	return object[1].offset;
}

static void test_evict_active(int fd)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 object;
	uint64_t expected;

	memset(&object, 0, sizeof(object));
	object.handle = gem_create(fd, 4096);
	gem_write(fd, object.handle, 0, &bbe, sizeof(bbe));

	expected = busy_batch(fd);
	object.offset = expected;
	object.flags = EXEC_OBJECT_PINNED;

	/* Replace the active batch with ourselves, forcing an eviction */
	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&object;
	execbuf.buffer_count = 1;

	gem_execbuf(fd, &execbuf);
	gem_close(fd, object.handle);

	igt_assert_eq_u64(object.offset, expected);
}

static void test_evict_snoop(int fd)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 object[2];
	uint64_t hole;

	igt_require(!gem_has_llc(fd));
	igt_require(!gem_uses_ppgtt(fd));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)object;
	execbuf.buffer_count = 1;

	/* Find a hole */
	memset(object, 0, sizeof(object));
	object[0].handle = gem_create(fd, 3*4096);
	gem_write(fd, object[0].handle, 0, &bbe, sizeof(bbe));
	gem_execbuf(fd, &execbuf);
	gem_close(fd, object[0].handle);
	hole = object[0].offset;

	/* Create a snoop + uncached pair */
	object[0].handle = gem_create(fd, 4096);
	object[0].flags = EXEC_OBJECT_PINNED;
	gem_set_caching(fd, object[0].handle, 1);
	object[1].handle = gem_create(fd, 4096);
	object[1].flags = EXEC_OBJECT_PINNED;
	gem_write(fd, object[1].handle, 4096-sizeof(bbe), &bbe, sizeof(bbe));
	execbuf.buffer_count = 2;

	/* snoop abutting before uncached -> error */
	object[0].offset = hole;
	object[1].offset = hole + 4096;
	igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);

	/* snoop abutting after uncached -> error */
	object[0].offset = hole + 4096;
	object[1].offset = hole;
	igt_assert_eq(__gem_execbuf(fd, &execbuf), -EINVAL);

	/* with gap -> okay */
	object[0].offset = hole + 2*4096;
	object[1].offset = hole;
	igt_assert_eq(__gem_execbuf(fd, &execbuf), 0);

	/* And we should force the snoop away (or the GPU may hang) */
	object[0].flags = 0;
	object[1].offset = hole + 4096;
	igt_assert_eq(__gem_execbuf(fd, &execbuf), 0);
	igt_assert(object[0].offset != hole);
	igt_assert(object[0].offset != hole + 2*4096);

	gem_close(fd, object[0].handle);
	gem_close(fd, object[1].handle);
}

static void test_evict_hang(int fd)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 object;
	uint64_t expected;
	igt_hang_ring_t hang;

	memset(&object, 0, sizeof(object));
	object.handle = gem_create(fd, 4096);
	gem_write(fd, object.handle, 0, &bbe, sizeof(bbe));

	hang = igt_hang_ctx(fd, 0, 0, 0, (uint64_t *)&expected);
	object.offset = expected;
	object.flags = EXEC_OBJECT_PINNED;

	/* Replace the hanging batch with ourselves, forcing an eviction */
	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&object;
	execbuf.buffer_count = 1;

	gem_execbuf(fd, &execbuf);
	gem_close(fd, object.handle);

	igt_assert_eq_u64(object.offset, expected);

	igt_post_hang_ring(fd, hang);
}

static void xchg_offset(void *array, unsigned i, unsigned j)
{
	struct drm_i915_gem_exec_object2 *object = array;
	uint64_t tmp = object[i].offset;
	object[i].offset = object[j].offset;
	object[j].offset = tmp;
}

static void test_noreloc(int fd)
{
	const int gen = intel_gen(intel_get_drm_devid(fd));
	const uint32_t size = 4096;
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 object[257];
	uint64_t offset;
	uint32_t handle;
	uint32_t *batch, *b;
	int i, loop;

	handle = gem_create(fd, (ARRAY_SIZE(object)+1)*size);
	gem_write(fd, handle, 0, &bbe, sizeof(bbe));

	memset(object, 0, sizeof(object));
	object[0].handle = handle;

	/* Find a hole */
	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)object;
	execbuf.buffer_count = 1;
	if (gen < 6)
		execbuf.flags |= I915_EXEC_SECURE;
	gem_execbuf(fd, &execbuf);
	gem_close(fd, object[0].handle);

	igt_debug("Made a %dx%d KiB hole: %08llx\n",
		  (int)ARRAY_SIZE(object), size/1024, object[0].offset);

	offset = object[0].offset;
	for (i = 0; i < ARRAY_SIZE(object) - 1; i++) {
		object[i].handle = gem_create(fd, size);
		object[i].offset = offset + i*size;
		object[i].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_WRITE;
	}
	object[i].handle = gem_create(fd, 2*size);
	object[i].offset = offset + i*size;
	object[i].flags = EXEC_OBJECT_PINNED;

	b = batch = gem_mmap__cpu(fd, object[i].handle, 0, 2*size, PROT_WRITE);
	gem_set_domain(fd, object[i].handle,
		       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
	for (i = 0; i < ARRAY_SIZE(object) - 1; i++) {
		*b++ = MI_STORE_DWORD_IMM | (gen < 6 ? 1 << 22 : 0);
		if (gen >= 8) {
			*b++ = object[i].offset;
			*b++ = object[i].offset >> 32;
		} else if (gen >= 4) {
			*b++ = 0;
			*b++ = object[i].offset;
		} else {
			b[-1]--;
			*b++ = object[i].offset;
		}
		*b++ = i;
	}
	*b++ = MI_BATCH_BUFFER_END;
	igt_assert(b - batch <= 2*size/sizeof(uint32_t));
	munmap(batch, size);

	execbuf.buffer_count = ARRAY_SIZE(object);
	for (loop = 0; loop < 1024; loop++) {
		igt_permute_array(object, ARRAY_SIZE(object)-1, xchg_offset);
		gem_execbuf(fd, &execbuf);

		for (i = 0; i < ARRAY_SIZE(object) - 1; i++) {
			uint32_t val;

			gem_read(fd, object[i].handle, 0, &val, sizeof(val));
			igt_assert_eq(val, (object[i].offset - offset)/size);
		}
	}
	for (i = 0; i < ARRAY_SIZE(object); i++)
		gem_close(fd, object[i].handle);
}

igt_main
{
	int fd = -1;

	igt_skip_on_simulation();

	igt_fixture {
		fd = drm_open_driver_master(DRIVER_INTEL);
		igt_require(gem_has_softpin(fd));
	}

	igt_subtest("invalid")
		test_invalid(fd);
	igt_subtest("softpin")
		test_softpin(fd);
	igt_subtest("overlap")
		test_overlap(fd);
	igt_subtest("noreloc")
		test_noreloc(fd);
	igt_subtest("evict-active")
		test_evict_active(fd);
	igt_subtest("evict-snoop")
		test_evict_snoop(fd);
	igt_subtest("evict-hang")
		test_evict_hang(fd);

	igt_fixture
		close(fd);
}
