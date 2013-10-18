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

#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <pciaccess.h>
#include <math.h>
#include <getopt.h>
#include <stdlib.h>
#include <linux/kd.h>
#include <unistd.h>
#include <sys/wait.h>
#include "drm_fourcc.h"

#include "drmtest.h"
#include "i915_drm.h"
#include "intel_chipset.h"
#include "intel_gpu_tools.h"

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
}

/**
 * drm_get_card() - get an intel card number for use in /dev or /sys
 *
 * @master: -1 not a master, 0 don't care, 1 is the master
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

		if (!is_intel(fd))
			continue;

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

static void quiescent_gpu_at_exit(int sig)
{
	int fd;

	fd = __drm_open_any();
	if (fd >= 0) {
		gem_quiescent_gpu(fd);
		close(fd);
	}
}

int drm_open_any(void)
{
	static int open_count;
	int fd = __drm_open_any();

	igt_require(fd >= 0);

	if (__sync_fetch_and_add(&open_count, 1))
		return fd;

	gem_quiescent_gpu(fd);
	igt_install_exit_handler(quiescent_gpu_at_exit);

	return fd;
}

int __gem_set_tiling(int fd, uint32_t handle, int tiling, int stride)
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

void gem_set_tiling(int fd, uint32_t handle, int tiling, int stride)
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

static int exit_handler_count;

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
			else
				igt_install_exit_handler(check_igt_exit);
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

out:
	return ret;
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

	if (skip_subtests_henceforth) {
		printf("Subtest %s: %s\n", subtest_name,
		       skip_subtests_henceforth == SKIP ?
		       "SKIP" : "FAIL");
		return false;
	}

	if (!run_single_subtest) {
		return (in_subtest = subtest_name);
	} else {
		if (strcmp(subtest_name, run_single_subtest) == 0)
			return (in_subtest = subtest_name);

		return false;
	}
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
		if (in_fixture)
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

	if (f) {
		static char *buf;

		/* igt_skip never returns, so try to not leak too badly. */
		if (buf)
			free(buf);

		va_start(args, f);
		vasprintf(&buf, f, args);
		va_end(args);

		igt_skip("Test requirement not met in function %s, file %s:%i:\n"
			 "Test requirement: (%s)\n%s",
			 func, file, line, check, buf);
	} else {
		igt_skip("Test requirement not met in function %s, file %s:%i:\n"
			 "Test requirement: (%s)\n",
			 func, file, line, check);
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
		strncmp (basename (buf), "gdb", 3) == 0);
}

void __igt_fail_assert(int exitcode, const char *file,
		       const int line, const char *func, const char *assertion,
		       const char *f, ...)
{
	va_list args;

	printf("Test assertion failure function %s, file %s:%i:\n"
	       "Failed assertion: %s\n",
	       func, file, line, assertion);

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
	if (igt_only_list_subtests())
		exit(0);

	if (!test_with_subtests)
		exit(0);

	/* Calling this without calling one of the above is a failure */
	assert(skipped_one || succeeded_one || failed_one);
	igt_exit_called = true;

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
		int status;

		if (pid != -1) {
			/* Someone forgot to fill up the array? */
			assert(pid != 0);

			assert(kill(pid, SIGQUIT) == 0);
			while (waitpid(pid, &status, 0) == -1 &&
			       errno == -EINTR)
				;
			helper_process_count--;
		}
	}

	assert(helper_process_count == 0);
}

