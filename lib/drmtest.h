/*
 * Copyright © 2007 Intel Corporation
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
 *
 */

#ifndef DRMTEST_H
#define DRMTEST_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <cairo.h>
#include <setjmp.h>
#include <sys/mman.h>

#include "xf86drm.h"
#include "xf86drmMode.h"
#include "i915_drm.h"
#include "intel_batchbuffer.h"
#include "intel_chipset.h"
#include "intel_gpu_tools.h"

drm_intel_bo * gem_handle_to_libdrm_bo(drm_intel_bufmgr *bufmgr, int fd,
				       const char *name, uint32_t handle);

int drm_get_card(void);
int drm_open_any(void);

void gem_quiescent_gpu(int fd);

/* ioctl wrappers and similar stuff for bare metal testing */
void gem_set_tiling(int fd, uint32_t handle, int tiling, int stride);
bool gem_has_enable_ring(int fd,int param);
bool gem_has_bsd(int fd);
bool gem_has_blt(int fd);
bool gem_has_vebox(int fd);
int gem_get_num_rings(int fd);

void gem_set_caching(int fd, uint32_t handle, int caching);
uint32_t gem_get_caching(int fd, uint32_t handle);
uint32_t gem_flink(int fd, uint32_t handle);
uint32_t gem_open(int fd, uint32_t name);
void gem_close(int fd, uint32_t handle);
void gem_write(int fd, uint32_t handle, uint32_t offset,  const void *buf, uint32_t size);
void gem_read(int fd, uint32_t handle, uint32_t offset, void *buf, uint32_t size);
void gem_set_domain(int fd, uint32_t handle,
		    uint32_t read_domains, uint32_t write_domain);
void gem_sync(int fd, uint32_t handle);
uint32_t gem_create(int fd, int size);
void gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *execbuf);

void *gem_mmap__gtt(int fd, uint32_t handle, int size, int prot);
void *gem_mmap__cpu(int fd, uint32_t handle, int size, int prot);
#define gem_mmap gem_mmap__gtt

uint64_t gem_aperture_size(int fd);
uint64_t gem_mappable_aperture_size(void);
int gem_madvise(int fd, uint32_t handle, int state);

uint32_t gem_context_create(int fd);

/* feature test helpers */
bool gem_uses_aliasing_ppgtt(int fd);
int gem_available_fences(int fd);

/* prime */
int prime_handle_to_fd(int fd, uint32_t handle);
uint32_t prime_fd_to_handle(int fd, int dma_buf_fd);
off_t prime_get_size(int dma_buf_fd);

/* generally useful helpers */
void igt_fork_signal_helper(void);
void igt_stop_signal_helper(void);
void igt_exchange_int(void *array, unsigned i, unsigned j);
void igt_permute_array(void *array, unsigned size,
			   void (*exchange_func)(void *array,
						 unsigned i,
						 unsigned j));
void igt_progress(const char *header, uint64_t i, uint64_t total);

/* subtest infrastructure */
jmp_buf igt_subtest_jmpbuf;
void igt_subtest_init(int argc, char **argv);
typedef int (*igt_opt_handler_t)(int opt, int opt_index);
struct option;
int igt_subtest_init_parse_opts(int argc, char **argv,
				const char *extra_short_opts,
				struct option *extra_long_opts,
				const char *help_str,
				igt_opt_handler_t opt_handler);
bool __igt_run_subtest(const char *subtest_name);
/**
 * igt_subtest/_f - Denote a subtest code block
 *
 * Magic control flow which denotes a subtest code block. Within that codeblock
 * igt_skip|success will only bail out of the subtest. The _f variant accepts a
 * printf format string, which is useful for constructing combinatorial tests.
 */
#define igt_tokencat2(x, y) x ## y
#define igt_tokencat(x, y) igt_tokencat2(x, y)
#define __igt_subtest_f(tmp, format...) \
	for (char tmp [256]; \
	     snprintf( tmp , sizeof( tmp ), \
		      format), \
	     __igt_run_subtest( tmp ) && \
	     (setjmp(igt_subtest_jmpbuf) == 0); \
	     igt_success())
#define igt_subtest_f(f...) \
	__igt_subtest_f(igt_tokencat(__tmpchar, __LINE__), f)
#define igt_subtest(name) for (; __igt_run_subtest((name)) && \
				   (setjmp(igt_subtest_jmpbuf) == 0); \
				   igt_success())
const char *igt_subtest_name(void);
/**
 * igt_skip - subtest aware test skipping
 *
 * For tests with subtests this will either bail out of the current subtest or
 * mark all subsequent subtests as SKIP (in case some global setup code failed).
 *
 * For normal tests without subtest it will directly exit.
 */
__attribute__((format(printf, 1, 2))) void igt_skip(const char *f, ...);
__attribute__((format(printf, 5, 6)))
void __igt_skip_check(const char *file, const int line,
		      const char *func, const char *check,
		      const char *format, ...);
