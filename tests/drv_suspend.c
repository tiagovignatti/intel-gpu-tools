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
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <drm.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_aux.h"

#define OBJECT_SIZE (16*1024*1024)

static void
test_fence_restore(int fd, bool tiled2untiled)
{
	uint32_t handle1, handle2, handle_tiled;
	uint32_t *ptr1, *ptr2, *ptr_tiled;
	int i;

	/* We wall the tiled object with untiled canary objects to make sure
	 * that we detect tile leaking in both directions. */
	handle1 = gem_create(fd, OBJECT_SIZE);
	handle2 = gem_create(fd, OBJECT_SIZE);
	handle_tiled = gem_create(fd, OBJECT_SIZE);

	/* Access the buffer objects in the order we want to have the laid out. */
	ptr1 = gem_mmap(fd, handle1, OBJECT_SIZE, PROT_READ | PROT_WRITE);
	igt_assert(ptr1 != MAP_FAILED);
	for (i = 0; i < OBJECT_SIZE/sizeof(uint32_t); i++)
		ptr1[i] = i;

	ptr_tiled = gem_mmap(fd, handle_tiled, OBJECT_SIZE, PROT_READ | PROT_WRITE);
	igt_assert(ptr_tiled != MAP_FAILED);
	if (tiled2untiled)
		gem_set_tiling(fd, handle_tiled, I915_TILING_X, 2048);
	for (i = 0; i < OBJECT_SIZE/sizeof(uint32_t); i++)
		ptr_tiled[i] = i;

	ptr2 = gem_mmap(fd, handle2, OBJECT_SIZE, PROT_READ | PROT_WRITE);
	igt_assert(ptr2 != MAP_FAILED);
	for (i = 0; i < OBJECT_SIZE/sizeof(uint32_t); i++)
		ptr2[i] = i;

	if (tiled2untiled)
		gem_set_tiling(fd, handle_tiled, I915_TILING_NONE, 2048);
	else
		gem_set_tiling(fd, handle_tiled, I915_TILING_X, 2048);

	igt_system_suspend_autoresume();

	igt_info("checking the first canary object\n");
	for (i = 0; i < OBJECT_SIZE/sizeof(uint32_t); i++)
		igt_assert(ptr1[i] == i);

	igt_info("checking the second canary object\n");
	for (i = 0; i < OBJECT_SIZE/sizeof(uint32_t); i++)
		igt_assert(ptr2[i] == i);

	gem_close(fd, handle1);
	gem_close(fd, handle2);
	gem_close(fd, handle_tiled);

	munmap(ptr1, OBJECT_SIZE);
	munmap(ptr2, OBJECT_SIZE);
	munmap(ptr_tiled, OBJECT_SIZE);
}

static void
test_debugfs_reader(void)
{
	struct igt_helper_process reader = {};
	reader.use_SIGKILL = true;

	igt_fork_helper(&reader) {
		static const char dfs_base[] = "/sys/kernel/debug/dri";
		static char tmp[1024];

		snprintf(tmp, sizeof(tmp) - 1,
			 "while true; do find %s/%i/ -type f | xargs cat > /dev/null 2>&1; done",
			 dfs_base, drm_get_card());
		igt_assert(execl("/bin/sh", "sh", "-c", tmp, (char *) NULL) != -1);
	}

	sleep(1);

	igt_system_suspend_autoresume();

	sleep(1);

	igt_stop_helper(&reader);
}

static void
test_sysfs_reader(void)
{
	struct igt_helper_process reader = {};
	reader.use_SIGKILL = true;

	igt_fork_helper(&reader) {
		static const char dfs_base[] = "/sys/class/drm/card";
		static char tmp[1024];

		snprintf(tmp, sizeof(tmp) - 1,
			 "while true; do find %s%i*/ -type f | xargs cat > /dev/null 2>&1; done",
			 dfs_base, drm_get_card());
		igt_assert(execl("/bin/sh", "sh", "-c", tmp, (char *) NULL) != -1);
	}

	sleep(1);

	igt_system_suspend_autoresume();

	sleep(1);

	igt_stop_helper(&reader);
}

static void
test_forcewake(void)
{
	int fw_fd;

	fw_fd = igt_open_forcewake_handle();
	igt_assert(fw_fd >= 0);
	igt_system_suspend_autoresume();
	close (fw_fd);
}

int fd;

igt_main
{
	igt_skip_on_simulation();

	igt_fixture
		fd = drm_open_any();

	igt_subtest("fence-restore-tiled2untiled")
		test_fence_restore(fd, true);

	igt_subtest("fence-restore-untiled")
		test_fence_restore(fd, false);

	igt_subtest("debugfs-reader")
		test_debugfs_reader();

	igt_subtest("sysfs-reader")
		test_sysfs_reader();

	igt_subtest("forcewake")
		test_forcewake();

	igt_fixture
		close(fd);
}
