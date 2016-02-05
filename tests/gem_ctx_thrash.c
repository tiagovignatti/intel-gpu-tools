/*
 * Copyright © 2014 Intel Corporation
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

#include "igt.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/resource.h>

IGT_TEST_DESCRIPTION("Fill the Gobal GTT with context objects and VMs\n");

#define NUM_THREADS (2*sysconf(_SC_NPROCESSORS_ONLN))

static void xchg_int(void *array, unsigned i, unsigned j)
{
	int *A = array;
	igt_swap(A[i], A[j]);
}

static unsigned get_num_contexts(int fd)
{
	uint64_t ggtt_size;
	unsigned size;
	unsigned count;

	/* Compute the number of contexts we can allocate to fill the GGTT */
	ggtt_size = gem_global_aperture_size(fd);
	size = 64 << 10; /* Most gen require at least 64k for ctx */

	count = 3 * (ggtt_size / size) / 2;
	igt_info("Creating %lld contexts (assuming of size %lld)\n",
		 (long long)count, (long long)size);

	intel_require_memory(count, size, CHECK_RAM | CHECK_SWAP);
	return count;
}

static int has_engine(int fd, const struct intel_execution_engine *e, uint32_t ctx)
{
	uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec;
	int ret;

	memset(&exec, 0, sizeof(exec));
	exec.handle = gem_create(fd, 4096);
	gem_write(fd, exec.handle, 0, &bbe, sizeof(bbe));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&exec;
	execbuf.buffer_count = 1;
	execbuf.flags = e->exec_id | e->flags;
	execbuf.rsvd1 = ctx;
	ret = __gem_execbuf(fd, &execbuf);
	gem_close(fd, exec.handle);

	return ret;
}

