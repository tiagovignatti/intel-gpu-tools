/*
 * Copyright © 2012 Intel Corporation
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
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

/*
 * Testcase: Check whether prime import/export works on the same device
 *
 * ... but with different fds, i.e. the wayland usecase.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"

#define BO_SIZE (16*1024)

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))

static char counter;

static void
check_bo(int fd1, uint32_t handle1, int fd2, uint32_t handle2)
{
	char *ptr1, *ptr2;
	int i;

	ptr1 = gem_mmap(fd1, handle1, BO_SIZE, PROT_READ | PROT_WRITE);
	ptr2 = gem_mmap(fd2, handle2, BO_SIZE, PROT_READ | PROT_WRITE);

	assert(ptr1);

	/* check whether it's still our old object first. */
	for (i = 0; i < BO_SIZE; i++) {
		assert(ptr1[i] == counter);
		assert(ptr2[i] == counter);
	}

	counter++;

	memset(ptr1, counter, BO_SIZE);
	assert(memcmp(ptr1, ptr2, BO_SIZE) == 0);

	munmap(ptr1, BO_SIZE);
	munmap(ptr2, BO_SIZE);
}

static void test_with_fd_dup(void)
{
	int fd1, fd2;
	uint32_t handle, handle_import;
	int dma_buf_fd1, dma_buf_fd2;

	counter = 0;

	fd1 = drm_open_any();
	fd2 = drm_open_any();

	handle = gem_create(fd1, BO_SIZE);

	dma_buf_fd1 = prime_handle_to_fd(fd1, handle);
	gem_close(fd1, handle);

	dma_buf_fd2 = dup(dma_buf_fd1);
	close(dma_buf_fd1);
	handle_import = prime_fd_to_handle(fd2, dma_buf_fd2);
	check_bo(fd2, handle_import, fd2, handle_import);

	close(dma_buf_fd2);
	check_bo(fd2, handle_import, fd2, handle_import);

	close(fd1);
	close(fd2);
}

static void test_with_two_bos(void)
{
	int fd1, fd2;
	uint32_t handle1, handle2, handle_import;
	int dma_buf_fd;

	counter = 0;

	fd1 = drm_open_any();
	fd2 = drm_open_any();

	handle1 = gem_create(fd1, BO_SIZE);
	handle2 = gem_create(fd1, BO_SIZE);

	dma_buf_fd = prime_handle_to_fd(fd1, handle1);
	handle_import = prime_fd_to_handle(fd2, dma_buf_fd);

	close(dma_buf_fd);
	gem_close(fd1, handle1);

	dma_buf_fd = prime_handle_to_fd(fd1, handle2);
	handle_import = prime_fd_to_handle(fd2, dma_buf_fd);
	check_bo(fd1, handle2, fd2, handle_import);

	gem_close(fd1, handle2);
	close(dma_buf_fd);

	check_bo(fd2, handle_import, fd2, handle_import);

	close(fd1);
	close(fd2);
}

static void test_with_one_bo(void)
{
	int fd1, fd2;
	uint32_t handle, handle_import1, handle_import2, handle_selfimport;
	int dma_buf_fd;

	fd1 = drm_open_any();
	fd2 = drm_open_any();

	handle = gem_create(fd1, BO_SIZE);

	dma_buf_fd = prime_handle_to_fd(fd1, handle);
	handle_import1 = prime_fd_to_handle(fd2, dma_buf_fd);

	check_bo(fd1, handle, fd2, handle_import1);

	/* reimport should give us the same handle so that userspace can check
	 * whether it has that bo already somewhere. */
	handle_import2 = prime_fd_to_handle(fd2, dma_buf_fd);
	assert(handle_import1 == handle_import2);

	/* Same for re-importing on the exporting fd. */
	handle_selfimport = prime_fd_to_handle(fd1, dma_buf_fd);
	assert(handle == handle_selfimport);

	/* close dma_buf, check whether nothing disappears. */
	close(dma_buf_fd);
	check_bo(fd1, handle, fd2, handle_import1);

	gem_close(fd1, handle);
	check_bo(fd2, handle_import1, fd2, handle_import1);

	/* re-import into old exporter */
	dma_buf_fd = prime_handle_to_fd(fd2, handle_import1);
	/* but drop all references to the obj in between */
	gem_close(fd2, handle_import1);
	handle = prime_fd_to_handle(fd1, dma_buf_fd);
	handle_import1 = prime_fd_to_handle(fd2, dma_buf_fd);
	check_bo(fd1, handle, fd2, handle_import1);

	/* Completely rip out exporting fd. */
	close(fd1);
	check_bo(fd2, handle_import1, fd2, handle_import1);
}

int main(int argc, char **argv)
{
	struct {
		const char *name;
		void (*fn)(void);
	} tests[] = {
		{ "with_one_bo", test_with_one_bo },
		{ "with_two_bos", test_with_two_bos },
		{ "with_fd_dup", test_with_fd_dup },
	};
	int i;

	drmtest_subtest_init(argc, argv);

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (drmtest_run_subtest(tests[i].name))
			tests[i].fn();
	}

	return 0;
}
