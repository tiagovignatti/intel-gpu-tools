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
#include <errno.h>
#include <stdbool.h>
#include <setjmp.h>
#include <sys/mman.h>

#include "xf86drm.h"
#include "xf86drmMode.h"
#include "i915_drm.h"
#include "intel_batchbuffer.h"
#include "intel_chipset.h"
#include "intel_gpu_tools.h"

#include "ioctl_wrappers.h"
#include "igt_core.h"

#ifdef ANDROID
#ifndef HAVE_MMAP64
extern void*  __mmap2(void *, size_t, int, int, int, off_t);
static inline void *mmap64(void *addr, size_t length, int prot, int flags,
        int fd, off64_t offset)
{
    return __mmap2(addr, length, prot, flags, fd, offset >> 12);
}
#endif
#endif

#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))

int drm_get_card(void);
int drm_open_any(void);
int drm_open_any_render(void);

void gem_quiescent_gpu(int fd);

/* generally useful helpers */
void igt_fork_signal_helper(void);
void igt_stop_signal_helper(void);
void igt_exchange_int(void *array, unsigned i, unsigned j);
void igt_permute_array(void *array, unsigned size,
			   void (*exchange_func)(void *array,
						 unsigned i,
						 unsigned j));
void igt_progress(const char *header, uint64_t i, uint64_t total);
bool igt_env_set(const char *env_var, bool default_value);

bool drmtest_dump_aub(void);

/* helpers based upon the libdrm buffer manager */
void igt_init_aperture_trashers(drm_intel_bufmgr *bufmgr);
void igt_trash_aperture(void);
void igt_cleanup_aperture_trashers(void);

#define do_or_die(x) igt_assert((x) == 0)
#define do_ioctl(fd, ptr, sz) igt_assert(drmIoctl((fd), (ptr), (sz)) == 0)

/* set vt into graphics mode, required to prevent fbcon from interfering */
void igt_set_vt_graphics_mode(void);

/* suspend and auto-resume system */
void igt_system_suspend_autoresume(void);

/* dropping priviledges */
void igt_drop_root(void);

void igt_wait_for_keypress(void);

/* sysinfo cross-arch wrappers from intel_os.c */
uint64_t intel_get_total_ram_mb(void);
uint64_t intel_get_total_swap_mb(void);

#endif /* DRMTEST_H */
