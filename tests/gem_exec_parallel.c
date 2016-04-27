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

/** @file gem_exec_parallel.c
 *
 * Exercise using many, many writers into a buffer.
 */

#include <pthread.h>

#include "igt.h"
#include "igt_gt.h"

#define LOCAL_I915_EXEC_NO_RELOC (1<<11)
#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

#define LOCAL_I915_EXEC_BSD_SHIFT      (13)
#define LOCAL_I915_EXEC_BSD_MASK       (3 << LOCAL_I915_EXEC_BSD_SHIFT)

#define ENGINE_MASK  (I915_EXEC_RING_MASK | LOCAL_I915_EXEC_BSD_MASK)

#define VERIFY 0

static void check_bo(int fd, uint32_t handle, int pass)
{
	uint32_t *map;
	int i;

	igt_debug("Verifying result (pass=%d, handle=%d)\n", pass, handle);
	map = gem_mmap__cpu(fd, handle, 0, 4096, PROT_READ);
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_CPU, 0);
	for (i = 0; i < 1024; i++)
		igt_assert_eq(map[i], i);
	munmap(map, 4096);
}

static uint32_t __gem_context_create(int fd)
{
	struct drm_i915_gem_context_create arg;

	memset(&arg, 0, sizeof(arg));
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, &arg) == 0)
		gem_context_destroy(fd, arg.ctx_id);

	return arg.ctx_id;
}

static void gem_require_context(int fd)
{
	igt_require(__gem_context_create(fd));
}

static bool can_mi_store_dword(int gen, unsigned engine)
{
	return !(gen == 6 && (engine & ~(3<<13)) == I915_EXEC_BSD);
}

static bool ignore_engine(int gen, unsigned engine)
{
	if (engine == 0)
		return true;

	if (!can_mi_store_dword(gen, engine))
		return true;

	return false;
}

#define CONTEXTS 0x1
#define FDS 0x2

struct thread {
	pthread_t thread;
	pthread_mutex_t *mutex;
	pthread_cond_t *cond;
	unsigned flags;
	uint32_t *scratch;
	unsigned id;
	unsigned engine;
	int fd, gen, *go;
};

static void *thread(void *data)
{
	struct thread *t = data;
	struct drm_i915_gem_exec_object2 obj[2];
	struct drm_i915_gem_relocation_entry reloc;
	struct drm_i915_gem_execbuffer2 execbuf;
	uint32_t batch[16];
	int fd, i;

	pthread_mutex_lock(t->mutex);
	while (*t->go == 0)
		pthread_cond_wait(t->cond, t->mutex);
	pthread_mutex_unlock(t->mutex);

	if (t->flags & FDS)
		fd = drm_open_driver(DRIVER_INTEL);
	else
		fd = t->fd;

	i = 0;
	batch[i] = MI_STORE_DWORD_IMM | (t->gen < 6 ? 1 << 22 : 0);
	if (t->gen >= 8) {
		batch[++i] = 4*t->id;
		batch[++i] = 0;
	} else if (t->gen >= 4) {
		batch[++i] = 0;
		batch[++i] = 4*t->id;
	} else {
		batch[i]--;
		batch[++i] = 4*t->id;
	}
	batch[++i] = t->id;
	batch[++i] = MI_BATCH_BUFFER_END;

	memset(obj, 0, sizeof(obj));
	obj[0].flags = EXEC_OBJECT_WRITE;

	memset(&reloc, 0, sizeof(reloc));
	reloc.offset = sizeof(uint32_t);
	if (t->gen < 8 && t->gen >= 4)
		reloc.offset += sizeof(uint32_t);
	reloc.read_domains = I915_GEM_DOMAIN_INSTRUCTION;
	reloc.write_domain = I915_GEM_DOMAIN_INSTRUCTION;
	reloc.delta = 4*t->id;
	obj[1].handle = gem_create(fd, 4096);
	obj[1].relocs_ptr = (uintptr_t)&reloc;
	obj[1].relocation_count = 1;
	gem_write(fd, obj[1].handle, 0, batch, sizeof(batch));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)obj;
	execbuf.buffer_count = 2;
	execbuf.flags = t->engine;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	if (t->gen < 6)
		execbuf.flags |= I915_EXEC_SECURE;
	if (t->flags & CONTEXTS)
		execbuf.rsvd1 = gem_context_create(fd);

	for (i = 0; i < 16; i++) {
		obj[0].handle = t->scratch[i];
		if (t->flags & FDS)
			obj[0].handle = gem_open(fd, obj[0].handle);

		gem_execbuf(fd, &execbuf);

		if (t->flags & FDS)
			gem_close(fd, obj[0].handle);
	}

	if (t->flags & CONTEXTS)
		gem_context_destroy(fd, execbuf.rsvd1);
	gem_close(fd, obj[1].handle);
	if (t->flags & FDS)
		close(fd);

	return NULL;
}