/**
 * igt_success - complete a (subtest) as successfull
 *
 * This bails out of a subtests and marks it as successful. For global tests it
 * it won't bail out of anything.
 */
void igt_success(void);
/**
 * igt_fail - fail a testcase
 *
 * For subtest it just bails out of the subtest, when run in global context it
 * will exit. Note that it won't attempt to keep on running further tests,
 * presuming that some mandatory setup failed.
 */
void igt_fail(int exitcode) __attribute__((noreturn));
__attribute__((format(printf, 6, 7)))
void __igt_fail_assert(int exitcode, const char *file,
		       const int line, const char *func, const char *assertion,
		       const char *format, ...)
	__attribute__((noreturn));
/**
 * igt_exit - exit() for igts
 *
 * This will exit the test with the right exit code when subtests have been
 * skipped. For normal tests it exits with a successful exit code, presuming
 * everything has worked out. For subtests it also checks that at least one
 * subtest has been run (save when only listing subtests.
 */
void igt_exit(void) __attribute__((noreturn));
/**
 * igt_assert - fails (sub-)test if a condition is not met
 *
 * Should be used everywhere where a test checks results.
 */
#define igt_assert(expr) \
	do { if (!(expr)) \
		__igt_fail_assert(99, __FILE__, __LINE__, __func__, #expr , NULL); \
	} while (0)
#define igt_assert_f(expr, f...) \
	do { if (!(expr)) \
		__igt_fail_assert(99, __FILE__, __LINE__, __func__, #expr , f); \
	} while (0)
/**
 * igt_require - skip a (sub-)test if a condition is not met
 *
 * This is useful to streamline the skip logic since it allows for a more flat
 * code control flow.
 */
#define igt_require(expr) igt_skip_on(!(expr))
#define igt_skip_on(expr) \
	do { if ((expr)) \
		__igt_skip_check(__FILE__, __LINE__, __func__, #expr , NULL); \
	} while (0)
#define igt_require_f(expr, f...) igt_skip_on_f(!(expr), f)
#define igt_skip_on_f(expr, f...) \
	do { if ((expr)) \
		__igt_skip_check(__FILE__, __LINE__, __func__, #expr , f); \
	} while (0)

bool __igt_fixture(void);
void __igt_fixture_complete(void);
void __igt_fixture_end(void) __attribute__((noreturn));
/**
 * igt_fixture - annote global test fixture code
 * 
 * Testcase with subtests often need to set up a bunch of global state as the
 * common test fixture. To avoid such code interferring with the subtest
 * enumeration (e.g. when enumerating on systemes without an intel gpu) such
 * blocks should be annotated with igt_fixture.
 */
#define igt_fixture for (int igt_tokencat(__tmpint,__LINE__) = 0; \
			 igt_tokencat(__tmpint,__LINE__) < 1 && \
			 __igt_fixture() && \
			 (setjmp(igt_subtest_jmpbuf) == 0); \
			 igt_tokencat(__tmpint,__LINE__) ++, \
			 __igt_fixture_complete())

bool __igt_fork(void);
/**
 * igt_fork - fork parallel test threads with fork()
 * @child: name of the int variable with the child number
 * @num_children: number of children to fork
 *
 * Joining all test threads should be done with igt_waitchildren to ensure that
 * the exit codes of all children are properly reflected in the test status.
 */
#define igt_fork(child, num_children) \
	for (int child = 0; child < (num_children); child++) \
		for (; __igt_fork(); exit(0))
void igt_waitchildren(void);

struct igt_helper_process {
	bool running;
	pid_t pid;
	int id;
};
bool __igt_fork_helper(struct igt_helper_process *proc);
void igt_stop_helper(struct igt_helper_process *proc);
#define igt_fork_helper(proc) \
	for (; __igt_fork_helper(proc); exit(0))

/* check functions which auto-skip tests by calling igt_skip() */
void gem_require_caching(int fd);
static inline void gem_require_ring(int fd, int ring_id)
{
	switch (ring_id) {
	case I915_EXEC_RENDER:
		return;
	case I915_EXEC_BLT:
		igt_require(HAS_BLT_RING(intel_get_drm_devid(fd)));
		return;
	case I915_EXEC_BSD:
		igt_require(HAS_BSD_RING(intel_get_drm_devid(fd)));
		return;
#ifdef I915_EXEC_VEBOX
	case I915_EXEC_VEBOX:
		igt_require(gem_has_vebox(fd));
		return;
#endif
	default:
		assert(0);
		return;
	}
}

/* helpers to automatically reduce test runtime in simulation */
bool igt_run_in_simulation(void);
#define SLOW_QUICK(slow,quick) (igt_run_in_simulation() ? (quick) : (slow))
/**
 * igt_skip_on_simulation - skip tests when INTEL_SIMULATION env war is set
 *
 * Skip the test when running on simulation (and that's relevant only when
 * we're not in the mode where we list the subtests).
 *
 * This function is subtest aware (since it uses igt_skip) and so can be used to
 * skip specific subtests or all subsequent subtests.
 */
void igt_skip_on_simulation(void);

#define DRMTEST_MODE_FLAG_3D_MASK   (DRM_MODE_FLAG_3D_FRAME_PACKING         | \
				     DRM_MODE_FLAG_3D_FIELD_ALTERNATIVE     | \
				     DRM_MODE_FLAG_3D_LINE_ALTERNATIVE      | \
				     DRM_MODE_FLAG_3D_SIDE_BY_SIDE_FULL     | \
				     DRM_MODE_FLAG_3D_L_DEPTH               | \
				     DRM_MODE_FLAG_3D_L_DEPTH_GFX_GFX_DEPTH | \
				     DRM_MODE_FLAG_3D_TOP_AND_BOTTOM        | \
				     DRM_MODE_FLAG_3D_SIDE_BY_SIDE_HALF)

/* helpers based upon the libdrm buffer manager */
void igt_init_aperture_trashers(drm_intel_bufmgr *bufmgr);
void igt_trash_aperture(void);
void igt_cleanup_aperture_trashers(void);

struct kmstest_connector_config {
	drmModeCrtc *crtc;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
	drmModeModeInfo default_mode;
	int crtc_idx;
	int pipe;
};

int kmstest_get_connector_default_mode(int drm_fd, drmModeConnector *connector,
				      drmModeModeInfo *mode);
int kmstest_get_connector_config(int drm_fd, uint32_t connector_id,
				 unsigned long crtc_idx_mask,
				 struct kmstest_connector_config *config);
void kmstest_free_connector_config(struct kmstest_connector_config *config);

/* helpers to create nice-looking framebuffers */
struct kmstest_fb {
	uint32_t fb_id;
	uint32_t gem_handle;
	uint32_t drm_format;
	int width;
	int height;
	int depth;
	unsigned stride;
	unsigned tiling;
	unsigned size;
	cairo_t *cairo_ctx;
};

enum kmstest_text_align {
	align_left,
	align_bottom	= align_left,
	align_right	= 0x01,
	align_top	= 0x02,
	align_vcenter	= 0x04,
	align_hcenter	= 0x08,
};

int kmstest_cairo_printf_line(cairo_t *cr, enum kmstest_text_align align,
			       double yspacing, const char *fmt, ...)
			       __attribute__((format (printf, 4, 5)));

unsigned int kmstest_create_fb(int fd, int width, int height, int bpp,
			       int depth, bool tiled,
			       struct kmstest_fb *fb_info);
unsigned int kmstest_create_fb2(int fd, int width, int height, uint32_t format,
			        bool tiled, struct kmstest_fb *fb);
void kmstest_remove_fb(int fd, struct kmstest_fb *fb_info);
cairo_t *kmstest_get_cairo_ctx(int fd, struct kmstest_fb *fb);
void kmstest_paint_color_gradient(cairo_t *cr, int x, int y, int w, int h,
				  int r, int g, int b);
void kmstest_paint_test_pattern(cairo_t *cr, int width, int height);
void kmstest_paint_image(cairo_t *cr, const char *filename,
			 int dst_x, int dst_y, int dst_width, int dst_height);
void kmstest_write_fb(int fd, struct kmstest_fb *fb, const char *filename);
void kmstest_dump_mode(drmModeModeInfo *mode);
int kmstest_get_pipe_from_crtc_id(int fd, int crtc_id);
const char *kmstest_format_str(uint32_t drm_format);
const char *kmstest_pipe_str(int pipe);
void kmstest_get_all_formats(const uint32_t **formats, int *format_count);
const char *kmstest_encoder_type_str(int type);
const char *kmstest_connector_status_str(int type);
const char *kmstest_connector_type_str(int type);

uint32_t drm_format_to_bpp(uint32_t drm_format);

#define do_or_die(x) igt_assert((x) == 0)
#define do_ioctl(fd, ptr, sz) igt_assert(drmIoctl((fd), (ptr), (sz)) == 0)

typedef void (*igt_exit_handler_t)(int sig);

/* reliable atexit helpers, also work when killed by a signal (if possible) */
int igt_install_exit_handler(igt_exit_handler_t fn);
void igt_enable_exit_handler(void);
void igt_disable_exit_handler(void);

/* set vt into graphics mode, required to prevent fbcon from interfering */
int igt_set_vt_graphics_mode(void);

/* prefault disabling, needs the corresponding debugfs interface */
void igt_disable_prefault(void);
void igt_enable_prefault(void);

/* suspend and auto-resume system */
void igt_system_suspend_autoresume(void);

#endif /* DRMTEST_H */
