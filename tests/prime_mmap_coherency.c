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
 *    Tiago Vignatti
 */

/** @file prime_mmap_coherency.c
 *
 * TODO: need to show the need for prime_sync_end().
 */

#include "igt.h"

IGT_TEST_DESCRIPTION("Test dma-buf mmap on !llc platforms mostly and provoke"
		" coherency bugs so we know for sure where we need the sync ioctls.");

#define ROUNDS 20

int fd;
int stale = 0;
static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
static int width = 1024, height = 1024;

/*
 * Exercises the need for read flush:
 *   1. create a BO and write '0's, in GTT domain.
 *   2. read BO using the dma-buf CPU mmap.
 *   3. write '1's, in GTT domain.
 *   4. read again through the mapped dma-buf.
 */
static void test_read_flush(bool expect_stale_cache)
{
	drm_intel_bo *bo_1;
	drm_intel_bo *bo_2;
	uint32_t *ptr_cpu;
	uint32_t *ptr_gtt;
	int dma_buf_fd, i;

	if (expect_stale_cache)
		igt_require(!gem_has_llc(fd));

	bo_1 = drm_intel_bo_alloc(bufmgr, "BO 1", width * height * 4, 4096);

	/* STEP #1: put the BO 1 in GTT domain. We use the blitter to copy and fill
	 * zeros to BO 1, so commands will be submitted and likely to place BO 1 in
	 * the GTT domain. */
	bo_2 = drm_intel_bo_alloc(bufmgr, "BO 2", width * height * 4, 4096);
	intel_copy_bo(batch, bo_1, bo_2, width * height);
	gem_sync(fd, bo_1->handle);
	drm_intel_bo_unreference(bo_2);

	/* STEP #2: read BO 1 using the dma-buf CPU mmap. This dirties the CPU caches. */
	dma_buf_fd = prime_handle_to_fd_for_mmap(fd, bo_1->handle);
	igt_skip_on(errno == EINVAL);

	ptr_cpu = mmap(NULL, width * height, PROT_READ | PROT_WRITE,
		       MAP_SHARED, dma_buf_fd, 0);
	igt_assert(ptr_cpu != MAP_FAILED);

	for (i = 0; i < (width * height) / 4; i++)
		igt_assert_eq(ptr_cpu[i], 0);

	/* STEP #3: write 0x11 into BO 1. */
	bo_2 = drm_intel_bo_alloc(bufmgr, "BO 2", width * height * 4, 4096);
	ptr_gtt = gem_mmap__gtt(fd, bo_2->handle, width * height, PROT_READ | PROT_WRITE);
	memset(ptr_gtt, 0x11, width * height);
	munmap(ptr_gtt, width * height);

	intel_copy_bo(batch, bo_1, bo_2, width * height);
	gem_sync(fd, bo_1->handle);
	drm_intel_bo_unreference(bo_2);

	/* STEP #4: read again using the CPU mmap. Doing #1 before #3 makes sure we
	 * don't do a full CPU cache flush in step #3 again. That makes sure all the
	 * stale cachelines from step #2 survive (mostly, a few will be evicted)
	 * until we try to read them again in step #4. This behavior could be fixed
	 * by flush CPU read right before accessing the CPU pointer */
	if (!expect_stale_cache)
		prime_sync_start(dma_buf_fd);

	for (i = 0; i < (width * height) / 4; i++)
		if (ptr_cpu[i] != 0x11111111) {
			igt_warn_on_f(!expect_stale_cache,
				    "Found 0x%08x at offset 0x%08x\n", ptr_cpu[i], i);
			stale++;
		}

	drm_intel_bo_unreference(bo_1);
	munmap(ptr_cpu, width * height);
}

/*
 * Exercises the need for write flush:
 *   1. create BO 1 and write '0's, in GTT domain.
 *   2. write '1's into BO 1 using the dma-buf CPU mmap.
 *   3. copy BO 1 to new BO 2, in GTT domain.
 *   4. read via dma-buf mmap BO 2.
 */
