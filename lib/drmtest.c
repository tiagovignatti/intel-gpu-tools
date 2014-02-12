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

/* This file contains a bunch of wrapper functions to directly use gem ioctls.
 * Mostly useful to write kernel tests. */

drm_intel_bo *
gem_handle_to_libdrm_bo(drm_intel_bufmgr *bufmgr, int fd, const char *name, uint32_t handle)
{
	struct drm_gem_flink flink;
	int ret;
	drm_intel_bo *bo;

	flink.handle = handle;
	ret = ioctl(fd, DRM_IOCTL_GEM_FLINK, &flink);
	igt_assert(ret == 0);

	bo = drm_intel_bo_gem_create_from_name(bufmgr, name, flink.name);
	igt_assert(bo);

	return bo;
}

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

bool gem_uses_aliasing_ppgtt(int fd)
{
	struct drm_i915_getparam gp;
	int val;

	gp.param = 18; /* HAS_ALIASING_PPGTT */
	gp.value = &val;

	if (ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp)))
		return 0;

	return val;
}

int gem_available_fences(int fd)
{
	struct drm_i915_getparam gp;
	int val;

	gp.param = I915_PARAM_NUM_FENCES_AVAIL;
	gp.value = &val;

	if (ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp)))
		return 0;

	return val;
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
 * drm_get_card() - get an intel card number for use in /dev or /sys
 *
 * returns -1 on error
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

static void oom_adjust_for_doom(void)
{
	int fd;
	const char always_kill[] = "1000";

	fd = open("/proc/self/oom_score_adj", O_WRONLY);
	igt_assert(fd != -1);
	igt_assert(write(fd, always_kill, sizeof(always_kill)) == sizeof(always_kill));
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

	oom_adjust_for_doom();

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

	oom_adjust_for_doom();

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

int __gem_set_tiling(int fd, uint32_t handle, uint32_t tiling, uint32_t stride)
{
	struct drm_i915_gem_set_tiling st;
	int ret;

	memset(&st, 0, sizeof(st));
	do {
		st.handle = handle;
		st.tiling_mode = tiling;
		st.stride = tiling ? stride : 0;

		ret = ioctl(fd, DRM_IOCTL_I915_GEM_SET_TILING, &st);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
	if (ret != 0)
		return -errno;

	igt_assert(st.tiling_mode == tiling);
	return 0;
}

void gem_set_tiling(int fd, uint32_t handle, uint32_t tiling, uint32_t stride)
{
	igt_assert(__gem_set_tiling(fd, handle, tiling, stride) == 0);
}

bool gem_has_enable_ring(int fd,int param)
{
	drm_i915_getparam_t gp;
	int ret, tmp;
	memset(&gp, 0, sizeof(gp));

	gp.value = &tmp;
	gp.param = param;

	ret = drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);

	if ((ret == 0) && (*gp.value > 0))
		return true;
	else
		return false;
}

bool gem_has_bsd(int fd)
{

	return gem_has_enable_ring(fd,I915_PARAM_HAS_BSD);
}

bool gem_has_blt(int fd)
{

	return gem_has_enable_ring(fd,I915_PARAM_HAS_BLT);
}

#define LOCAL_I915_PARAM_HAS_VEBOX 22
bool gem_has_vebox(int fd)
{

	return gem_has_enable_ring(fd,LOCAL_I915_PARAM_HAS_VEBOX);
}

int gem_get_num_rings(int fd)
{
	int num_rings = 1;	/* render ring is always available */

	if (gem_has_bsd(fd))
		num_rings++;
	else
		goto skip;

	if (gem_has_blt(fd))
		num_rings++;
	else
		goto skip;

	if (gem_has_vebox(fd))
		num_rings++;
	else
		goto skip;


skip:
	return num_rings;
}

struct local_drm_i915_gem_caching {
	uint32_t handle;
	uint32_t caching;
};

#define LOCAL_DRM_I915_GEM_SET_CACHEING    0x2f
#define LOCAL_DRM_I915_GEM_GET_CACHEING    0x30
#define LOCAL_DRM_IOCTL_I915_GEM_SET_CACHEING \
	DRM_IOW(DRM_COMMAND_BASE + LOCAL_DRM_I915_GEM_SET_CACHEING, struct local_drm_i915_gem_caching)
#define LOCAL_DRM_IOCTL_I915_GEM_GET_CACHEING \
	DRM_IOWR(DRM_COMMAND_BASE + LOCAL_DRM_I915_GEM_GET_CACHEING, struct local_drm_i915_gem_caching)

void gem_require_caching(int fd)
{
	struct local_drm_i915_gem_caching arg;
	int ret;

	arg.handle = gem_create(fd, 4096);
	igt_assert(arg.handle != 0);

	arg.caching = 0;
	ret = ioctl(fd, LOCAL_DRM_IOCTL_I915_GEM_SET_CACHEING, &arg);
	gem_close(fd, arg.handle);

	igt_require(ret == 0);
}

void gem_set_caching(int fd, uint32_t handle, int caching)
{
	struct local_drm_i915_gem_caching arg;
	int ret;

	arg.handle = handle;
	arg.caching = caching;
	ret = ioctl(fd, LOCAL_DRM_IOCTL_I915_GEM_SET_CACHEING, &arg);

	igt_assert(ret == 0 || (errno == ENOTTY || errno == EINVAL));
	igt_require(ret == 0);
}

uint32_t gem_get_caching(int fd, uint32_t handle)
{
	struct local_drm_i915_gem_caching arg;
	int ret;

	arg.handle = handle;
	arg.caching = 0;
	ret = ioctl(fd, LOCAL_DRM_IOCTL_I915_GEM_GET_CACHEING, &arg);
	igt_assert(ret == 0);

	return arg.caching;
}

uint32_t gem_open(int fd, uint32_t name)
{
	struct drm_gem_open open_struct;
	int ret;

	open_struct.name = name;
	ret = ioctl(fd, DRM_IOCTL_GEM_OPEN, &open_struct);
	igt_assert(ret == 0);
	igt_assert(open_struct.handle != 0);

	return open_struct.handle;
}

uint32_t gem_flink(int fd, uint32_t handle)
{
	struct drm_gem_flink flink;
	int ret;

	flink.handle = handle;
	ret = ioctl(fd, DRM_IOCTL_GEM_FLINK, &flink);
	igt_assert(ret == 0);

	return flink.name;
}

void gem_close(int fd, uint32_t handle)
{
	struct drm_gem_close close_bo;

	close_bo.handle = handle;
	do_ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_bo);
}

