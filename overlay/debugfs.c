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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "debugfs.h"

char debugfs_path[128];
char debugfs_dri_path[128];

int debugfs_init(void)
{
	const char *path = "/sys/kernel/debug";
	struct stat st;
	int n;

	if (stat("/debug/dri", &st) == 0) {
		path = "/debug/dri";
		goto find_minor;
	}

	if (stat("/sys/kernel/debug/dri", &st) == 0)
		goto find_minor;

	if (stat("/sys/kernel/debug", &st))
		return errno;

	if (mount("debug", "/sys/kernel/debug", "debugfs", 0, 0))
		return errno;

find_minor:
	strcpy(debugfs_path, path);
	for (n = 0; n < 16; n++) {
		int len = sprintf(debugfs_dri_path, "%s/dri/%d", path, n);
		sprintf(debugfs_dri_path + len, "/i915_error_state");
		if (stat(debugfs_dri_path, &st) == 0) {
			debugfs_dri_path[len] = '\0';
			return 0;
		}
	}

	debugfs_dri_path[0] = '\0';
	return ENOENT;
}
