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
 */

/* Simple tool to print statistics on incoming line buffers intervals */

#define _ISOC99_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "igt_stats.h"

static void statify(FILE *file, const char *name)
{
	igt_stats_t stats;
	char *line = NULL;
	size_t line_len = 0;

	igt_stats_init(&stats);
	while (getline(&line, &line_len, file) != -1) {
		char *end, *start = line;
		union {
			unsigned long long u64;
			double fp;
		} u;
		int is_float;

		is_float = 0;
		u.u64 = strtoull(start, &end, 0);
		if (*end == '.') {
			u.fp = strtod(start, &end);
			is_float = 1;
		}
		while (start != end) {
			if (is_float)
				igt_stats_push_float(&stats, u.fp);
			else
				igt_stats_push(&stats, u.u64);

			is_float = 0;
			u.u64 = strtoull(start = end, &end, 0);
			if (*end == '.') {
				u.fp = strtod(start, &end);
				is_float = 1;
			}
		}
	}
	free(line);

	if (name)
		printf("%s: ", name);

	printf("%f\n", igt_stats_get_trimean(&stats));

	igt_stats_fini(&stats);
}

int main(int argc, char **argv)
{
	if (argc == 1) {
		statify(stdin, NULL);
	} else {
		int i;

		for (i = 1; i < argc; i++) {
			FILE *file;

			file = fopen(argv[i], "r");
			if (file == NULL) {
				perror(argv[i]);
				continue;
			}

			statify(file, argv[i]);
			fclose(file);
		}
	}

	return 0;
}
