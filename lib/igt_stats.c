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

#include "igt_core.h"
#include "igt_stats.h"

void igt_stats_init(igt_stats_t *stats, unsigned int capacity)
{
	stats->values = calloc(capacity, sizeof(*stats->values));
	igt_assert(stats->values);
	stats->capacity = capacity;
	stats->n_values = 0;
}

void igt_stats_fini(igt_stats_t *stats)
{
	free(stats->values);
}

void igt_stats_push(igt_stats_t *stats, uint64_t value)
{
	igt_assert(stats->n_values < stats->capacity);
	stats->values[stats->n_values++] = value;
}

double igt_stats_get_average(igt_stats_t *stats)
{
	unsigned int i;
	double a = 0.0;

	for (i = 0; i < stats->n_values; i++)
		a += (double)stats->values[i] / stats->n_values;

	return a;
}
