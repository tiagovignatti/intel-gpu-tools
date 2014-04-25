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

#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <errno.h>

#include <xf86drm.h>

#include "intel_batchbuffer.h"

#ifdef ANDROID
#ifndef HAVE_MMAP64
extern void*  __mmap2(void *, size_t, int, int, int, off_t);

/* mmap64 is a recent addition to bionic and not available in all android builds. */
/* I can find no reliable way to know if it is defined or not - so just avoid it */
#define mmap64 igt_mmap64
static inline void *igt_mmap64(void *addr, size_t length, int prot, int flags,
        int fd, off64_t offset)
{
    return __mmap2(addr, length, prot, flags, fd, offset >> 12);
}
#endif
#endif

/**
 * ARRAY_SIZE:
 * @arr: static array
 *
 * Macro to compute the size of the static array @arr.
 */
#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))

/**
 * ALIGN:
 * @v: value to be aligned
 * @a: alignment unit in bytes
 *
 * Macro to align a value @v to a specified unit @a.
 */
#define ALIGN(v, a) (((v) + (a)-1) & ~((a)-1))

int drm_get_card(void);
int drm_open_any(void);
int drm_open_any_render(void);

void gem_quiescent_gpu(int fd);

/**
 * do_or_die:
 * @x: command
 *
 * Simple macro to execute x and check that it's return value is 0. Presumes
 * that in any failure case the return value is non-zero and a precise error is
 * logged into errno. Uses igt_assert() internally.
 */
#define do_or_die(x) igt_assert((x) == 0)

/**
 * do_ioctl:
 * @fd: open i915 drm file descriptor
 * @ioc: ioctl op definition from drm headers
 * @ioc_data: data pointer for the ioctl operation
 *
 * This macro wraps drmIoctl() and uses igt_assert to check that it has been
 * successfully executed.
 */
#define do_ioctl(fd, ioc, ioc_data) do { \
	igt_assert(drmIoctl((fd), (ioc), (ioc_data)) == 0); \
	errno = 0; \
} while (0)

#endif /* DRMTEST_H */