void gem_write(int fd, uint32_t handle, uint32_t offset, const void *buf, uint32_t size)
{
	struct drm_i915_gem_pwrite gem_pwrite;

	gem_pwrite.handle = handle;
	gem_pwrite.offset = offset;
	gem_pwrite.size = size;
	gem_pwrite.data_ptr = (uintptr_t)buf;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite);
}

void gem_read(int fd, uint32_t handle, uint32_t offset, void *buf, uint32_t length)
{
	struct drm_i915_gem_pread gem_pread;

	gem_pread.handle = handle;
	gem_pread.offset = offset;
	gem_pread.size = length;
	gem_pread.data_ptr = (uintptr_t)buf;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_PREAD, &gem_pread);
}

void gem_set_domain(int fd, uint32_t handle,
		    uint32_t read_domains, uint32_t write_domain)
{
	struct drm_i915_gem_set_domain set_domain;

	set_domain.handle = handle;
	set_domain.read_domains = read_domains;
	set_domain.write_domain = write_domain;

	do_ioctl(fd, DRM_IOCTL_I915_GEM_SET_DOMAIN, &set_domain);
}

void gem_sync(int fd, uint32_t handle)
{
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
}

uint32_t __gem_create(int fd, int size)
{
	struct drm_i915_gem_create create;
	int ret;

	create.handle = 0;
	create.size = size;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);

	if (ret < 0)
		return 0;
	else
		return create.handle;
}

uint32_t gem_create(int fd, int size)
{
	struct drm_i915_gem_create create;

	create.handle = 0;
	create.size = size;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);
	igt_assert(create.handle);

	return create.handle;
}

void gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *execbuf)
{
	int ret;

	ret = drmIoctl(fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER2,
		       execbuf);
	igt_assert(ret == 0);
}

void *gem_mmap__gtt(int fd, uint32_t handle, int size, int prot)
{
	struct drm_i915_gem_mmap_gtt mmap_arg;
	void *ptr;

	mmap_arg.handle = handle;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_arg))
		return NULL;

	ptr = mmap64(0, size, prot, MAP_SHARED, fd, mmap_arg.offset);
	if (ptr == MAP_FAILED)
		ptr = NULL;

	return ptr;
}

void *gem_mmap__cpu(int fd, uint32_t handle, int size, int prot)
{
	struct drm_i915_gem_mmap mmap_arg;

	mmap_arg.handle = handle;
	mmap_arg.offset = 0;
	mmap_arg.size = size;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_MMAP, &mmap_arg))
		return NULL;

	return (void *)(uintptr_t)mmap_arg.addr_ptr;
}

uint64_t gem_available_aperture_size(int fd)
{
	struct drm_i915_gem_get_aperture aperture;

	aperture.aper_size = 256*1024*1024;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_GET_APERTURE, &aperture);
	return aperture.aper_available_size;
}

uint64_t gem_aperture_size(int fd)
{
	struct drm_i915_gem_get_aperture aperture;

	aperture.aper_size = 256*1024*1024;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_GET_APERTURE, &aperture);
	return aperture.aper_size;
}

uint64_t gem_mappable_aperture_size(void)
{
	struct pci_device *pci_dev;
	int bar;
	pci_dev = intel_get_pci_device();

	if (intel_gen(pci_dev->device_id) < 3)
		bar = 0;
	else
		bar = 2;

	return pci_dev->regions[bar].size;
}

int gem_madvise(int fd, uint32_t handle, int state)
{
	struct drm_i915_gem_madvise madv;

	madv.handle = handle;
	madv.madv = state;
	madv.retained = 1;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_MADVISE, &madv);

	return madv.retained;
}

uint32_t gem_context_create(int fd)
{
	struct drm_i915_gem_context_create create;
	int ret;

	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, &create);
	igt_require(ret == 0 || (errno != ENODEV && errno != EINVAL));
	igt_assert(ret == 0);

	return create.ctx_id;
}

void gem_sw_finish(int fd, uint32_t handle)
{
	struct drm_i915_gem_sw_finish finish;

	finish.handle = handle;

	do_ioctl(fd, DRM_IOCTL_I915_GEM_SW_FINISH, &finish);
}

bool gem_bo_busy(int fd, uint32_t handle)
{
	struct drm_i915_gem_busy busy;

	busy.handle = handle;

	do_ioctl(fd, DRM_IOCTL_I915_GEM_BUSY, &busy);

	return !!busy.busy;
}

/* prime */
int prime_handle_to_fd(int fd, uint32_t handle)
{
	struct drm_prime_handle args;

	args.handle = handle;
	args.flags = DRM_CLOEXEC;
	args.fd = -1;

	do_ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);

	return args.fd;
}

uint32_t prime_fd_to_handle(int fd, int dma_buf_fd)
{
	struct drm_prime_handle args;

	args.fd = dma_buf_fd;
	args.flags = 0;
	args.handle = 0;

	do_ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &args);

	return args.handle;
}

off_t prime_get_size(int dma_buf_fd)
{
	off_t ret;
	ret = lseek(dma_buf_fd, 0, SEEK_END);
	igt_assert(ret >= 0 || errno == ESPIPE);
	igt_require(ret >= 0);

	return ret;
}

/* signal interrupt helpers */
static bool igt_only_list_subtests(void);

static unsigned int exit_handler_count;

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

/* subtests helpers */
static bool list_subtests = false;
static char *run_single_subtest = NULL;
static const char *in_subtest = NULL;
static bool in_fixture = false;
static bool test_with_subtests = false;
static enum {
	CONT = 0, SKIP, FAIL
} skip_subtests_henceforth = CONT;

/* fork support state */
pid_t *test_children;
int num_test_children;
int test_children_sz;
bool test_child;

