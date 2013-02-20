/*
 * Copyright © 2008-9 Intel Corporation
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
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <pthread.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"

#define OBJECT_SIZE (128*1024) /* restricted to 1MiB alignment on i915 fences */

/* Before introduction of the LRU list for fences, allocation of a fence for a page
 * fault would use the first inactive fence (i.e. in preference one with no outstanding
 * GPU activity, or it would wait on the first to finish). Given the choice, it would simply
 * reuse the fence that had just been allocated for the previous page-fault - the worst choice
 * when copying between two buffers and thus constantly swapping fences.
 */

static void *
bo_create (int fd)
{
	void *ptr;
	int handle;

	handle = gem_create(fd, OBJECT_SIZE);

	gem_set_tiling(fd, handle, I915_TILING_X, 1024);

	ptr = gem_mmap(fd, handle, OBJECT_SIZE, PROT_READ | PROT_WRITE);

	/* XXX: mmap_gtt pulls the bo into the GTT read domain. */
	gem_sync(fd, handle);

	return ptr;
}

static void *
bo_copy (void *_arg)
{
	int fd = *(int *)_arg;
	int n;
	char *a, *b;

	a = bo_create (fd);
	b = bo_create (fd);

	for (n = 0; n < 1000; n++) {
		memcpy (a, b, OBJECT_SIZE);
		sched_yield ();
	}

	return NULL;
}

int
main(int argc, char **argv)
{
	drm_i915_getparam_t gp;
	pthread_t threads[32];
	int n, num_fences;
	int fd, ret;

	fd = drm_open_any();

	gp.param = I915_PARAM_NUM_FENCES_AVAIL;
	gp.value = &num_fences;
	ret = ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
	assert (ret == 0);

	printf ("creating %d threads\n", num_fences);
	assert (num_fences < sizeof (threads) / sizeof (threads[0]));

	for (n = 0; n < num_fences; n++)
		pthread_create (&threads[n], NULL, bo_copy, &fd);

	for (n = 0; n < num_fences; n++)
		pthread_join (threads[n], NULL);

	close(fd);

	return 0;
}
