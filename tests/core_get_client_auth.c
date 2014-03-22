/*
 * Copyright © 2012,2013 Intel Corporation
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
 *
 * Based upon code from libva/va/drm/va_drm_auth.c:
 */

/*
 * Testcase: Check that the hollowed-out get_client ioctl still works for libva
 *
 * Oh dear, libva, why do you do such funny things?
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
# include <sys/syscall.h>

#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"

/* Checks whether the thread id is the current thread */
static bool
is_local_tid(pid_t tid)
{
#ifndef ANDROID
	/* On Linux systems, drmGetClient() would return the thread ID
	   instead of the actual process ID */
	return syscall(SYS_gettid) == tid;
#else
	return gettid() == tid;
#endif
}


static bool check_auth(int fd)
{
	pid_t client_pid;
	int i, auth, pid, uid;
	unsigned long magic, iocs;
	bool is_authenticated = false;

	client_pid = getpid();
	for (i = 0; !is_authenticated; i++) {
		if (drmGetClient(fd, i, &auth, &pid, &uid, &magic, &iocs) != 0)
			break;
		is_authenticated = auth && (pid == client_pid || is_local_tid(pid));
	}
	return is_authenticated;
}


igt_main
{
	/* root (which we run igt as) should always be authenticated */
	igt_subtest("simple") {
		int fd = drm_open_any();

		igt_assert(check_auth(fd) == true);

		close(fd);
	}

	igt_subtest("master-drop") {
		int fd = drm_open_any();
		int fd2 = drm_open_any();

		igt_assert(check_auth(fd2) == true);

		close(fd);

		igt_assert(check_auth(fd2) == true);

		close(fd2);
	}
}