bool __igt_fixture(void)
{
	assert(!in_fixture);

	if (igt_only_list_subtests())
		return false;

	if (skip_subtests_henceforth)
		return false;

	in_fixture = true;
	return true;
}

void __igt_fixture_complete(void)
{
	assert(in_fixture);

	in_fixture = false;
}

void __igt_fixture_end(void)
{
	assert(in_fixture);

	in_fixture = false;
	longjmp(igt_subtest_jmpbuf, 1);
}

bool igt_exit_called;
static void check_igt_exit(int sig)
{
	/* When not killed by a signal check that igt_exit() has been properly
	 * called. */
	assert(sig != 0 || igt_exit_called);
}


static void print_version(void)
{
	struct utsname uts;

	if (list_subtests)
		return;

	uname(&uts);

	fprintf(stdout, "IGT-Version: %s-%s (%s) (%s: %s %s)\n", PACKAGE_VERSION,
		IGT_GIT_SHA1, TARGET_CPU_PLATFORM,
		uts.sysname, uts.release, uts.machine);
}

static void print_usage(const char *command_str, const char *help_str,
			bool output_on_stderr)
{
	FILE *f = output_on_stderr ? stderr : stdout;

	fprintf(f, "Usage: %s [OPTIONS]\n"
		   "  --list-subtests\n"
		   "  --run-subtest <pattern>\n", command_str);
	if (help_str)
		fprintf(f, "%s\n", help_str);
}

int igt_subtest_init_parse_opts(int argc, char **argv,
				const char *extra_short_opts,
				struct option *extra_long_opts,
				const char *help_str,
				igt_opt_handler_t extra_opt_handler)
{
	int c, option_index = 0;
	static struct option long_options[] = {
		{"list-subtests", 0, 0, 'l'},
		{"run-subtest", 1, 0, 'r'},
		{"help", 0, 0, 'h'},
	};
	const char *command_str;
	char *short_opts;
	struct option *combined_opts;
	int extra_opt_count;
	int all_opt_count;
	int ret = 0;

	test_with_subtests = true;

	command_str = argv[0];
	if (strrchr(command_str, '/'))
		command_str = strrchr(command_str, '/') + 1;

	/* First calculate space for all passed-in extra long options */
	all_opt_count = 0;
	while (extra_long_opts && extra_long_opts[all_opt_count].name)
		all_opt_count++;
	extra_opt_count = all_opt_count;

	all_opt_count += ARRAY_SIZE(long_options);

	combined_opts = malloc(all_opt_count * sizeof(*combined_opts));
	memcpy(combined_opts, extra_long_opts,
	       extra_opt_count * sizeof(*combined_opts));

	/* Copy the subtest long options (and the final NULL entry) */
	memcpy(&combined_opts[extra_opt_count], long_options,
		ARRAY_SIZE(long_options) * sizeof(*combined_opts));

	ret = asprintf(&short_opts, "%sh",
		       extra_short_opts ? extra_short_opts : "");
	assert(ret >= 0);

	while ((c = getopt_long(argc, argv, short_opts, combined_opts,
			       &option_index)) != -1) {
		switch(c) {
		case 'l':
			if (!run_single_subtest)
				list_subtests = true;
			break;
		case 'r':
			if (!list_subtests)
				run_single_subtest = strdup(optarg);
			break;
		case 'h':
			print_usage(command_str, help_str, false);
			ret = -1;
			goto out;
		case '?':
			if (opterr) {
				print_usage(command_str, help_str, true);
				ret = -2;
				goto out;
			}
			/*
			 * Just ignore the error, since the unknown argument
			 * can be something the caller understands and will
			 * parse by doing a second getopt scanning.
			 */
			break;
		default:
			ret = extra_opt_handler(c, option_index);
			if (ret)
				goto out;
		}
	}

	igt_install_exit_handler(check_igt_exit);
	oom_adjust_for_doom();

out:
	free(short_opts);
	free(combined_opts);
	print_version();

	return ret;
}

enum igt_log_level igt_log_level = IGT_LOG_INFO;

