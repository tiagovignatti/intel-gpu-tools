/*
 * Copyright © 2008 Intel Corporation
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
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include "config.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#ifdef HAVE_STRUCT_SYSINFO_TOTALRAM
#include <sys/sysinfo.h>
#endif

#include "intel_gpu_tools.h"
#include "i915_drm.h"

uint32_t
intel_get_drm_devid(int fd)
{
	int ret;
	struct drm_i915_getparam gp;
	uint32_t devid;

	gp.param = I915_PARAM_CHIPSET_ID;
	gp.value = (int *)&devid;

	ret = ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp));
	assert(ret == 0);

	return devid;
}

int intel_gen(uint32_t devid)
{
	if (IS_GEN2(devid))
		return 2;
	if (IS_GEN3(devid))
		return 3;
	if (IS_GEN4(devid))
		return 4;
	if (IS_GEN5(devid))
		return 5;
	if (IS_GEN6(devid))
		return 6;
	if (IS_GEN7(devid))
		return 7;

	return -1;
}

uint64_t
intel_get_total_ram_mb(void)
{
	uint64_t retval;

#ifdef HAVE_STRUCT_SYSINFO_TOTALRAM /* Linux */
	struct sysinfo sysinf;
	int ret;

	ret = sysinfo(&sysinf);
	assert(ret == 0);

	retval = sysinf.totalram;
	retval *= sysinf.mem_unit;
#elif defined(_SC_PAGESIZE) && defined(_SC_PHYS_PAGES) /* Solaris */
	long pagesize, npages;

	pagesize = sysconf(_SC_PAGESIZE);
        npages = sysconf(_SC_PHYS_PAGES);

	retval = pagesize * npages;
#else
#error "Unknown how to get RAM size for this OS"
#endif

	return retval / (1024*1024);
}
