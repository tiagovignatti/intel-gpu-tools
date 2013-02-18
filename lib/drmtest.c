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
	assert(ret == 0);

	bo = drm_intel_bo_gem_create_from_name(bufmgr, name, flink.name);
	assert(bo);

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

	gem_sync(fd, handle);
}

static bool is_master(int fd)
{
	drm_client_t client;
	int ret;

	/* Check that we're the only opener and authed. */
	client.idx = 0;
	ret = ioctl(fd, DRM_IOCTL_GET_CLIENT, &client);
	assert (ret == 0);
	if (!client.auth) {
		return 0;
	}
	client.idx = 1;
	ret = ioctl(fd, DRM_IOCTL_GET_CLIENT, &client);
	if (ret != -1 || errno != EINVAL) {
		return 0;
	}
	return 1;
}

/**
 * drm_get_card() - get an intel card number for use in /dev or /sys
 *
 * @master: -1 not a master, 0 don't care, 1 is the master
 *
 * returns -1 on error
 */
int drm_get_card(int master)
{
	char *name;
	int i, fd;

	for (i = 0; i < 16; i++) {
		int ret;

		ret = asprintf(&name, "/dev/dri/card%u", i);
		if (ret == -1)
			return -1;
		fd = open(name, O_RDWR);
		free(name);

		if (fd == -1)
			continue;

		if (is_intel(fd) && master == 0) {
			close(fd);
			break;
		}

		if (master == 1 && is_master(fd)) {
			close(fd);
			break;
		}

		if (master == -1 && !is_master(fd)) {
			close(fd);
			break;
		}

		close(fd);
	}

	return i;
}

/** Open the first DRM device we can find, searching up to 16 device nodes */
int drm_open_any(void)
{
	char *name;
	int ret, fd;

	ret = asprintf(&name, "/dev/dri/card%d", drm_get_card(0));
	if (ret == -1)
		return -1;

	fd = open(name, O_RDWR);
	free(name);

	if (fd == -1)
		fprintf(stderr, "failed to open any drm device. retry as root?\n");

	assert(is_intel(fd));

	return fd;
}

/**
 * Open the first DRM device we can find where we end up being the master.
 */
