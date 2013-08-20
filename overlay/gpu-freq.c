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

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "gpu-freq.h"

int gpu_freq_init(struct gpu_freq *gf)
{
	char buf[4096], *s;
	int fd, len = -1;

	memset(gf, 0, sizeof(*gf));

	fd = open("/sys/kernel/debug/dri/0/i915_cur_delayinfo", 0);
	if (fd < 0)
		return gf->error = errno;

	len = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if (len < 0)
		goto err;

	buf[len] = '\0';

	s = strstr(buf, "(RPN)");
	if (s == NULL)
		goto err;
	sscanf(s, "(RPN) frequency: %dMHz", &gf->rpn);

	s = strstr(s, "(RP1)");
	if (s == NULL)
		goto err;
	sscanf(s, "(RP1) frequency: %dMHz", &gf->rp1);

	s = strstr(s, "(RP0)");
	if (s == NULL)
		goto err;
	sscanf(s, "(RP0) frequency: %dMHz", &gf->rp0);

	s = strstr(s, "Max");
	if (s == NULL)
		goto err;
	sscanf(s, "Max overclocked frequency: %dMHz", &gf->max);
	gf->min = gf->rpn;

	return 0;

err:
	return gf->error = EIO;
}

int gpu_freq_update(struct gpu_freq *gf)
{
	char buf[4096], *s;
	int fd, len = -1;

	if (gf->error)
		return gf->error;

	fd = open("/sys/kernel/debug/dri/0/i915_cur_delayinfo", 0);
	if (fd < 0)
		return gf->error = errno;

	len = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if (len < 0)
		return gf->error = EIO;

	buf[len] = '\0';

	s = strstr(buf, "RPNSWREQ:");
	if (s)
		sscanf(s, "RPNSWREQ: %dMHz", &gf->request);

	s = strstr(buf, "CAGF:");
	if (s)
		sscanf(s, "CAGF: %dMHz", &gf->current);

	return 0;
}