bool __igt_fork_helper(struct igt_helper_process *proc)
{
	pid_t pid;
	sighandler_t oldsig;
	int id;

	assert(!proc->running);
	assert(helper_process_count < ARRAY_SIZE(helper_process_pids));

	for (id = 0; helper_process_pids[id] != -1; id++)
		;

	igt_install_exit_handler(fork_helper_exit_handler);

	/*
	 * XXX: There's a race between fork and the subsequent kill in
	 * igt_stop_signal_helper if we don't ovewrite the SIGQUIT handler. Note
	 * that inserting sufficient amounts of printf or other delays makes
	 * this unnecessary.
	 */
	oldsig = signal(SIGQUIT, SIG_DFL);
	switch (pid = fork()) {
	case -1:
		igt_assert(0);
	case 0:
		exit_handler_count = 0;
		reset_helper_process_list();

		return true;
	default:
		signal(SIGQUIT, oldsig);

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
	int status;

	assert(proc->running);

	assert(kill(proc->pid,
		    proc->use_SIGKILL ? SIGKILL : SIGQUIT) == 0);
	while (waitpid(proc->pid, &status, 0) == -1 &&
	       errno == -EINTR)
		;
	igt_assert(WIFSIGNALED(status) &&
		   WTERMSIG(status) == (proc->use_SIGKILL ? SIGKILL : SIGQUIT));

	proc->running = false;

	helper_process_pids[proc->id] = -1;
	helper_process_count--;
}

static void children_exit_handler(int sig)
{
	assert(!test_child);

	for (int nc = 0; nc < num_test_children; nc++) {
		int status = -1;
		assert(kill(test_children[nc], SIGQUIT) == 0);

		while (waitpid(test_children[nc], &status, 0) == -1 &&
		       errno == -EINTR)
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
		       errno == -EINTR)
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

/* helpers to create nice-looking framebuffers */
static int create_bo_for_fb(int fd, int width, int height, int bpp,
			    bool tiled, uint32_t *gem_handle_ret,
			    unsigned *size_ret, unsigned *stride_ret)
{
	uint32_t gem_handle;
	int size;
	unsigned stride;

	if (tiled) {
		int v;

		/* Round the tiling up to the next power-of-two and the
		 * region up to the next pot fence size so that this works
		 * on all generations.
		 *
		 * This can still fail if the framebuffer is too large to
		 * be tiled. But then that failure is expected.
		 */

		v = width * bpp / 8;
		for (stride = 512; stride < v; stride *= 2)
			;

		v = stride * height;
		for (size = 1024*1024; size < v; size *= 2)
			;
	} else {
		/* Scan-out has a 64 byte alignment restriction */
		stride = (width * (bpp / 8) + 63) & ~63;
		size = stride * height;
	}

	gem_handle = gem_create(fd, size);

	if (tiled)
		gem_set_tiling(fd, gem_handle, I915_TILING_X, stride);

	*stride_ret = stride;
	*size_ret = size;
	*gem_handle_ret = gem_handle;

	return 0;
}

void kmstest_paint_color(cairo_t *cr, int x, int y, int w, int h,
			 double r, double g, double b)
{
	cairo_rectangle(cr, x, y, w, h);
	cairo_set_source_rgb(cr, r, g, b);
	cairo_fill(cr);
}

void
kmstest_paint_color_gradient(cairo_t *cr, int x, int y, int w, int h,
		     int r, int g, int b)
{
	cairo_pattern_t *pat;

	pat = cairo_pattern_create_linear(x, y, x + w, y + h);
	cairo_pattern_add_color_stop_rgba(pat, 1, 0, 0, 0, 1);
	cairo_pattern_add_color_stop_rgba(pat, 0, r, g, b, 1);

	cairo_rectangle(cr, x, y, w, h);
	cairo_set_source(cr, pat);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);
}

static void
paint_test_patterns(cairo_t *cr, int width, int height)
{
	double gr_height, gr_width;
	int x, y;

	y = height * 0.10;
	gr_width = width * 0.75;
	gr_height = height * 0.08;
	x = (width / 2) - (gr_width / 2);

	kmstest_paint_color_gradient(cr, x, y, gr_width, gr_height, 1, 0, 0);

	y += gr_height;
	kmstest_paint_color_gradient(cr, x, y, gr_width, gr_height, 0, 1, 0);

	y += gr_height;
	kmstest_paint_color_gradient(cr, x, y, gr_width, gr_height, 0, 0, 1);

	y += gr_height;
	kmstest_paint_color_gradient(cr, x, y, gr_width, gr_height, 1, 1, 1);
}

int kmstest_cairo_printf_line(cairo_t *cr, enum kmstest_text_align align,
				double yspacing, const char *fmt, ...)
{
	double x, y, xofs, yofs;
	cairo_text_extents_t extents;
	char *text;
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vasprintf(&text, fmt, ap);
	assert(ret >= 0);
	va_end(ap);

	cairo_text_extents(cr, text, &extents);

	xofs = yofs = 0;
	if (align & align_right)
		xofs = -extents.width;
	else if (align & align_hcenter)
		xofs = -extents.width / 2;

	if (align & align_top)
		yofs = extents.height;
	else if (align & align_vcenter)
		yofs = extents.height / 2;

	cairo_get_current_point(cr, &x, &y);
	if (xofs || yofs)
		cairo_rel_move_to(cr, xofs, yofs);

	cairo_text_path(cr, text);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_fill(cr);

	cairo_move_to(cr, x, y + extents.height + yspacing);

	free(text);

	return extents.width;
}

static void
paint_marker(cairo_t *cr, int x, int y)
{
	enum kmstest_text_align align;
	int xoff, yoff;

	cairo_move_to(cr, x, y - 20);
	cairo_line_to(cr, x, y + 20);
	cairo_move_to(cr, x - 20, y);
	cairo_line_to(cr, x + 20, y);
	cairo_new_sub_path(cr);
	cairo_arc(cr, x, y, 10, 0, M_PI * 2);
	cairo_set_line_width(cr, 4);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);

	xoff = x ? -20 : 20;
	align = x ? align_right : align_left;

	yoff = y ? -20 : 20;
	align |= y ? align_bottom : align_top;

	cairo_move_to(cr, x + xoff, y + yoff);
	cairo_set_font_size(cr, 18);
	kmstest_cairo_printf_line(cr, align, 0, "(%d, %d)", x, y);
}