static void all(int fd, unsigned engine, unsigned flags)
{
	const int gen = intel_gen(intel_get_drm_devid(fd));
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct thread *threads;
	unsigned engines[16];
	unsigned nengine;
	uint32_t scratch[16], handle[16];
	int go;
	int i;

	if (flags & CONTEXTS)
		gem_require_context(fd);

	nengine = 0;
	if (engine == -1) {
		for_each_engine(fd, engine) {
			if (!ignore_engine(gen, engine))
				engines[nengine++] = engine;
		}
	} else {
		igt_require(gem_has_ring(fd, engine));
		igt_require(can_mi_store_dword(gen, engine));
		engines[nengine++] = engine;
	}
	igt_require(nengine);

	for (i = 0; i < 16; i++) {
		scratch[i] = handle[i] = gem_create(fd, 4096);
		if (flags & FDS)
			scratch[i] = gem_flink(fd, handle[i]);
	}

	threads = calloc(1024, sizeof(struct thread));
	igt_assert(threads);

	pthread_mutex_init(&mutex, 0);
	pthread_cond_init(&cond, 0);
	go = 0;

	for (i = 0; i < 1024; i++) {
		threads[i].id = i;
		threads[i].fd = fd;
		threads[i].gen = gen;
		threads[i].engine = engines[i % nengine];
		threads[i].flags = flags;
		threads[i].scratch = scratch;
		threads[i].mutex = &mutex;
		threads[i].cond = &cond;
		threads[i].go = &go;

		pthread_create(&threads[i].thread, 0, thread, &threads[i]);
	}

	pthread_mutex_lock(&mutex);
	go = 1024;
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&mutex);

	for (i = 0; i < 1024; i++)
		pthread_join(threads[i].thread, NULL);

	for (i = 0; i < 16; i++) {
		check_bo(fd, handle[i], i);
		gem_close(fd, handle[i]);
	}

	free(threads);
}

igt_main
{
	const struct mode {
		const char *name;
		unsigned flags;
	} modes[] = {
		{ "", 0 },
		{ "contexts", CONTEXTS },
		{ "fds", FDS },
		{ NULL }
	};
	int fd;

	igt_fixture
		fd = drm_open_driver_master(DRIVER_INTEL);

	igt_fork_hang_detector(fd);

	for (const struct mode *m = modes; m->name; m++)
		igt_subtest_f("%s", *m->name ? m->name : "basic")
			all(fd, -1, m->flags);

	for (const struct intel_execution_engine *e = intel_execution_engines;
	     e->name; e++) {
		for (const struct mode *m = modes; m->name; m++)
			igt_subtest_f("%s%s%s",
				      e->name,
				      *m->name ? "-" : "",
				      m->name)
				all(fd, e->exec_id | e->flags, m->flags);
	}

	igt_stop_hang_detector();

	igt_fixture
		close(fd);
}
