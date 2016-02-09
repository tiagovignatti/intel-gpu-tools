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

#define DRIVER_ANY 0x1
#define DRIVER_INTEL (0x1 << 1)
#define DRIVER_VC4 (0x1 << 2)

#ifdef ANDROID
#if (!(defined HAVE_MMAP64)) && (!(defined __x86_64__))
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
int drm_open_driver(int chipset);
int drm_open_driver_master(int chipset);
int drm_open_driver_render(int chipset);
int __drm_open_driver(int chipset);

void gem_quiescent_gpu(int fd);

void igt_require_intel(int fd);

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
	igt_assert_eq(igt_ioctl((fd), (ioc), (ioc_data)), 0); \
	errno = 0; \
} while (0)

/**
 * do_ioctl_err:
 * @fd: open i915 drm file descriptor
 * @ioc: ioctl op definition from drm headers
 * @ioc_data: data pointer for the ioctl operation
 * @err: value to expect in errno
 *
 * This macro wraps drmIoctl() and uses igt_assert to check that it fails,
 * returning a particular value in errno.
 */
#define do_ioctl_err(fd, ioc, ioc_data, err) do { \
	igt_assert_eq(igt_ioctl((fd), (ioc), (ioc_data)), -1); \
	igt_assert_eq(errno, err); \
	errno = 0; \
} while (0)

#endif /* DRMTEST_H */