static void common_init(void)
{
	char *env = getenv("IGT_LOG_LEVEL");

	if (!env)
		return;

	if (strcmp(env, "debug") == 0)
		igt_log_level = IGT_LOG_DEBUG;
	else if (strcmp(env, "info") == 0)
		igt_log_level = IGT_LOG_INFO;
	else if (strcmp(env, "warn") == 0)
		igt_log_level = IGT_LOG_WARN;
	else if (strcmp(env, "none") == 0)
		igt_log_level = IGT_LOG_NONE;
}

void igt_subtest_init(int argc, char **argv)
{
	int ret;

	/* supress getopt errors about unknown options */
	opterr = 0;

	ret = igt_subtest_init_parse_opts(argc, argv, NULL, NULL, NULL, NULL);
	if (ret < 0)
		/* exit with no error for -h/--help */
		exit(ret == -1 ? 0 : ret);

	/* reset opt parsing */
	optind = 1;

	common_init();
}

void igt_simple_init(void)
{
	print_version();

	common_init();
}

/*
 * Note: Testcases which use these helpers MUST NOT output anything to stdout
 * outside of places protected by igt_run_subtest checks - the piglit
 * runner adds every line to the subtest list.
 */
bool __igt_run_subtest(const char *subtest_name)
{
	assert(!in_subtest);
	assert(!in_fixture);

	if (list_subtests) {
		printf("%s\n", subtest_name);
		return false;
	}

	if (run_single_subtest &&
	    strcmp(subtest_name, run_single_subtest) != 0)
		return false;

	if (skip_subtests_henceforth) {
		printf("Subtest %s: %s\n", subtest_name,
		       skip_subtests_henceforth == SKIP ?
		       "SKIP" : "FAIL");
		return false;
	}

	return (in_subtest = subtest_name);
}

const char *igt_subtest_name(void)
{
	return in_subtest;
}

static bool igt_only_list_subtests(void)
{
	return list_subtests;
}

static bool skipped_one = false;
static bool succeeded_one = false;
static bool failed_one = false;
static int igt_exitcode;

static void exit_subtest(const char *) __attribute__((noreturn));
static void exit_subtest(const char *result)
{
	printf("Subtest %s: %s\n", in_subtest, result);
	in_subtest = NULL;
	longjmp(igt_subtest_jmpbuf, 1);
}

void igt_skip(const char *f, ...)
{
	va_list args;
	skipped_one = true;

	assert(!test_child);

	if (!igt_only_list_subtests()) {
		va_start(args, f);
		vprintf(f, args);
		va_end(args);
	}

	if (in_subtest) {
		exit_subtest("SKIP");
	} else if (test_with_subtests) {
		skip_subtests_henceforth = SKIP;
		assert(in_fixture);
		__igt_fixture_end();
	} else {
		exit(77);
	}
}

void __igt_skip_check(const char *file, const int line,
		      const char *func, const char *check,
		      const char *f, ...)
{
	va_list args;
	int err = errno;

	if (f) {
		static char *buf;

		/* igt_skip never returns, so try to not leak too badly. */
		if (buf)
			free(buf);

		va_start(args, f);
		vasprintf(&buf, f, args);
		va_end(args);

		igt_skip("Test requirement not met in function %s, file %s:%i:\n"
			 "Last errno: %i, %s\n"
			 "Test requirement: (%s)\n%s",
			 func, file, line, err, strerror(err), check, buf);
	} else {
		igt_skip("Test requirement not met in function %s, file %s:%i:\n"
			 "Last errno: %i, %s\n"
			 "Test requirement: (%s)\n",
			 func, file, line, err, strerror(err), check);
	}
}

void igt_success(void)
{
	succeeded_one = true;
	if (in_subtest)
		exit_subtest("SUCCESS");
}

void igt_fail(int exitcode)
{
	assert(exitcode != 0 && exitcode != 77);

	if (!failed_one)
		igt_exitcode = exitcode;

	failed_one = true;

	/* Silent exit, parent will do the yelling. */
	if (test_child)
		exit(exitcode);

	if (in_subtest)
		exit_subtest("FAIL");
	else {
		assert(!test_with_subtests || in_fixture);

		if (in_fixture) {
			skip_subtests_henceforth = FAIL;
			__igt_fixture_end();
		}

		exit(exitcode);
	}
}