void kmstest_paint_test_pattern(cairo_t *cr, int width, int height)
{
	paint_test_patterns(cr, width, height);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	/* Paint corner markers */
	paint_marker(cr, 0, 0);
	paint_marker(cr, width, 0);
	paint_marker(cr, 0, height);
	paint_marker(cr, width, height);

	assert(!cairo_status(cr));
}

void kmstest_paint_image(cairo_t *cr, const char *filename,
			 int dst_x, int dst_y, int dst_width, int dst_height)
{
	cairo_surface_t *image;
	int img_width, img_height;
	double scale_x, scale_y;

	image = cairo_image_surface_create_from_png(filename);
	assert(cairo_surface_status(image) == CAIRO_STATUS_SUCCESS);

	img_width = cairo_image_surface_get_width(image);
	img_height = cairo_image_surface_get_height(image);

	scale_x = (double)dst_width / img_width;
	scale_y = (double)dst_height / img_height;

	cairo_save(cr);

	cairo_translate(cr, dst_x, dst_y);
	cairo_scale(cr, scale_x, scale_y);
	cairo_set_source_surface(cr, image, 0, 0);
	cairo_paint(cr);

	cairo_surface_destroy(image);

	cairo_restore(cr);
}

#define DF(did, cid, _bpp, _depth)	\
	{ DRM_FORMAT_##did, CAIRO_FORMAT_##cid, # did, _bpp, _depth }
static struct format_desc_struct {
	uint32_t drm_id;
	cairo_format_t cairo_id;
	const char *name;
	int bpp;
	int depth;
} format_desc[] = {
	DF(RGB565,	RGB16_565,	16, 16),
	DF(RGB888,	INVALID,	24, 24),
	DF(XRGB8888,	RGB24,		32, 24),
	DF(XRGB2101010,	RGB30,		32, 30),
	DF(ARGB8888,	ARGB32,		32, 32),
};
#undef DF

#define for_each_format(f)	\
	for (f = format_desc; f - format_desc < ARRAY_SIZE(format_desc); f++)

static uint32_t bpp_depth_to_drm_format(int bpp, int depth)
{
	struct format_desc_struct *f;

	for_each_format(f)
		if (f->bpp == bpp && f->depth == depth)
			return f->drm_id;

	abort();
}

/* Return fb_id on success, 0 on error */
unsigned int kmstest_create_fb(int fd, int width, int height, int bpp,
			       int depth, bool tiled, struct kmstest_fb *fb)
{
	memset(fb, 0, sizeof(*fb));

	if (create_bo_for_fb(fd, width, height, bpp, tiled, &fb->gem_handle,
			       &fb->size, &fb->stride) < 0)
		return 0;

	if (drmModeAddFB(fd, width, height, depth, bpp, fb->stride,
			       fb->gem_handle, &fb->fb_id) < 0) {
		gem_close(fd, fb->gem_handle);

		return 0;
	}

	fb->width = width;
	fb->height = height;
	fb->tiling = tiled;
	fb->drm_format = bpp_depth_to_drm_format(bpp, depth);

	return fb->fb_id;
}

uint32_t drm_format_to_bpp(uint32_t drm_format)
{
	struct format_desc_struct *f;

	for_each_format(f)
		if (f->drm_id == drm_format)
			return f->bpp;

	abort();
}

unsigned int kmstest_create_fb2(int fd, int width, int height, uint32_t format,
			        bool tiled, struct kmstest_fb *fb)
{
	uint32_t handles[4];
	uint32_t pitches[4];
	uint32_t offsets[4];
	uint32_t fb_id;
	int bpp;
	int ret;

	memset(fb, 0, sizeof(*fb));

	bpp = drm_format_to_bpp(format);
	ret = create_bo_for_fb(fd, width, height, bpp, tiled, &fb->gem_handle,
			      &fb->size, &fb->stride);
	if (ret < 0)
		return ret;

	memset(handles, 0, sizeof(handles));
	handles[0] = fb->gem_handle;
	memset(pitches, 0, sizeof(pitches));
	pitches[0] = fb->stride;
	memset(offsets, 0, sizeof(offsets));
	if (drmModeAddFB2(fd, width, height, format, handles, pitches,
			  offsets, &fb_id, 0) < 0) {
		gem_close(fd, fb->gem_handle);

		return 0;
	}

	fb->width = width;
	fb->height = height;
	fb->tiling = tiled;
	fb->drm_format = format;
	fb->fb_id = fb_id;

	return fb_id;
}

static cairo_format_t drm_format_to_cairo(uint32_t drm_format)
{
	struct format_desc_struct *f;

	for_each_format(f)
		if (f->drm_id == drm_format)
			return f->cairo_id;

	abort();
}

static cairo_surface_t *create_image_surface(int fd, struct kmstest_fb *fb)
{
	cairo_surface_t *surface;
	cairo_format_t cformat;
	void *fb_ptr;

	cformat = drm_format_to_cairo(fb->drm_format);
	fb_ptr = gem_mmap(fd, fb->gem_handle, fb->size, PROT_READ | PROT_WRITE);
	surface = cairo_image_surface_create_for_data((unsigned char *)fb_ptr,
						   cformat, fb->width,
						   fb->height, fb->stride);
	assert(surface);

	return surface;
}

static cairo_t *create_cairo_ctx(int fd, struct kmstest_fb *fb)
{
	cairo_t *cr;
	cairo_surface_t *surface;

	surface = create_image_surface(fd, fb);
	cr = cairo_create(surface);
	cairo_surface_destroy(surface);

	return cr;
}

void kmstest_write_fb(int fd, struct kmstest_fb *fb, const char *filename)
{
	cairo_surface_t *surface;
	cairo_status_t status;

	surface = create_image_surface(fd, fb);
	status = cairo_surface_write_to_png(surface, filename);
	assert(status == CAIRO_STATUS_SUCCESS);
	cairo_surface_destroy(surface);
}

cairo_t *kmstest_get_cairo_ctx(int fd, struct kmstest_fb *fb)
{

	if (!fb->cairo_ctx)
		fb->cairo_ctx = create_cairo_ctx(fd, fb);

	gem_set_domain(fd, fb->gem_handle, I915_GEM_DOMAIN_CPU,
		       I915_GEM_DOMAIN_CPU);

	return fb->cairo_ctx;
}

void kmstest_remove_fb(int fd, struct kmstest_fb *fb)
{
	if (fb->cairo_ctx)
		cairo_destroy(fb->cairo_ctx);
	do_or_die(drmModeRmFB(fd, fb->fb_id));
	gem_close(fd, fb->gem_handle);
}

const char *kmstest_format_str(uint32_t drm_format)
{
	struct format_desc_struct *f;

	for_each_format(f)
		if (f->drm_id == drm_format)
			return f->name;

	return "invalid";
}

const char *kmstest_pipe_str(int pipe)
{
	const char *str[] = { "A", "B", "C" };

	if (pipe > 2)
		return "invalid";

	return str[pipe];
}

void kmstest_get_all_formats(const uint32_t **formats, int *format_count)
{
	static uint32_t *drm_formats;

	if (!drm_formats) {
		struct format_desc_struct *f;
		uint32_t *format;

		drm_formats = calloc(ARRAY_SIZE(format_desc),
				     sizeof(*drm_formats));
		format = &drm_formats[0];
		for_each_format(f)
			*format++ = f->drm_id;
	}

	*formats = drm_formats;
	*format_count = ARRAY_SIZE(format_desc);
}

struct type_name {
	int type;
	const char *name;
};

#define type_name_fn(res) \
const char * kmstest_##res##_str(int type) {		\
	unsigned int i;					\
	for (i = 0; i < ARRAY_SIZE(res##_names); i++) { \
		if (res##_names[i].type == type)	\
			return res##_names[i].name;	\
	}						\
	return "(invalid)";				\
}

struct type_name encoder_type_names[] = {
	{ DRM_MODE_ENCODER_NONE, "none" },
	{ DRM_MODE_ENCODER_DAC, "DAC" },
	{ DRM_MODE_ENCODER_TMDS, "TMDS" },
	{ DRM_MODE_ENCODER_LVDS, "LVDS" },
	{ DRM_MODE_ENCODER_TVDAC, "TVDAC" },
};

type_name_fn(encoder_type)

struct type_name connector_status_names[] = {
	{ DRM_MODE_CONNECTED, "connected" },
	{ DRM_MODE_DISCONNECTED, "disconnected" },
	{ DRM_MODE_UNKNOWNCONNECTION, "unknown" },
};

type_name_fn(connector_status)

struct type_name connector_type_names[] = {
	{ DRM_MODE_CONNECTOR_Unknown, "unknown" },
	{ DRM_MODE_CONNECTOR_VGA, "VGA" },
	{ DRM_MODE_CONNECTOR_DVII, "DVI-I" },
	{ DRM_MODE_CONNECTOR_DVID, "DVI-D" },
	{ DRM_MODE_CONNECTOR_DVIA, "DVI-A" },
	{ DRM_MODE_CONNECTOR_Composite, "composite" },
	{ DRM_MODE_CONNECTOR_SVIDEO, "s-video" },
	{ DRM_MODE_CONNECTOR_LVDS, "LVDS" },
	{ DRM_MODE_CONNECTOR_Component, "component" },
	{ DRM_MODE_CONNECTOR_9PinDIN, "9-pin DIN" },
	{ DRM_MODE_CONNECTOR_DisplayPort, "DP" },
	{ DRM_MODE_CONNECTOR_HDMIA, "HDMI-A" },
	{ DRM_MODE_CONNECTOR_HDMIB, "HDMI-B" },
	{ DRM_MODE_CONNECTOR_TV, "TV" },
	{ DRM_MODE_CONNECTOR_eDP, "eDP" },
};

type_name_fn(connector_type)

static const char *mode_stereo_name(const drmModeModeInfo *mode)
{
	switch (mode->flags & DRM_MODE_FLAG_3D_MASK) {
	case DRM_MODE_FLAG_3D_FRAME_PACKING:
		return "FP";
	case DRM_MODE_FLAG_3D_FIELD_ALTERNATIVE:
		return "FA";
	case DRM_MODE_FLAG_3D_LINE_ALTERNATIVE:
		return "LA";
	case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_FULL:
		return "SBSF";
	case DRM_MODE_FLAG_3D_L_DEPTH:
		return "LD";
	case DRM_MODE_FLAG_3D_L_DEPTH_GFX_GFX_DEPTH:
		return "LDGFX";
	case DRM_MODE_FLAG_3D_TOP_AND_BOTTOM:
		return "TB";
	case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_HALF:
		return "SBSH";
	default:
		return NULL;
	}
}

void kmstest_dump_mode(drmModeModeInfo *mode)
{
	const char *stereo = mode_stereo_name(mode);

	printf("  %s %d %d %d %d %d %d %d %d %d 0x%x 0x%x %d%s%s%s\n",
	       mode->name,
	       mode->vrefresh,
	       mode->hdisplay,
	       mode->hsync_start,
	       mode->hsync_end,
	       mode->htotal,
	       mode->vdisplay,
	       mode->vsync_start,
	       mode->vsync_end,
	       mode->vtotal,
	       mode->flags,
	       mode->type,
	       mode->clock,
	       stereo ? " (3D:" : "",
	       stereo ? stereo : "",
	       stereo ? ")" : "");
	fflush(stdout);
}

int kmstest_get_pipe_from_crtc_id(int fd, int crtc_id)
{
	struct drm_i915_get_pipe_from_crtc_id pfci;
	int ret;

	memset(&pfci, 0, sizeof(pfci));
	pfci.crtc_id = crtc_id;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GET_PIPE_FROM_CRTC_ID, &pfci);
	igt_assert(ret == 0);

	return pfci.pipe;
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
	restore_all_sig_handler();

	/*
	 * exit_handler_disabled is always false here, since when we set it
	 * we also block signals.
	 */
	call_exit_handlers(sig);

	raise(sig);
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
int igt_install_exit_handler(igt_exit_handler_t fn)
{
	int i;

	for (i = 0; i < exit_handler_count; i++)
		if (exit_handler_fn[i] == fn)
			return 0;

	if (exit_handler_count == MAX_EXIT_HANDLERS)
		return -1;

	exit_handler_fn[exit_handler_count] = fn;
	exit_handler_count++;

	if (exit_handler_count > 1)
		return 0;

	for (i = 0; i < ARRAY_SIZE(handled_signals); i++) {
		if (install_sig_handler(handled_signals[i],
					fatal_sig_handler))
			goto err;
	}

	if (atexit(igt_atexit_handler))
		goto err;

	return 0;
err:
	restore_all_sig_handler();
	exit_handler_count--;

	return -1;
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

static signed long set_vt_mode(unsigned long mode)
{
	int fd;
	unsigned long prev_mode;

	fd = open("/dev/tty0", O_RDONLY);
	if (fd < 0)
		return -errno;

	prev_mode = 0;
	if (drmIoctl(fd, KDGETMODE, &prev_mode))
		goto err;
	if (drmIoctl(fd, KDSETMODE, (void *)mode))
		goto err;

	close(fd);

	return prev_mode;
err:
	close(fd);

	return -errno;
}

static unsigned long orig_vt_mode = -1UL;

static void restore_vt_mode_at_exit(int sig)
{
	if (orig_vt_mode != -1UL)
		set_vt_mode(orig_vt_mode);
}

/*
 * Set the VT to graphics mode and install an exit handler to restore the
 * original mode.
 */

int igt_set_vt_graphics_mode(void)
{
	if (igt_install_exit_handler(restore_vt_mode_at_exit))
		return -1;

	igt_disable_exit_handler();
	orig_vt_mode = set_vt_mode(KD_GRAPHICS);
	if (orig_vt_mode < 0)
		orig_vt_mode = -1UL;
	igt_enable_exit_handler();

	return orig_vt_mode < 0 ? -1 : 0;
}

int kmstest_get_connector_default_mode(int drm_fd, drmModeConnector *connector,
				      drmModeModeInfo *mode)
{
	drmModeRes *resources;
	int i;

	resources = drmModeGetResources(drm_fd);
	if (!resources) {
		perror("drmModeGetResources failed");

		return -1;
	}

	if (!connector->count_modes) {
		fprintf(stderr, "no modes for connector %d\n",
			connector->connector_id);
		drmModeFreeResources(resources);

		return -1;
	}

	for (i = 0; i < connector->count_modes; i++) {
		if (i == 0 ||
		    connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
			*mode = connector->modes[i];
			if (mode->type & DRM_MODE_TYPE_PREFERRED)
				break;
		}
	}

	drmModeFreeResources(resources);

	return 0;
}

int kmstest_get_connector_config(int drm_fd, uint32_t connector_id,
				 unsigned long crtc_idx_mask,
				 struct kmstest_connector_config *config)
{
	drmModeRes *resources;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
	int i, j;

	resources = drmModeGetResources(drm_fd);
	if (!resources) {
		perror("drmModeGetResources failed");
		goto err1;
	}

	/* First, find the connector & mode */
	connector = drmModeGetConnector(drm_fd, connector_id);
	if (!connector)
		goto err2;

	if (connector->connection != DRM_MODE_CONNECTED)
		goto err3;

	if (!connector->count_modes) {
		fprintf(stderr, "connector %d has no modes\n", connector_id);
		goto err3;
	}

	if (connector->connector_id != connector_id) {
		fprintf(stderr, "connector id doesn't match (%d != %d)\n",
			connector->connector_id, connector_id);
		goto err3;
	}

	/*
	 * Find given CRTC if crtc_id != 0 or else the first CRTC not in use.
	 * In both cases find the first compatible encoder and skip the CRTC
	 * if there is non such.
	 */
	encoder = NULL;		/* suppress GCC warning */
	for (i = 0; i < resources->count_crtcs; i++) {
		if (!resources->crtcs[i] || !(crtc_idx_mask & (1 << i)))
			continue;

		/* Now get a compatible encoder */
		for (j = 0; j < connector->count_encoders; j++) {
			encoder = drmModeGetEncoder(drm_fd,
						    connector->encoders[j]);

			if (!encoder) {
				fprintf(stderr, "could not get encoder %d: %s\n",
					resources->encoders[j], strerror(errno));

				continue;
			}

			if (encoder->possible_crtcs & (1 << i))
				goto found;

			drmModeFreeEncoder(encoder);
		}
	}

	fprintf(stderr,
		"no crtc with a compatible encoder (crtc_idx_mask %08lx)\n",
		crtc_idx_mask);
	goto err3;

found:
	if (kmstest_get_connector_default_mode(drm_fd, connector,
				       &config->default_mode) < 0)
		goto err4;

	config->connector = connector;
	config->encoder = encoder;
	config->crtc = drmModeGetCrtc(drm_fd, resources->crtcs[i]);
	config->crtc_idx = i;
	config->pipe = kmstest_get_pipe_from_crtc_id(drm_fd,
						     config->crtc->crtc_id);

	drmModeFreeResources(resources);

	return 0;
err4:
	drmModeFreeEncoder(encoder);
err3:
	drmModeFreeConnector(connector);
err2:
	drmModeFreeResources(resources);
err1:
	return -1;
}

void kmstest_free_connector_config(struct kmstest_connector_config *config)
{
	drmModeFreeCrtc(config->crtc);
	drmModeFreeEncoder(config->encoder);
	drmModeFreeConnector(config->connector);
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
	igt_install_exit_handler(enable_prefault_at_exit);

	igt_prefault_control(false);
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