static void single(const char *name, bool all_engines)
{
	struct drm_i915_gem_exec_object2 *obj;
	struct drm_i915_gem_relocation_entry *reloc;
	const struct intel_execution_engine *e;
	unsigned engines[16];
	uint64_t size;
	uint32_t *ctx, *map, scratch;
	unsigned num_ctx;
	int fd, gen, num_engines;
#define MAX_LOOP 16

	fd = drm_open_driver_master(DRIVER_INTEL);
	gen = intel_gen(intel_get_drm_devid(fd));
	num_ctx = get_num_contexts(fd);

	num_engines = 0;
	if (all_engines) {
		for (e = intel_execution_engines; e->name; e++) {
			if (e->exec_id == 0)
				continue;

			if (has_engine(fd, e, 0))
				continue;

			if (e->exec_id == I915_EXEC_BSD) {
				int is_bsd2 = e->flags != 0;
				if (gem_has_bsd2(fd) != is_bsd2)
					continue;
			}

			igt_require(has_engine(fd, e, 1) == -ENOENT);

			engines[num_engines++] = e->exec_id | e->flags;
			if (num_engines == ARRAY_SIZE(engines))
				break;
		}
	} else
		engines[num_engines++] = 0;

	size = ALIGN(num_ctx * sizeof(uint32_t), 4096);
	scratch = gem_create(fd, ALIGN(num_ctx * sizeof(uint32_t), 4096));
	gem_set_caching(fd, scratch, I915_CACHING_CACHED);
	obj = calloc(num_ctx, 2 * sizeof(*obj));
	reloc = calloc(num_ctx, sizeof(*reloc));

	ctx = malloc(num_ctx * sizeof(uint32_t));
	igt_assert(ctx);
	for (unsigned n = 0; n < num_ctx; n++) {
		ctx[n] = gem_context_create(fd);
		obj[2*n + 0].handle = scratch;

		reloc[n].target_handle = scratch;
		reloc[n].presumed_offset = 0;
		reloc[n].offset = sizeof(uint32_t);
		reloc[n].delta = n * sizeof(uint32_t);
		reloc[n].read_domains = I915_GEM_DOMAIN_INSTRUCTION;
		reloc[n].write_domain = 0; /* lies! */
		if (gen >= 4 && gen < 8)
			reloc[n].offset += sizeof(uint32_t);

		obj[2*n + 1].relocs_ptr = (uintptr_t)&reloc[n];
		obj[2*n + 1].relocation_count = 1;
	}

	map = gem_mmap__cpu(fd, scratch, 0, size, PROT_WRITE);
	for (unsigned loop = 1; loop <= MAX_LOOP; loop <<= 1) {
		unsigned count = loop * num_ctx;
		uint32_t *all;

		all = malloc(count * sizeof(uint32_t));
		for (unsigned n = 0; n < count; n++)
			all[n] = ctx[n % num_ctx];
		igt_permute_array(all, count, xchg_int);
		for (unsigned n = 0; n < count; n++) {
			struct drm_i915_gem_execbuffer2 execbuf;
			unsigned r = n % num_ctx;
			uint64_t offset = reloc[r].presumed_offset + reloc[r].delta;
			uint32_t handle = gem_create(fd, 4096);
			uint32_t buf[16];
			int i;

			buf[i = 0] = MI_STORE_DWORD_IMM;
			if (gen >= 8) {
				buf[++i] = offset;
				buf[++i] = offset >> 32;
			} else if (gen >= 4) {
				if (gen < 6)
					buf[i] |= 1 << 22;
				buf[++i] = 0;
				buf[++i] = offset;
			} else {
				buf[i]--;
				buf[++i] = offset;
			}
			buf[++i] = all[n];
			buf[++i] = MI_BATCH_BUFFER_END;
			gem_write(fd, handle, 0, buf, sizeof(buf));
			obj[2*r + 1].handle = handle;

			memset(&execbuf, 0, sizeof(execbuf));
			execbuf.buffers_ptr = (uintptr_t)&obj[2*r];
			execbuf.buffer_count = 2;
			execbuf.flags = engines[n % num_engines];
			execbuf.rsvd1 = all[n];
			gem_execbuf(fd, &execbuf);
			gem_close(fd, handle);
		}

		/* Note we lied about the write-domain when writing from the
		 * GPU (in order to avoid inter-ring synchronisation), so now
		 * we have to force the synchronisation here.
		 */
		gem_set_domain(fd, scratch,
			       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
		for (unsigned n = count - num_ctx; n < count; n++)
			igt_assert_eq(map[n % num_ctx], all[n]);
		free(all);
		igt_progress(name, loop, MAX_LOOP);
	}
	munmap(map, size);

	free(ctx);
	close(fd);
}

static void processes(void)
{
	const struct intel_execution_engine *e;
	unsigned engines[16];
	int num_engines;
	struct rlimit rlim;
	unsigned num_ctx;
	uint32_t name;
	int fd, *fds;

	fd = drm_open_driver(DRIVER_INTEL);
	num_ctx = get_num_contexts(fd);

	num_engines = 0;
	for (e = intel_execution_engines; e->name; e++) {
		if (e->exec_id == 0)
			continue;

		if (has_engine(fd, e, 0))
			continue;

		if (e->exec_id == I915_EXEC_BSD) {
			int is_bsd2 = e->flags != 0;
			if (gem_has_bsd2(fd) != is_bsd2)
				continue;
		}

		engines[num_engines++] = e->exec_id | e->flags;
		if (num_engines == ARRAY_SIZE(engines))
			break;
	}

	/* tweak rlimits to allow us to create this many files */
	igt_assert(getrlimit(RLIMIT_NOFILE, &rlim) == 0);
	if (rlim.rlim_cur < ALIGN(num_ctx + 1024, 1024)) {
		rlim.rlim_cur = ALIGN(num_ctx + 1024, 1024);
		if (rlim.rlim_cur > rlim.rlim_max)
			rlim.rlim_max = rlim.rlim_cur;
		igt_assert(setrlimit(RLIMIT_NOFILE, &rlim) == 0);
	}

	fds = malloc(num_ctx * sizeof(int));
	igt_assert(fds);
	for (unsigned n = 0; n < num_ctx; n++) {
		fds[n] = drm_open_driver(DRIVER_INTEL);
		if (fds[n] == -1) {
			int err = errno;
			for (unsigned i = n; i--; )
				close(fds[i]);
			free(fds);
			errno = err;
			igt_assert_f(0, "failed to create context %lld/%lld\n", (long long)n, (long long)num_ctx);
		}
	}

	if (1) {
		uint32_t bbe = MI_BATCH_BUFFER_END;
		name = gem_create(fd, 4096);
		gem_write(fd, name, 0, &bbe, sizeof(bbe));
		name = gem_flink(fd, name);
	}

	igt_fork(child, NUM_THREADS) {
		struct drm_i915_gem_execbuffer2 execbuf;
		struct drm_i915_gem_exec_object2 obj;

		memset(&obj, 0, sizeof(obj));
		memset(&execbuf, 0, sizeof(execbuf));
		execbuf.buffers_ptr = (uintptr_t)&obj;
		execbuf.buffer_count = 1;

		igt_permute_array(fds, num_ctx, xchg_int);
		for (unsigned n = 0; n < num_ctx; n++) {
			obj.handle = gem_open(fds[n], name);
			execbuf.flags = engines[n % num_engines];
			gem_execbuf(fds[n], &execbuf);
			gem_close(fds[n], obj.handle);
		}
	}
	igt_waitchildren();

	for (unsigned n = 0; n < num_ctx; n++)
		close(fds[n]);
	free(fds);
	close(fd);
}

struct thread {
	int fd;
	uint32_t *all_ctx;
	unsigned num_ctx;
	uint32_t batch;
};

static void *thread(void *data)
{
	struct thread *t = data;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	uint32_t *ctx;

	memset(&obj, 0, sizeof(obj));
	obj.handle = t->batch;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&obj;
	execbuf.buffer_count = 1;

	ctx = malloc(t->num_ctx * sizeof(uint32_t));
	igt_assert(ctx);
	memcpy(ctx, t->all_ctx, t->num_ctx * sizeof(uint32_t));
	igt_permute_array(ctx, t->num_ctx, xchg_int);

	for (unsigned n = 0; n < t->num_ctx; n++) {
		execbuf.rsvd1 = ctx[n];
		gem_execbuf(t->fd, &execbuf);
	}

	free(ctx);

	return NULL;
}

static void threads(void)
{
	uint32_t bbe = MI_BATCH_BUFFER_END;
	pthread_t threads[NUM_THREADS];
	struct thread data;

	data.fd = drm_open_driver_render(DRIVER_INTEL);
	data.num_ctx = get_num_contexts(data.fd);
	data.all_ctx = malloc(data.num_ctx * sizeof(uint32_t));
	igt_assert(data.all_ctx);
	for (unsigned n = 0; n < data.num_ctx; n++)
		data.all_ctx[n] = gem_context_create(data.fd);
	data.batch = gem_create(data.fd, 4096);
	gem_write(data.fd, data.batch, 0, &bbe, sizeof(bbe));

	for (int n = 0; n < NUM_THREADS; n++)
		pthread_create(&threads[n], NULL, thread, &data);

	for (int n = 0; n < NUM_THREADS; n++)
		pthread_join(threads[n], NULL);

	close(data.fd);
}

igt_main
{
	igt_skip_on_simulation();

	igt_subtest("single")
		single("single", false);
	igt_subtest("engines")
		single("engines", true);

	igt_subtest("processes")
		processes();

	igt_subtest("threads")
		threads();
}
