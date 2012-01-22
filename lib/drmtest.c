/*
 * Copyright © 2007, 2011 Intel Corporation
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

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <pciaccess.h>

#include "drmtest.h"
#include "i915_drm.h"
#include "intel_chipset.h"
#include "intel_gpu_tools.h"

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

/* Ensure the gpu is idle by launching a nop execbuf and stalling for it. */
void gem_quiescent_gpu(int fd)
{
	uint32_t batch[2] = {MI_BATCH_BUFFER_END, 0};
	uint32_t handle;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];
	int ret = 0;

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
	execbuf.rsvd1 = 0;
	execbuf.rsvd2 = 0;

	ret = drmIoctl(fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER2,
		       &execbuf);
	assert(ret == 0);

	gem_sync(fd, handle);
}

/** Open the first DRM device we can find, searching up to 16 device nodes */
int drm_open_any(void)
{
	char name[20];
	int i, fd;

	for (i = 0; i < 16; i++) {
		sprintf(name, "/dev/dri/card%d", i);
		fd = open(name, O_RDWR);
		if (fd == -1)
			continue;

		if (is_intel(fd)) {
			gem_quiescent_gpu(fd);
			return fd;
		}

		close(fd);
	}
	fprintf(stderr, "failed to open any drm device. retry as root?\n");
	abort();
}


/**
 * Open the first DRM device we can find where we end up being the master.
 */
int drm_open_any_master(void)
{
	char name[20];
	int i, fd;

	for (i = 0; i < 16; i++) {
		drm_client_t client;
		int ret;

		sprintf(name, "/dev/dri/card%d", i);
		fd = open(name, O_RDWR);
		if (fd == -1)
			continue;

		if (!is_intel(fd)) {
			close(fd);
			continue;
		}

		/* Check that we're the only opener and authed. */
		client.idx = 0;
		ret = ioctl(fd, DRM_IOCTL_GET_CLIENT, &client);
		assert (ret == 0);
		if (!client.auth) {
			close(fd);
			continue;
		}
		client.idx = 1;
		ret = ioctl(fd, DRM_IOCTL_GET_CLIENT, &client);
		if (ret != -1 || errno != EINVAL) {
			close(fd);
			continue;
		}
		return fd;
	}
	fprintf(stderr, "Couldn't find an un-controlled DRM device\n");
	abort();
}

void gem_set_tiling(int fd, uint32_t handle, int tiling, int stride)
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
	assert(ret == 0);
	assert(st.tiling_mode == tiling);
}

void gem_close(int fd, uint32_t handle)
{
	struct drm_gem_close close_bo;
	int ret;

	close_bo.handle = handle;
	ret = drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &close_bo);
	assert(ret == 0);
}

void gem_write(int fd, uint32_t handle, uint32_t offset, const void *buf, uint32_t size)
{
	struct drm_i915_gem_pwrite gem_pwrite;
	int ret;

	gem_pwrite.handle = handle;
	gem_pwrite.offset = offset;
	gem_pwrite.size = size;
	gem_pwrite.data_ptr = (uintptr_t)buf;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite);
	assert(ret == 0);
}

void gem_read(int fd, uint32_t handle, uint32_t offset, void *buf, uint32_t length)
{
	struct drm_i915_gem_pread gem_pread;
	int ret;

	gem_pread.handle = handle;
	gem_pread.offset = offset;
	gem_pread.size = length;
	gem_pread.data_ptr = (uintptr_t)buf;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_PREAD, &gem_pread);
	assert(ret == 0);
}

void gem_set_domain(int fd, uint32_t handle,
		    uint32_t read_domains, uint32_t write_domain)
{
	struct drm_i915_gem_set_domain set_domain;
	int ret;

	set_domain.handle = handle;
	set_domain.read_domains = read_domains;
	set_domain.write_domain = write_domain;

	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_SET_DOMAIN, &set_domain);
	assert(ret == 0);
}

void gem_sync(int fd, uint32_t handle)
{
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
}

uint32_t gem_create(int fd, int size)
{
	struct drm_i915_gem_create create;
	int ret;

	create.handle = 0;
	create.size = size;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);
	assert(ret == 0);
	assert(create.handle);

	return create.handle;
}

void *gem_mmap(int fd, uint32_t handle, int size, int prot)
{
	struct drm_i915_gem_mmap_gtt mmap_arg;
	void *ptr;

	mmap_arg.handle = handle;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_arg))
		return NULL;

	ptr = mmap(0, size, prot, MAP_SHARED, fd, mmap_arg.offset);
	if (ptr == MAP_FAILED)
		ptr = NULL;

	return ptr;
}

uint64_t gem_aperture_size(int fd)
{
	struct drm_i915_gem_get_aperture aperture;

	aperture.aper_size = 256*1024*1024;
	(void)drmIoctl(fd, DRM_IOCTL_I915_GEM_GET_APERTURE, &aperture);
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

/* signal interrupt helpers */
static pid_t signal_helper = -1;
long long int sig_stat;
static void signal_helper_process(pid_t pid)
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

void drmtest_fork_signal_helper(void)
{
	pid_t pid;

	signal(SIGUSR1, sig_handler);
	pid = fork();
	if (pid == 0) {
		signal_helper_process(getppid());
		return;
	}

	signal_helper = pid;
}

void drmtest_stop_signal_helper(void)
{
	if (signal_helper != -1)
		kill(signal_helper, SIGQUIT);

	if (sig_stat)
		fprintf(stderr, "signal handler called %llu times\n", sig_stat);

	signal_helper = -1;
}

/* other helpers */
void drmtest_exchange_int(void *array, unsigned i, unsigned j)
{
	int *int_arr, tmp;
	int_arr = array;

	tmp = int_arr[i];
	int_arr[i] = int_arr[j];
	int_arr[j] = tmp;
}

void drmtest_permute_array(void *array, unsigned size,
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

/* mappable aperture trasher helper */
drm_intel_bo **trash_bos;
int num_trash_bos;

void drmtest_init_aperture_trashers(drm_intel_bufmgr *bufmgr)
{
	int i;

	num_trash_bos = gem_mappable_aperture_size() / (1024*1024);

	trash_bos = malloc(num_trash_bos * sizeof(drm_intel_bo *));
	assert(trash_bos);

	for (i = 0; i < num_trash_bos; i++)
		trash_bos[i] = drm_intel_bo_alloc(bufmgr, "trash bo", 1024*1024, 4096);
}

void drmtest_trash_aperture(void)
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

void drmtest_cleanup_aperture_trashers(void)
{
	int i;

	for (i = 0; i < num_trash_bos; i++)
		drm_intel_bo_unreference(trash_bos[i]);

	free(trash_bos);
}
