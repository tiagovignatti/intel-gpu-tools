/*
 * Copyright © 2007, 2011, 2013 Intel Corporation
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
 *    Eric Anholt <eric@anholt.net>
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#ifndef ANDROID
#define _GNU_SOURCE
#else
#include <libgen.h>
#endif
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <pciaccess.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <termios.h>

#include "drmtest.h"
#include "i915_drm.h"
#include "intel_chipset.h"
#include "intel_gpu_tools.h"
#include "igt_debugfs.h"
#include "../version.h"
#include "config.h"
#include "intel_reg.h"
#include "ioctl_wrappers.h"

/* This file contains a bunch of wrapper functions to directly use gem ioctls.
 * Mostly useful to write kernel tests. */

static int
is_intel(int fd)
{
	struct drm_i915_getparam gp;
	int devid;

	gp.param = I915_PARAM_CHIPSET_ID;
	gp.value = &devid;

	if (ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp)))
		return 0;

	return IS_INTEL(devid);
}

#define LOCAL_I915_EXEC_VEBOX	(4 << 0)
/* Ensure the gpu is idle by launching a nop execbuf and stalling for it. */
void gem_quiescent_gpu(int fd)
{
	uint32_t batch[2] = {MI_BATCH_BUFFER_END, 0};
	uint32_t handle;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];

	handle = gem_create(fd, 4096);
	gem_write(fd, handle, 0, batch, sizeof(batch));

	gem_exec[0].handle = handle;
	gem_exec[0].relocation_count = 0;
	gem_exec[0].relocs_ptr = 0;
	gem_exec[0].alignment = 0;
	gem_exec[0].offset = 0;
	gem_exec[0].flags = 0;
	gem_exec[0].rsvd1 = 0;
	gem_exec[0].rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = 8;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = 0;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	do_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);

	if (gem_has_blt(fd)) {
		execbuf.flags = I915_EXEC_BLT;
		do_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
	}

	if (gem_has_bsd(fd)) {
		execbuf.flags = I915_EXEC_BSD;
		do_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
	}

	if (gem_has_vebox(fd)) {
		execbuf.flags = LOCAL_I915_EXEC_VEBOX;
		do_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
	}

	gem_sync(fd, handle);
	igt_drop_caches_set(DROP_RETIRE);
	gem_close(fd, handle);
}

/**
 * drm_get_card:
 *
 * Get an intel card number for use in /dev or /sys
 *
 * Returns: -1 on error
 */
int drm_get_card(void)
{
	char *name;
	int i, fd;

	for (i = 0; i < 16; i++) {
		int ret;

		ret = asprintf(&name, "/dev/dri/card%u", i);
		igt_assert(ret != -1);

		fd = open(name, O_RDWR);
		free(name);

		if (fd == -1)
			continue;

		if (!is_intel(fd)) {
			close(fd);
			continue;
		}

		close(fd);
		return i;
	}

	igt_skip("No intel gpu found\n");

	return -1;
}

/** Open the first DRM device we can find, searching up to 16 device nodes */
static int __drm_open_any(void)
{
	char *name;
	int ret, fd;

	ret = asprintf(&name, "/dev/dri/card%d", drm_get_card());
	if (ret == -1)
		return -1;

	fd = open(name, O_RDWR);
	free(name);

	if (!is_intel(fd)) {
		close(fd);
		fd = -1;
	}

	return fd;
}

static int __drm_open_any_render(void)
{
	char *name;
	int i, fd;

	for (i = 128; i < (128 + 16); i++) {
		int ret;

		ret = asprintf(&name, "/dev/dri/renderD%u", i);
		igt_assert(ret != -1);

		fd = open(name, O_RDWR);
		free(name);

		if (fd == -1)
			continue;

		if (!is_intel(fd)) {
			close(fd);
			fd = -1;
			continue;
		}

		return fd;
	}

	return fd;
}

static int at_exit_drm_fd = -1;
static int at_exit_drm_render_fd = -1;

static void quiescent_gpu_at_exit(int sig)
{
	if (at_exit_drm_fd < 0)
		return;

	gem_quiescent_gpu(at_exit_drm_fd);
	close(at_exit_drm_fd);
	at_exit_drm_fd = -1;
}

static void quiescent_gpu_at_exit_render(int sig)
{
	if (at_exit_drm_render_fd < 0)
		return;

	gem_quiescent_gpu(at_exit_drm_render_fd);
	close(at_exit_drm_render_fd);
	at_exit_drm_render_fd = -1;
}

int drm_open_any(void)
{
	static int open_count;
	int fd = __drm_open_any();

	igt_require(fd >= 0);

	if (__sync_fetch_and_add(&open_count, 1))
		return fd;

	gem_quiescent_gpu(fd);
	at_exit_drm_fd = dup(fd);
	igt_install_exit_handler(quiescent_gpu_at_exit);

	return fd;
}