static bool run_under_gdb(void)
{
	char buf[1024];

	sprintf(buf, "/proc/%d/exe", getppid());
	return (readlink (buf, buf, sizeof (buf)) != -1 &&
		strncmp(basename(buf), "gdb", 3) == 0);
}

void __igt_fail_assert(int exitcode, const char *file,
		       const int line, const char *func, const char *assertion,
		       const char *f, ...)
{
	va_list args;
	int err = errno;

	printf("Test assertion failure function %s, file %s:%i:\n"
	       "Last errno: %i, %s\n"
	       "Failed assertion: %s\n",
	       func, file, line, err, strerror(err), assertion);

	if (f) {
		va_start(args, f);
		vprintf(f, args);
		va_end(args);
	}

	if (run_under_gdb())
		abort();
	igt_fail(exitcode);
}

void igt_exit(void)
{
	igt_exit_called = true;

	if (igt_only_list_subtests())
		exit(0);

	if (!test_with_subtests)
		exit(0);

	/* Calling this without calling one of the above is a failure */
	assert(skipped_one || succeeded_one || failed_one);

	if (failed_one)
		exit(igt_exitcode);
	else if (succeeded_one)
		exit(0);
	else
		exit(77);
}

static int helper_process_count;
static pid_t helper_process_pids[] =
{ -1, -1, -1, -1};

static void reset_helper_process_list(void)
{
	for (int i = 0; i < ARRAY_SIZE(helper_process_pids); i++)
		helper_process_pids[i] = -1;
	helper_process_count = 0;
}

static void fork_helper_exit_handler(int sig)
{
	for (int i = 0; i < ARRAY_SIZE(helper_process_pids); i++) {
		pid_t pid = helper_process_pids[i];
		int status, ret;

		if (pid != -1) {
			/* Someone forgot to fill up the array? */
			assert(pid != 0);

			ret = kill(pid, SIGQUIT);
			assert(ret == 0);
			while (waitpid(pid, &status, 0) == -1 &&
			       errno == EINTR)
				;
			helper_process_count--;
		}
	}

	assert(helper_process_count == 0);
}

bool __igt_fork_helper(struct igt_helper_process *proc)
{
	pid_t pid;
	int id;

	assert(!proc->running);
	assert(helper_process_count < ARRAY_SIZE(helper_process_pids));

	for (id = 0; helper_process_pids[id] != -1; id++)
		;

	igt_install_exit_handler(fork_helper_exit_handler);

	switch (pid = fork()) {
	case -1:
		igt_assert(0);
	case 0:
		exit_handler_count = 0;
		reset_helper_process_list();
		oom_adjust_for_doom();

		return true;
	default:
		proc->running = true;
		proc->pid = pid;
		proc->id = id;
		helper_process_pids[id] = pid;
		helper_process_count++;

		return false;
	}

}

/**
 * igt_waitchildren - wait for all children forked with igt_fork
 *
 * The magic here is that exit codes from children will be correctly propagated
 */
void igt_stop_helper(struct igt_helper_process *proc)
{
	int status, ret;

	assert(proc->running);

	ret = kill(proc->pid,
		   proc->use_SIGKILL ? SIGKILL : SIGQUIT);
	assert(ret == 0);
	while (waitpid(proc->pid, &status, 0) == -1 &&
	       errno == EINTR)
		;
	igt_assert(WIFSIGNALED(status) &&
		   WTERMSIG(status) == (proc->use_SIGKILL ? SIGKILL : SIGQUIT));

	proc->running = false;

	helper_process_pids[proc->id] = -1;
	helper_process_count--;
}

void igt_wait_helper(struct igt_helper_process *proc)
{
	int status;

	assert(proc->running);

	while (waitpid(proc->pid, &status, 0) == -1 &&
	       errno == EINTR)
		;
	igt_assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	proc->running = false;

	helper_process_pids[proc->id] = -1;
	helper_process_count--;
}

