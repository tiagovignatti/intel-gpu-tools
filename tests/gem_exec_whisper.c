/*
 * Copyright © 2009 Intel Corporation
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

/** @file gem_exec_whisper.c
 *
 * Pass around a value to write into a scratch buffer between lots of batches
 */

#include "igt.h"
#include "igt_gt.h"

#define LOCAL_I915_EXEC_NO_RELOC (1<<11)
#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

#define LOCAL_I915_EXEC_BSD_SHIFT      (13)
#define LOCAL_I915_EXEC_BSD_MASK       (3 << LOCAL_I915_EXEC_BSD_SHIFT)

#define ENGINE_MASK  (I915_EXEC_RING_MASK | LOCAL_I915_EXEC_BSD_MASK)

static void write_seqno(unsigned offset)
{
	uint32_t seqno = UINT32_MAX - offset;
	FILE *file;

	file = igt_debugfs_fopen("i915_next_seqno", "w");
	igt_assert(file);

	igt_assert(fprintf(file, "0x%x", seqno) > 0);
	fclose(file);

	igt_debug("next seqno set to: 0x%x\n", seqno);
}

static void check_bo(int fd, uint32_t handle)
{
	uint32_t *map;
	int i;

	igt_debug("Verifying result\n");
	map = gem_mmap__cpu(fd, handle, 0, 4096, PROT_READ);
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_CPU, 0);
	for (i = 0; i < 1024; i++)
		igt_assert_eq(map[i], i);
	munmap(map, 4096);
}

static int __gem_context_create(int fd, uint32_t *ctx_id)
{
	struct drm_i915_gem_context_create arg;
	int ret = 0;

	memset(&arg, 0, sizeof(arg));
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, &arg))
		ret = -errno;

	*ctx_id = arg.ctx_id;
	return ret;
}

static bool ignore_engine(int gen, unsigned engine)
{
	return gen == 6 && (engine & ~(3<<13)) == I915_EXEC_BSD;
}

#define CONTEXTS 0x1

static void whisper(int fd, unsigned flags)
{
	const int gen = intel_gen(intel_get_drm_devid(fd));
	struct drm_i915_gem_exec_object2 batches[1024];
	struct drm_i915_gem_relocation_entry inter[1024];
	struct drm_i915_gem_relocation_entry reloc;
	uint32_t contexts[64];
	uint32_t scratch;
	unsigned engines[16];
	unsigned nengine;
	unsigned engine;
	uint32_t batch[16];
	int i, n, pass, loc;

	nengine = 0;
	for_each_engine(fd, engine)
		if (!ignore_engine(gen, engine)) engines[nengine++] = engine;
	igt_require(nengine);

	scratch = gem_create(fd, 4096);

	memset(&reloc, 0, sizeof(reloc));
	reloc.offset = sizeof(uint32_t);
	reloc.read_domains = I915_GEM_DOMAIN_INSTRUCTION;
	reloc.write_domain = I915_GEM_DOMAIN_INSTRUCTION;

	i = 0;
	batch[i] = MI_STORE_DWORD_IMM | (gen < 6 ? 1 << 22 : 0);
	if (gen >= 8) {
		batch[++i] = 0;
		batch[++i] = 0;
	} else if (gen >= 4) {
		batch[++i] = 0;
		batch[++i] = 0;
		reloc.offset += sizeof(uint32_t);
	} else {
		batch[i]--;
		batch[++i] = 0;
	}
	batch[loc = ++i] = 0xc0ffee;
	batch[++i] = MI_BATCH_BUFFER_END;

	if (flags & CONTEXTS) {
		igt_require(__gem_context_create(fd, &contexts[0]) == 0);
		for (n = 1; n < 64; n++)
			contexts[n] = gem_context_create(fd);
	}

	memset(batches, 0, sizeof(batches));
	for (n = 0; n < 1024; n++) {
		batches[n].handle = gem_create(fd, 4096);
		inter[n] = reloc;
		inter[n].presumed_offset = ~0;
		inter[n].delta = sizeof(uint32_t) * loc;
		batches[n].relocs_ptr = (uintptr_t)&inter[n];
		batches[n].relocation_count = 1;
		gem_write(fd, batches[n].handle, 0, batch, sizeof(batch));
	}

	for (pass = 0; pass < 1024; pass++) {
		struct drm_i915_gem_exec_object2 tmp[2];
		struct drm_i915_gem_execbuffer2 execbuf;

		write_seqno(pass);

		memset(&execbuf, 0, sizeof(execbuf));
		execbuf.buffers_ptr = (uintptr_t)tmp;
		execbuf.buffer_count = 2;
		execbuf.flags = LOCAL_I915_EXEC_HANDLE_LUT;
		if (gen < 6)
			execbuf.flags |= I915_EXEC_SECURE;

		memset(tmp, 0, sizeof(tmp));
		tmp[0].handle = scratch;
		reloc.presumed_offset = ~0;
		reloc.delta = 4*pass;
		batch[loc] = ~pass;
		tmp[1].handle = gem_create(fd, 4096);
		gem_write(fd, tmp[1].handle, 0, batch, sizeof(batch));
		tmp[1].relocs_ptr = (uintptr_t)&reloc;
		tmp[1].relocation_count = 1;
		execbuf.flags &= ~ENGINE_MASK;
		igt_require(__gem_execbuf(fd, &execbuf) == 0);

		gem_write(fd, batches[1023].handle, 4*loc, &pass, sizeof(pass));
		for (n = 1024; --n >= 1; ) {
			execbuf.buffers_ptr = (uintptr_t)&batches[n-1];
			batches[n-1].relocation_count = 0;

			execbuf.flags &= ~ENGINE_MASK;
			execbuf.flags |= engines[rand() % nengine];
			if (flags & CONTEXTS)
				execbuf.rsvd1 = contexts[rand() % 64];
			gem_execbuf(fd, &execbuf);

			batches[n-1].relocation_count = 1;
		}
		execbuf.flags &= ~ENGINE_MASK;
		execbuf.rsvd1 = 0;
		execbuf.buffers_ptr = (uintptr_t)&tmp;

		tmp[0] = tmp[1];
		tmp[0].relocation_count = 0;
		tmp[1] = batches[0];
		gem_execbuf(fd, &execbuf);
		batches[0] = tmp[1];

		tmp[1] = tmp[0];
		tmp[0].handle = scratch;
		reloc.presumed_offset = ~0;
		reloc.delta = 4*pass;
		tmp[1].relocs_ptr = (uintptr_t)&reloc;
		tmp[1].relocation_count = 1;
		gem_execbuf(fd, &execbuf);
		gem_close(fd, tmp[1].handle);
	}

	check_bo(fd, scratch);
	gem_close(fd, scratch);
	for (n = 0; n < 1024; n++)
		gem_create(fd, batches[n].handle);
}

igt_main
{
	int fd;

	igt_fixture
		fd = drm_open_driver_master(DRIVER_INTEL);

	igt_subtest("basic")
		whisper(fd, 0);

	igt_subtest("contexts")
		whisper(fd, CONTEXTS);

	igt_fixture
		close(fd);
}