int drm_open_any_master(void)
{
	char *name;
	int ret, fd;

	ret = asprintf(&name, "/dev/dri/card%d", drm_get_card(1));
	if (ret == -1)
		return -1;

	fd = open(name, O_RDWR);
	free(name);
	if (fd == -1)
		fprintf(stderr, "Couldn't find an un-controlled DRM device\n");

	assert(is_intel(fd));

	return fd;
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

struct local_drm_i915_gem_cacheing {
	uint32_t handle;
	uint32_t cacheing;
};

#define LOCAL_DRM_I915_GEM_SET_CACHEING    0x2f
#define LOCAL_DRM_I915_GEM_GET_CACHEING    0x30
#define LOCAL_DRM_IOCTL_I915_GEM_SET_CACHEING \
	DRM_IOW(DRM_COMMAND_BASE + LOCAL_DRM_I915_GEM_SET_CACHEING, struct local_drm_i915_gem_cacheing)
#define LOCAL_DRM_IOCTL_I915_GEM_GET_CACHEING \
	DRM_IOWR(DRM_COMMAND_BASE + LOCAL_DRM_I915_GEM_GET_CACHEING, struct local_drm_i915_gem_cacheing)

int gem_has_cacheing(int fd)
{
	struct local_drm_i915_gem_cacheing arg;
	int ret;

	arg.handle = gem_create(fd, 4096);
	if (arg.handle == 0)
		return 0;

	arg.cacheing = 0;
	ret = ioctl(fd, LOCAL_DRM_IOCTL_I915_GEM_SET_CACHEING, &arg);
	gem_close(fd, arg.handle);

	return ret == 0;
}

void gem_set_cacheing(int fd, uint32_t handle, int cacheing)
{
	struct local_drm_i915_gem_cacheing arg;
	int ret;

	arg.handle = handle;
	arg.cacheing = cacheing;
	ret = ioctl(fd, LOCAL_DRM_IOCTL_I915_GEM_SET_CACHEING, &arg);
	assert(ret == 0);
}

int gem_get_cacheing(int fd, uint32_t handle)
{
	struct local_drm_i915_gem_cacheing arg;
	int ret;

	arg.handle = handle;
	arg.cacheing = 0;
	ret = ioctl(fd, LOCAL_DRM_IOCTL_I915_GEM_GET_CACHEING, &arg);
	assert(ret == 0);

	return arg.cacheing;
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
	assert(create.handle);

	return create.handle;
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

/* signal interrupt helpers */
static pid_t signal_helper = -1;
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

/* subtests helpers */
static bool list_subtests = false;
static char *run_single_subtest = NULL;

void drmtest_subtest_init(int argc, char **argv)
{
	int c, option_index = 0;
	static struct option long_options[] = {
		{"list-subtests", 0, 0, 'l'},
		{"run-subtest", 1, 0, 'r'},
		{NULL, 0, 0, 0,}
	};

	/* supress getopt errors about unknown options */
	opterr = 0;
	while((c = getopt_long(argc, argv, "",
			       long_options, &option_index)) != -1) {
		switch(c) {
		case 'l':
			list_subtests = true;
			goto out;
		case 'r':
			run_single_subtest = strdup(optarg);
			goto out;
		}
	}

out:
	/* reset opt parsing */
	optind = 1;
}

/*
 * Note: Testcases which use these helpers MUST NOT output anything to stdout
 * outside of places protected by drmtest_run_subtest checks - the piglit
 * runner adds every line to the subtest list.
 */
bool drmtest_run_subtest(const char *subtest_name)
{
	if (list_subtests) {
		printf("%s\n", subtest_name);
		return false;
	}

	if (!run_single_subtest) {
		return true;
	} else {
		if (strcmp(subtest_name, run_single_subtest) == 0)
			return true;

		return false;
	}
}

bool drmtest_only_list_subtests(void)
{
	return list_subtests;
}

static bool env_set(const char *env_var)
{
	char *val;

	val = getenv(env_var);
	if (!val)
		return false;

	return atoi(val) != 0;
}

bool drmtest_run_quick(void)
{
	static int run_quick = -1;

	if (run_quick == -1)
		run_quick = env_set("IGT_QUICK");

	return run_quick;
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

void drmtest_progress(const char *header, uint64_t i, uint64_t total)
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

/* helpers to create nice-looking framebuffers */
static cairo_surface_t *
paint_allocate_surface(int fd, int width, int height, int depth, int bpp,
		       bool tiled,
		       struct kmstest_fb *fb_info)
{
	cairo_format_t format;
	struct drm_i915_gem_set_tiling set_tiling;
	int size;
	unsigned stride;
	uint32_t *fb_ptr;

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

	switch (depth) {
	case 16:
		format = CAIRO_FORMAT_RGB16_565;
		break;
	case 24:
		format = CAIRO_FORMAT_RGB24;
		break;
#if 0
	case 30:
		format = CAIRO_FORMAT_RGB30;
		break;
#endif
	case 32:
		format = CAIRO_FORMAT_ARGB32;
		break;
	default:
		fprintf(stderr, "bad depth %d\n", depth);
		return NULL;
	}

	assert (bpp >= depth);

	fb_info->gem_handle = gem_create(fd, size);

	if (tiled) {
		set_tiling.handle = fb_info->gem_handle;
		set_tiling.tiling_mode = I915_TILING_X;
		set_tiling.stride = stride;
		if (ioctl(fd, DRM_IOCTL_I915_GEM_SET_TILING, &set_tiling)) {
			fprintf(stderr, "set tiling failed: %s (stride=%d, size=%d)\n",
				strerror(errno), stride, size);
			return NULL;
		}
	}

	fb_ptr = gem_mmap(fd, fb_info->gem_handle, size, PROT_READ | PROT_WRITE);

	fb_info->stride = stride;
	fb_info->size = size;

	return cairo_image_surface_create_for_data((unsigned char *)fb_ptr,
						   format, width, height,
						   stride);
}

static void
paint_color_gradient(cairo_t *cr, int x, int y, int w, int h,
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

	paint_color_gradient(cr, x, y, gr_width, gr_height, 1, 0, 0);

	y += gr_height;
	paint_color_gradient(cr, x, y, gr_width, gr_height, 0, 1, 0);

	y += gr_height;
	paint_color_gradient(cr, x, y, gr_width, gr_height, 0, 0, 1);

	y += gr_height;
	paint_color_gradient(cr, x, y, gr_width, gr_height, 1, 1, 1);
}

enum corner {
	topleft,
	topright,
	bottomleft,
	bottomright,
};

static void
paint_marker(cairo_t *cr, int x, int y, char *str, enum corner text_location)
{
	cairo_text_extents_t extents;
	int xoff, yoff;

	cairo_set_font_size(cr, 18);
	cairo_text_extents(cr, str, &extents);

	switch (text_location) {
	case topleft:
		xoff = -20;
		xoff -= extents.width;
		yoff = -20;
		break;
	case topright:
		xoff = 20;
		yoff = -20;
		break;
	case bottomleft:
		xoff = -20;
		xoff -= extents.width;
		yoff = 20;
		break;
	case bottomright:
		xoff = 20;
		yoff = 20;
		break;
	default:
		xoff = 0;
		yoff = 0;
	}

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

	cairo_move_to(cr, x + xoff, y + yoff);
	cairo_text_path(cr, str);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_fill(cr);
}

unsigned int kmstest_create_fb(int fd, int width, int height, int bpp,
			       int depth, bool tiled,
			       struct kmstest_fb *fb_info,
			       kmstest_paint_func paint_func,
			       void *func_arg)
{
	cairo_surface_t *surface;
	cairo_status_t status;
	cairo_t *cr;
	char buf[128];
	unsigned int fb_id;

	surface = paint_allocate_surface(fd, width, height, depth, bpp,
					 tiled, fb_info);
	assert(surface);

	cr = cairo_create(surface);

	paint_test_patterns(cr, width, height);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	/* Paint corner markers */
	snprintf(buf, sizeof buf, "(%d, %d)", 0, 0);
	paint_marker(cr, 0, 0, buf, bottomright);
	snprintf(buf, sizeof buf, "(%d, %d)", width, 0);
	paint_marker(cr, width, 0, buf, bottomleft);
	snprintf(buf, sizeof buf, "(%d, %d)", 0, height);
	paint_marker(cr, 0, height, buf, topright);
	snprintf(buf, sizeof buf, "(%d, %d)", width, height);
	paint_marker(cr, width, height, buf, topleft);

	if (paint_func)
		paint_func(cr, width, height, func_arg);

	status = cairo_status(cr);
	assert(!status);
	cairo_destroy(cr);

	do_or_die(drmModeAddFB(fd, width, height, depth, bpp,
			       fb_info->stride,
			       fb_info->gem_handle, &fb_id));

	cairo_surface_destroy(surface);

	fb_info->fb_id = fb_id;

	return fb_id;
}

void kmstest_remove_fb(int fd, int fb_id)
{
	do_or_die(drmModeRmFB(fd, fb_id));
}

void kmstest_dump_mode(drmModeModeInfo *mode)
{
	printf("  %s %d %d %d %d %d %d %d %d %d 0x%x 0x%x %d\n",
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
	       mode->clock);
	fflush(stdout);
}

int kmstest_get_pipe_from_crtc_id(int fd, int crtc_id)
{
	struct drm_i915_get_pipe_from_crtc_id pfci;
	int ret;

	memset(&pfci, 0, sizeof(pfci));
	pfci.crtc_id = crtc_id;
	ret = drmIoctl(fd, DRM_IOCTL_I915_GET_PIPE_FROM_CRTC_ID, &pfci);
	assert(ret == 0);

	return pfci.pipe;
}

