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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * To share code with mesa without having to do big modifications and still be
 * able to sync files together at a later point, this file holds macros and
 * types defined in mesa's core headers.
 */

#ifndef __BRW_COMPAT_H__
#define __BRW_COMPAT_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 *  * __builtin_expect macros
 *   */
#if !defined(__GNUC__)
#  define __builtin_expect(x, y) (x)
#endif

#ifndef likely
#  ifdef __GNUC__
#    define likely(x)   __builtin_expect(!!(x), 1)
#    define unlikely(x) __builtin_expect(!!(x), 0)
#  else
#    define likely(x)   (x)
#    define unlikely(x) (x)
#  endif
#endif

#if (__GNUC__ >= 3)
#define PRINTFLIKE(f, a) __attribute__ ((format(__printf__, f, a)))
#else
#define PRINTFLIKE(f, a)
#endif

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define Elements(x) ARRAY_SIZE(x)

typedef union { float f; int i; unsigned u; } fi_type;

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* __BRW_COMPAT_H__ */