static void children_exit_handler(int sig)
{
	int ret;

	assert(!test_child);

	for (int nc = 0; nc < num_test_children; nc++) {
		int status = -1;
		ret = kill(test_children[nc], SIGQUIT);
		assert(ret == 0);

		while (waitpid(test_children[nc], &status, 0) == -1 &&
		       errno == EINTR)
			;
	}

	num_test_children = 0;
}

bool __igt_fork(void)
{
	assert(!test_with_subtests || in_subtest);
	assert(!test_child);

	igt_install_exit_handler(children_exit_handler);

	if (num_test_children >= test_children_sz) {
		if (!test_children_sz)
			test_children_sz = 4;
		else
			test_children_sz *= 2;

		test_children = realloc(test_children,
					sizeof(pid_t)*test_children_sz);
		igt_assert(test_children);
	}

	switch (test_children[num_test_children++] = fork()) {
	case -1:
		igt_assert(0);
	case 0:
		test_child = true;
		exit_handler_count = 0;
		reset_helper_process_list();
		oom_adjust_for_doom();

		return true;
	default:
		return false;
	}

}

/**
 * igt_waitchildren - wait for all children forked with igt_fork
 *
 * The magic here is that exit codes from children will be correctly propagated
 */
void igt_waitchildren(void)
{
	assert(!test_child);

	for (int nc = 0; nc < num_test_children; nc++) {
		int status = -1;
		while (waitpid(test_children[nc], &status, 0) == -1 &&
		       errno == EINTR)
			;

		if (status != 0) {
			if (WIFEXITED(status)) {
				printf("child %i failed with exit status %i\n",
				       nc, WEXITSTATUS(status));
				igt_fail(WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
				printf("child %i died with signal %i, %s\n",
				       nc, WTERMSIG(status),
				       strsignal(WTERMSIG(status)));
				igt_fail(99);
			} else {
				printf("Unhandled failure in child %i\n", nc);
				abort();
			}
		}
	}

	num_test_children = 0;
}

static bool env_set(const char *env_var, bool default_value)
{
	char *val;

	val = getenv(env_var);
	if (!val)
		return default_value;

	return atoi(val) != 0;
}

bool igt_run_in_simulation(void)
{
	static int simulation = -1;

	if (simulation == -1)
		simulation = env_set("INTEL_SIMULATION", false);

	return simulation;
}

/**
 * igt_skip_on_simulation - skip tests when INTEL_SIMULATION env war is set
 *
 * Skip the test when running on simulation (and that's relevant only when
 * we're not in the mode where we list the subtests).
 *
 * This function is subtest aware (since it uses igt_skip) and so can be used to
 * skip specific subtests or all subsequent subtests.
 */
void igt_skip_on_simulation(void)
{
	if (igt_only_list_subtests())
		return;

	igt_require(!igt_run_in_simulation());
}

void igt_log(enum igt_log_level level, const char *format, ...)
{
	va_list args;

	assert(format);

	if (igt_log_level > level)
		return;

	va_start(args, format);
	if (level == IGT_LOG_WARN) {
		fflush(stdout);
		vfprintf(stderr, format, args);
	} else
		vprintf(format, args);
	va_end(args);
}

bool drmtest_dump_aub(void)
{
	static int dump_aub = -1;

	if (dump_aub == -1)
		dump_aub = env_set("IGT_DUMP_AUB", false);

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
	assert(trash_bos);

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

#define MAX_SIGNALS		32
#define MAX_EXIT_HANDLERS	5

static struct {
	sighandler_t handler;
	bool installed;
} orig_sig[MAX_SIGNALS];

static igt_exit_handler_t exit_handler_fn[MAX_EXIT_HANDLERS];
static bool exit_handler_disabled;
static sigset_t saved_sig_mask;
static const int handled_signals[] =
	{ SIGINT, SIGHUP, SIGTERM, SIGQUIT, SIGPIPE, SIGABRT, SIGSEGV, SIGBUS };

static int install_sig_handler(int sig_num, sighandler_t handler)
{
	orig_sig[sig_num].handler = signal(sig_num, handler);

	if (orig_sig[sig_num].handler == SIG_ERR)
		return -1;

	orig_sig[sig_num].installed = true;

	return 0;
}

static void restore_sig_handler(int sig_num)
{
	/* Just restore the default so that we properly fall over. */
	signal(sig_num, SIG_DFL);
}

static void restore_all_sig_handler(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(orig_sig); i++)
		restore_sig_handler(i);
}

static void call_exit_handlers(int sig)
{
	int i;

	if (!exit_handler_count) {
		return;
	}

	for (i = exit_handler_count - 1; i >= 0; i--)
		exit_handler_fn[i](sig);

	/* ensure we don't get called twice */
	exit_handler_count = 0;
}

static void igt_atexit_handler(void)
{
	restore_all_sig_handler();

	if (!exit_handler_disabled)
		call_exit_handlers(0);
}

static void fatal_sig_handler(int sig)
{
	pid_t pid, tid;

	restore_all_sig_handler();

	/*
	 * exit_handler_disabled is always false here, since when we set it
	 * we also block signals.
	 */
	call_exit_handlers(sig);

	/* Workaround cached PID and TID races on glibc and Bionic libc. */
	pid = syscall(SYS_getpid);
	tid = syscall(SYS_gettid);

	syscall(SYS_tgkill, pid, tid, sig);
}

/*
 * Set a handler that will be called either when the process calls exit() or
 * returns from the main function, or one of the signals in 'handled_signals'
 * is raised. MAX_EXIT_HANDLERS handlers can be installed, each of which will
 * be called only once, even if a subsequent signal is raised. If the exit
 * handlers are called due to a signal, the signal will be re-raised with the
 * original signal disposition after all handlers returned.
 *
 * The handler will be passed the signal number if called due to a signal, or
 * 0 otherwise.
 */
void igt_install_exit_handler(igt_exit_handler_t fn)
{
	int i;

	for (i = 0; i < exit_handler_count; i++)
		if (exit_handler_fn[i] == fn)
			return;

	igt_assert(exit_handler_count < MAX_EXIT_HANDLERS);

	exit_handler_fn[exit_handler_count] = fn;
	exit_handler_count++;

	if (exit_handler_count > 1)
		return;

	for (i = 0; i < ARRAY_SIZE(handled_signals); i++) {
		if (install_sig_handler(handled_signals[i],
					fatal_sig_handler))
			goto err;
	}

	if (atexit(igt_atexit_handler))
		goto err;

	return;
err:
	restore_all_sig_handler();
	exit_handler_count--;

	igt_assert_f(0, "failed to install the signal handler\n");
}

void igt_disable_exit_handler(void)
{
	sigset_t set;
	int i;

	if (exit_handler_disabled)
		return;

	sigemptyset(&set);
	for (i = 0; i < ARRAY_SIZE(handled_signals); i++)
		sigaddset(&set, handled_signals[i]);

	if (sigprocmask(SIG_BLOCK, &set, &saved_sig_mask)) {
		perror("sigprocmask");
		return;
	}

	exit_handler_disabled = true;
}

void igt_enable_exit_handler(void)
{
	if (!exit_handler_disabled)
		return;

	if (sigprocmask(SIG_SETMASK, &saved_sig_mask, NULL)) {
		perror("sigprocmask");
		return;
	}

	exit_handler_disabled = false;
}

#define PREFAULT_DEBUGFS "/sys/module/i915/parameters/prefault_disable"
static void igt_prefault_control(bool enable)
{
	const char *name = PREFAULT_DEBUGFS;
	int fd;
	char buf[2] = {'Y', 'N'};
	int index;

	fd = open(name, O_RDWR);
	igt_require(fd >= 0);

	if (enable)
		index = 1;
	else
		index = 0;

	igt_require(write(fd, &buf[index], 1) == 1);

	close(fd);
}

static void enable_prefault_at_exit(int sig)
{
	igt_enable_prefault();
}

void igt_disable_prefault(void)
{
	igt_prefault_control(false);

	igt_install_exit_handler(enable_prefault_at_exit);
}

void igt_enable_prefault(void)
{
	igt_prefault_control(true);
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
