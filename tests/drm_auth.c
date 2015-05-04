/*
 * Copyright 2015 David Herrmann <dh.herrmann@gmail.com>
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
 */

/*
 * Testcase: drmGetMagic() and drmAuthMagic()
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/poll.h>
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "igt_aux.h"

IGT_TEST_DESCRIPTION("Call drmGetMagic() and drmAuthMagic() and see if it behaves.");

static int magic_cmp(const void *p1, const void *p2)
{
	return *(const drm_magic_t*)p1 < *(const drm_magic_t*)p2;
}

static void test_many_magics(int master)
{
	drm_magic_t magic, *magics = NULL;
	unsigned int i, j, ns, allocated = 0;
	char path[512];
	int *fds = NULL, slave;

	sprintf(path, "/proc/self/fd/%d", master);

	for (i = 0; ; ++i) {
		/* open slave and make sure it's NOT a master */
		slave = open(path, O_RDWR | O_CLOEXEC);
		if (slave < 0) {
			igt_assert(errno == EMFILE);
			break;
		}
		igt_assert(drmSetMaster(slave) < 0);

		/* resize magic-map */
		if (i >= allocated) {
			ns = allocated * 2;
			igt_assert(ns >= allocated);

			if (!ns)
				ns = 128;

			magics = realloc(magics, sizeof(*magics) * ns);
			igt_assert(magics);

			fds = realloc(fds, sizeof(*fds) * ns);
			igt_assert(fds);

			allocated = ns;
		}

		/* insert magic */
		igt_assert(drmGetMagic(slave, &magic) == 0);
		igt_assert(magic > 0);

		magics[i] = magic;
		fds[i] = slave;
	}

	/* make sure we could at least open a reasonable number of files */
	igt_assert(i > 128);

	/*
	 * We cannot open the DRM file anymore. Lets sort the magic-map and
	 * verify no magic was used multiple times.
	 */
	qsort(magics, i, sizeof(*magics), magic_cmp);
	for (j = 1; j < i; ++j)
		igt_assert(magics[j] != magics[j - 1]);

	/* make sure we can authenticate all of them */
	for (j = 0; j < i; ++j)
		igt_assert(drmAuthMagic(master, magics[j]) == 0);

	/* close files again */
	for (j = 0; j < i; ++j)
		close(fds[j]);

	free(fds);
	free(magics);
}

static void test_basic_auth(int master)
{
	drm_magic_t magic, old_magic;
	int slave;

	/* open slave and make sure it's NOT a master */
	slave = drm_open_any();
	igt_require(slave >= 0);
	igt_require(drmSetMaster(slave) < 0);

	/* retrieve magic for slave */
	igt_assert(drmGetMagic(slave, &magic) == 0);
	igt_assert(magic > 0);

	/* verify the same magic is returned every time */
	old_magic = magic;
	igt_assert(drmGetMagic(slave, &magic) == 0);
	igt_assert_eq(magic, old_magic);

	/* verify magic can be authorized exactly once, on the master */
	igt_assert(drmAuthMagic(slave, magic) < 0);
	igt_assert(drmAuthMagic(master, magic) == 0);
	igt_assert(drmAuthMagic(master, magic) < 0);

	/* verify that the magic did not change */
	old_magic = magic;
	igt_assert(drmGetMagic(slave, &magic) == 0);
	igt_assert_eq(magic, old_magic);

	close(slave);
}

igt_main
{
	int master;

	igt_fixture
		master = drm_open_any_master();

	igt_subtest("basic-auth")
		test_basic_auth(master);

	igt_subtest("many-magics")
		test_many_magics(master);
}
