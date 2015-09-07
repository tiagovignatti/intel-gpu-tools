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
 * @values_u64: An array containing pushed integer values
 * @values_f: An array containing pushed float values
 * @n_values: The number of pushed values
 */
typedef struct {
	union {
		uint64_t *values_u64;
		double *values_f;
	};
	unsigned int n_values;
	unsigned int is_float : 1;

	/*< private >*/
	unsigned int capacity;
	unsigned int is_population  : 1;
	unsigned int mean_variance_valid : 1;
	unsigned int sorted_array_valid : 1;

	uint64_t min, max;
	double range[2];
	double mean, variance;

	union {
		uint64_t *sorted_u64;
		double *sorted_f;
	};
} igt_stats_t;

void igt_stats_init(igt_stats_t *stats);
void igt_stats_init_with_size(igt_stats_t *stats, unsigned int capacity);
void igt_stats_fini(igt_stats_t *stats);
bool igt_stats_is_population(igt_stats_t *stats);
void igt_stats_set_population(igt_stats_t *stats, bool full_population);
void igt_stats_push(igt_stats_t *stats, uint64_t value);
void igt_stats_push_float(igt_stats_t *stats, double value);
void igt_stats_push_array(igt_stats_t *stats,
			  const uint64_t *values, unsigned int n_values);
uint64_t igt_stats_get_min(igt_stats_t *stats);
uint64_t igt_stats_get_max(igt_stats_t *stats);
uint64_t igt_stats_get_range(igt_stats_t *stats);
void igt_stats_get_quartiles(igt_stats_t *stats,
			     double *q1, double *q2, double *q3);
double igt_stats_get_iqr(igt_stats_t *stats);
double igt_stats_get_iqm(igt_stats_t *stats);
double igt_stats_get_mean(igt_stats_t *stats);
double igt_stats_get_trimean(igt_stats_t *stats);
double igt_stats_get_median(igt_stats_t *stats);
double igt_stats_get_variance(igt_stats_t *stats);
double igt_stats_get_std_deviation(igt_stats_t *stats);

#endif /* __IGT_STATS_H__ */
