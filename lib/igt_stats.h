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

#ifndef __IGT_STATS_H__
#define __IGT_STATS_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * igt_stats_t:
 * @values: An array containing the pushed values
 * @n_values: The number of pushed values
 */
typedef struct {
	uint64_t *values;
	unsigned int n_values;

	/*< private >*/
	unsigned int capacity;
	unsigned int is_population  : 1;
	unsigned int mean_variance_valid : 1;
	uint64_t min, max;
	double mean, variance;
} igt_stats_t;

void igt_stats_init(igt_stats_t *stats, unsigned int capacity);
void igt_stats_fini(igt_stats_t *stats);
bool igt_stats_is_population(igt_stats_t *stats);
void igt_stats_set_population(igt_stats_t *stats, bool full_population);
void igt_stats_push(igt_stats_t *stats, uint64_t value);
uint64_t igt_stats_get_min(igt_stats_t *stats);
uint64_t igt_stats_get_max(igt_stats_t *stats);
double igt_stats_get_mean(igt_stats_t *stats);
double igt_stats_get_variance(igt_stats_t *stats);
double igt_stats_get_std_deviation(igt_stats_t *stats);

#endif /* __IGT_STATS_H__ */