static void test_write_flush(bool expect_stale_cache)
{
	drm_intel_bo *bo_1;
	drm_intel_bo *bo_2;
	uint32_t *ptr_cpu;
	uint32_t *ptr2_cpu;
	int dma_buf_fd, dma_buf2_fd, i;

	if (expect_stale_cache)
		igt_require(!gem_has_llc(fd));

	bo_1 = drm_intel_bo_alloc(bufmgr, "BO 1", width * height * 4, 4096);

	/* STEP #1: Put the BO 1 in GTT domain. We use the blitter to copy and fill
	 * zeros to BO 1, so commands will be submitted and likely to place BO 1 in
	 * the GTT domain. */
	bo_2 = drm_intel_bo_alloc(bufmgr, "BO 2", width * height * 4, 4096);
	intel_copy_bo(batch, bo_1, bo_2, width * height);
	gem_sync(fd, bo_1->handle);
	drm_intel_bo_unreference(bo_2);

	/* STEP #2: Write '1's into BO 1 using the dma-buf CPU mmap. */
	dma_buf_fd = prime_handle_to_fd_for_mmap(fd, bo_1->handle);
	igt_skip_on(errno == EINVAL);

	ptr_cpu = mmap(NULL, width * height, PROT_READ | PROT_WRITE,
		       MAP_SHARED, dma_buf_fd, 0);
	igt_assert(ptr_cpu != MAP_FAILED);

	/* This is the main point of this test: !llc hw requires a cache write
	 * flush right here (explained in step #4). */
	if (!expect_stale_cache)
		prime_sync_start(dma_buf_fd);

	memset(ptr_cpu, 0x11, width * height);

	/* STEP #3: Copy BO 1 into BO 2, using blitter. */
	bo_2 = drm_intel_bo_alloc(bufmgr, "BO 2", width * height * 4, 4096);
	intel_copy_bo(batch, bo_2, bo_1, width * height);
	gem_sync(fd, bo_2->handle);

	/* STEP #4: compare BO 2 against written BO 1. In !llc hardware, there
	 * should be some cache lines that didn't get flushed out and are still 0,
	 * requiring cache flush before the write in step 2. */
	dma_buf2_fd = prime_handle_to_fd_for_mmap(fd, bo_2->handle);
	igt_skip_on(errno == EINVAL);

	ptr2_cpu = mmap(NULL, width * height, PROT_READ | PROT_WRITE,
		        MAP_SHARED, dma_buf2_fd, 0);
	igt_assert(ptr2_cpu != MAP_FAILED);

	for (i = 0; i < (width * height) / 4; i++)
		if (ptr2_cpu[i] != 0x11111111) {
			igt_warn_on_f(!expect_stale_cache,
				      "Found 0x%08x at offset 0x%08x\n", ptr2_cpu[i], i);
			stale++;
		}

	drm_intel_bo_unreference(bo_1);
	drm_intel_bo_unreference(bo_2);
	munmap(ptr_cpu, width * height);
}

int main(int argc, char **argv)
{
	int i;
	bool expect_stale_cache;
	igt_subtest_init(argc, argv);

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);

		bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
		batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
	}

	/* Cache coherency and the eviction are pretty much unpredictable, so
	 * reproducing boils down to trial and error to hit different scenarios.
	 * TODO: We may want to improve tests a bit by picking random subranges. */
	igt_info("%d rounds for each test\n", ROUNDS);
	igt_subtest("read") {
		stale = 0;
		expect_stale_cache = false;
		igt_info("exercising read flush\n");
		for (i = 0; i < ROUNDS; i++)
			test_read_flush(expect_stale_cache);
		igt_fail_on_f(stale, "num of stale cache lines %d\n", stale);
	}

	/* Only for !llc platforms */
	igt_subtest("read-and-fail") {
		stale = 0;
		expect_stale_cache = true;
		igt_info("exercising read flush and expect to fail on !llc\n");
		for (i = 0; i < ROUNDS; i++)
			test_read_flush(expect_stale_cache);
		igt_fail_on_f(!stale, "couldn't find any stale cache lines\n");
	}

	igt_subtest("write") {
		stale = 0;
		expect_stale_cache = false;
		igt_info("exercising write flush\n");
		for (i = 0; i < ROUNDS; i++)
			test_write_flush(expect_stale_cache);
		igt_fail_on_f(stale, "num of stale cache lines %d\n", stale);
	}

	/* Only for !llc platforms */
	igt_subtest("write-and-fail") {
		stale = 0;
		expect_stale_cache = true;
		igt_info("exercising write flush and expect to fail on !llc\n");
		for (i = 0; i < ROUNDS; i++)
			test_write_flush(expect_stale_cache);
		igt_fail_on_f(!stale, "couldn't find any stale cache lines\n");
	}

	igt_fixture {
		intel_batchbuffer_free(batch);
		drm_intel_bufmgr_destroy(bufmgr);

		close(fd);
	}

	igt_exit();
}
