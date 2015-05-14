/*
 * Copyright © 2015 Intel Corporation
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
 *    Damien Lespiau <damien.lespiau@intel.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "igt_core.h"
#include "igt_debugfs.h"
#include "igt_kms.h"

typedef struct {
	int fd;
	int pipe;
	int n_crcs;
} display_crc_t;

static int pipe_from_str(const char *str)
{
	unsigned char c;

	if (!str || strlen(str) != 1)
		return -1;

	c = str[0];

	if (c >= 'A' && c <= 'C')
		return c - 'A';

	if (c >= 'a' && c <= 'c')
		return c - 'a';

	if (c >= '0' && c <= '3')
		return c - '0';

	return -1;
}

static void print_crcs(display_crc_t *ctx)
{
	igt_pipe_crc_t *pipe_crc;
	igt_crc_t crc;
	char *crc_str;
	int i;

	pipe_crc = igt_pipe_crc_new(ctx->pipe, INTEL_PIPE_CRC_SOURCE_AUTO);

	for (i = 0; i < ctx->n_crcs; i++) {
		igt_pipe_crc_collect_crc(pipe_crc, &crc);

		crc_str = igt_crc_to_string(&crc);
		printf("CRC on pipe %s: %s\n", kmstest_pipe_name(ctx->pipe),
		       crc_str);
		free(crc_str);
	}

	igt_pipe_crc_free(pipe_crc);
}

static display_crc_t ctx;

int main(int argc, char **argv)
{
	int opt;

	ctx.n_crcs = 1;

	while ((opt = getopt(argc, argv, "p:n:")) != -1) {
		switch (opt) {
		case 'p':
			ctx.pipe = pipe_from_str(optarg);
			if (ctx.pipe == -1) {
				fprintf(stderr, "Unknown pipe %s\n", optarg);
				exit(1);
			}
			break;
		case 'n':
			ctx.n_crcs = atoi(optarg);
			break;
		default:
			igt_assert(0);
		}
	}

	print_crcs(&ctx);
}