int drm_open_any_render(void)
{
	static int open_count;
	int fd = __drm_open_any_render();

	/* no render nodes, fallback to drm_open_any() */
	if (fd == -1)
		return drm_open_any();

	if (__sync_fetch_and_add(&open_count, 1))
		return fd;

	at_exit_drm_render_fd = dup(fd);
	gem_quiescent_gpu(fd);
	igt_install_exit_handler(quiescent_gpu_at_exit_render);

	return fd;
}

/* signal interrupt helpers */
static struct igt_helper_process signal_helper;
long long int sig_stat;
static void __attribute__((noreturn)) signal_helper_process(pid_t pid)
{
	/* Interrupt the parent process at 500Hz, just to be annoying */
	while (1) {
		usleep(1000 * 1000 / 500);
		if (kill(pid, SIGUSR1)) /* Parent has died, so must we. */
			exit(0);
	}
}

static void sig_handler(int i)
{
	sig_stat++;
}

void igt_fork_signal_helper(void)
{
	if (igt_only_list_subtests())
		return;

	signal(SIGUSR1, sig_handler);

	igt_fork_helper(&signal_helper) {
		signal_helper_process(getppid());
	}
}

void igt_stop_signal_helper(void)
{
	if (igt_only_list_subtests())
		return;

	igt_stop_helper(&signal_helper);

	sig_stat = 0;
}

bool igt_env_set(const char *env_var, bool default_value)
{
	char *val;

	val = getenv(env_var);
	if (!val)
		return default_value;

	return atoi(val) != 0;
}

bool drmtest_dump_aub(void)
{
	static int dump_aub = -1;

	if (dump_aub == -1)
		dump_aub = igt_env_set("IGT_DUMP_AUB", false);

	return dump_aub;
}

/* other helpers */
void igt_exchange_int(void *array, unsigned i, unsigned j)
{
	int *int_arr, tmp;
	int_arr = array;

	tmp = int_arr[i];
	int_arr[i] = int_arr[j];
	int_arr[j] = tmp;
}

void igt_permute_array(void *array, unsigned size,
			   void (*exchange_func)(void *array,
						 unsigned i,
						 unsigned j))
{
	int i;

	for (i = size - 1; i > 1; i--) {
		/* yes, not perfectly uniform, who cares */
		long l = random() % (i +1);
		if (i != l)
			exchange_func(array, i, l);
	}
}

void igt_progress(const char *header, uint64_t i, uint64_t total)
{
	int divider = 200;

	if (!isatty(fileno(stderr)))
		return;

	if (i+1 >= total) {
		fprintf(stderr, "\r%s100%%\n", header);
		return;
	}

	if (total / 200 == 0)
		divider = 1;

	/* only bother updating about every 0.5% */
	if (i % (total / divider) == 0 || i+1 >= total) {
		fprintf(stderr, "\r%s%3llu%%", header,
			(long long unsigned) i * 100 / total);
	}
}

/* mappable aperture trasher helper */
drm_intel_bo **trash_bos;
int num_trash_bos;

void igt_init_aperture_trashers(drm_intel_bufmgr *bufmgr)
{
	int i;

	num_trash_bos = gem_mappable_aperture_size() / (1024*1024);

	trash_bos = malloc(num_trash_bos * sizeof(drm_intel_bo *));
	igt_assert(trash_bos);

	for (i = 0; i < num_trash_bos; i++)
		trash_bos[i] = drm_intel_bo_alloc(bufmgr, "trash bo", 1024*1024, 4096);
}

void igt_trash_aperture(void)
{
	int i;
	uint8_t *gtt_ptr;

	for (i = 0; i < num_trash_bos; i++) {
		drm_intel_gem_bo_map_gtt(trash_bos[i]);
		gtt_ptr = trash_bos[i]->virtual;
		*gtt_ptr = 0;
		drm_intel_gem_bo_unmap_gtt(trash_bos[i]);
	}
}

void igt_cleanup_aperture_trashers(void)
{
	int i;

	for (i = 0; i < num_trash_bos; i++)
		drm_intel_bo_unreference(trash_bos[i]);

	free(trash_bos);
}

void igt_system_suspend_autoresume(void)
{
	int ret;

	/* FIXME: Simulation doesn't like suspend/resume, and not even a lighter
	 * approach using /sys/power/pm_test to just test our driver's callbacks
	 * seems to fare better. We need to investigate what's going on. */
	igt_skip_on_simulation();

	ret = system("rtcwake -s 30 -m mem");
	igt_assert(ret == 0);
}

void igt_drop_root(void)
{
	igt_assert(getuid() == 0);

	igt_assert(setgid(2) == 0);
	igt_assert(setuid(2) == 0);

	igt_assert(getgid() == 2);
	igt_assert(getuid() == 2);
}

void igt_wait_for_keypress(void)
{
	struct termios oldt, newt;

	if (!isatty(STDIN_FILENO))
		return;

	tcgetattr ( STDIN_FILENO, &oldt );
	newt = oldt;
	newt.c_lflag &= ~( ICANON | ECHO );
	tcsetattr ( STDIN_FILENO, TCSANOW, &newt );
	getchar();
	tcsetattr ( STDIN_FILENO, TCSANOW, &oldt );
}
