/*
 * Copyright © 2011 Intel Corporation
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
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_gpu_tools.h"

static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;

/* Testcase: check whether the libdrm vma limiter works
 *
 * We've had reports of the X server exhausting the default rlimit of 64k vma's
 * in the kernel. libdrm has grown facilities to limit the vma caching since,
 * this checks whether they actually work.
 *
 * This one checks cpu mmaps only.
 */

/* we do both cpu and gtt maps, so only need half of 64k to exhaust */
#define BO_ARRAY_SIZE 68000
drm_intel_bo *bos[BO_ARRAY_SIZE];

int main(int argc, char **argv)
{
	int fd;
	int i;
	char *ptr;

	fd = drm_open_any();

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));

	drm_intel_bufmgr_gem_set_vma_cache_size(bufmgr, 500);

	for (i = 0; i < BO_ARRAY_SIZE; i++) {
		bos[i] = drm_intel_bo_alloc(bufmgr, "mmap bo", 4096, 4096);
		assert(bos[i]);

		drm_intel_gem_bo_map_gtt(bos[i]);
		ptr = bos[i]->virtual;
		assert(ptr);
		*ptr = 'c';
		drm_intel_gem_bo_unmap_gtt(bos[i]);
	}

	/* and recheck whether a second map of the same still works */
	for (i = 0; i < BO_ARRAY_SIZE; i++) {
		bos[i] = drm_intel_bo_alloc(bufmgr, "mmap bo", 4096, 4096);
		assert(bos[i]);

		drm_intel_gem_bo_map_gtt(bos[i]);
		ptr = bos[i]->virtual;
		assert(*ptr = 'c');
		drm_intel_gem_bo_unmap_gtt(bos[i]);
	}

	intel_batchbuffer_free(batch);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return 0;
}
