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

#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))

static void push_fixture_1(igt_stats_t *stats)
{
	igt_stats_push(stats, 2);
	igt_stats_push(stats, 4);
	igt_stats_push(stats, 6);
	igt_stats_push(stats, 8);
	igt_stats_push(stats, 10);
}

/* Make sure we zero igt_stats_t fields at init() time */
static void test_init_zero(void)
{
	igt_stats_t stats;

	stats.mean = 1.;
	igt_stats_init(&stats);
	igt_assert_eq_double(stats.mean, 0.);
}

static void test_init(void)
{
	igt_stats_t stats;

	igt_stats_init(&stats);

	/*
	 * Make sure we default to representing only a sample of a bigger
	 * population.
	 */
	igt_assert(igt_stats_is_population(&stats) == false);
}

static void test_min_max(void)
{
	igt_stats_t stats;

	igt_stats_init(&stats);
	push_fixture_1(&stats);

	igt_assert(igt_stats_get_min(&stats) == 2);
	igt_assert(igt_stats_get_max(&stats) == 10);
}

static void test_range(void)
{
	igt_stats_t stats;

	igt_stats_init(&stats);
	push_fixture_1(&stats);

	igt_assert(igt_stats_get_range(&stats) == 8);
}

/*
 * Examples taken from: https://en.wikipedia.org/wiki/Quartile
 * The values are shifted a bit to test we do indeed start by sorting data
 * set.
 */
static void test_quartiles(void)
{
	static const uint64_t s1[] =
		{ 47, 49, 6, 7, 15, 36, 39, 40, 41, 42, 43 };
	static const uint64_t s2[] = { 40, 41, 7, 15, 36, 39 };
	igt_stats_t stats;
	double q1, q2, q3;

	/* s1, odd number of data points */
	igt_stats_init(&stats);
	igt_stats_push_array(&stats, s1, ARRAY_SIZE(s1));

	igt_stats_get_quartiles(&stats, &q1, &q2, &q3);
	igt_assert_eq_double(q1, 25.5);
	igt_assert_eq_double(q2, 40);
	igt_assert_eq_double(q3, 42.5);
	igt_assert_eq_double(igt_stats_get_median(&stats), 40);
	igt_assert_eq_double(igt_stats_get_iqr(&stats), 42.5 - 25.5);

	igt_stats_fini(&stats);

	/* s1, even number of data points */
	igt_stats_init(&stats);
	igt_stats_push_array(&stats, s2, ARRAY_SIZE(s2));

	igt_stats_get_quartiles(&stats, &q1, &q2, &q3);
	igt_assert_eq_double(q1, 15);
	igt_assert_eq_double(q2, 37.5);
	igt_assert_eq_double(q3, 40);
	igt_assert_eq_double(igt_stats_get_median(&stats), 37.5);
	igt_assert_eq_double(igt_stats_get_iqr(&stats), 40 - 15);

	igt_stats_fini(&stats);
}

static void test_invalidate_sorted(void)
{
	igt_stats_t stats;
	static const uint64_t s1_truncated[] =
		{ 47, 49, 6, 7, 15, 36, 39, 40, 41, 42};
	double median1, median2;

	igt_stats_init(&stats);
	igt_stats_push_array(&stats, s1_truncated, ARRAY_SIZE(s1_truncated));
	median1 = igt_stats_get_median(&stats);

	igt_stats_push(&stats, 43);
	median2 = igt_stats_get_median(&stats);

	igt_assert_eq_double(median2, 40);
	igt_assert(median1 != median2);
}

static void test_mean(void)
{
	igt_stats_t stats;
	double mean;

	igt_stats_init(&stats);
	push_fixture_1(&stats);

	mean = igt_stats_get_mean(&stats);
	igt_assert_eq_double(mean, (2 + 4 + 6 + 8 + 10) / 5.);

	igt_stats_fini(&stats);
}

static void test_invalidate_mean(void)
{
	igt_stats_t stats;
	double mean1, mean2;

	igt_stats_init(&stats);
	push_fixture_1(&stats);

	mean1 = igt_stats_get_mean(&stats);
	igt_assert_eq_double(mean1, (2 + 4 + 6 + 8 + 10) / 5.);

	igt_stats_push(&stats, 100);

	mean2 = igt_stats_get_mean(&stats);
	igt_assert(mean1 != mean2);

	igt_stats_fini(&stats);
}

/*
 * Taken from the "Basic examples" section of:
 * https://en.wikipedia.org/wiki/Standard_deviation
 */
static void test_std_deviation(void)
{
	igt_stats_t stats;
	double mean, variance, std_deviation;

	igt_stats_init(&stats);
	igt_stats_set_population(&stats, true);

	igt_stats_push(&stats, 2);
	igt_stats_push(&stats, 4);
	igt_stats_push(&stats, 4);
	igt_stats_push(&stats, 4);
	igt_stats_push(&stats, 5);
	igt_stats_push(&stats, 5);
	igt_stats_push(&stats, 7);
	igt_stats_push(&stats, 9);

	mean = igt_stats_get_mean(&stats);
	igt_assert_eq_double(mean, (2 + 3 * 4 + 2 * 5 + 7 + 9) / 8.);

	variance = igt_stats_get_variance(&stats);
	igt_assert_eq_double(variance, 4);

	std_deviation = igt_stats_get_std_deviation(&stats);
	igt_assert_eq_double(std_deviation, 2);

	igt_stats_fini(&stats);
}

static void test_reallocation(void)
{
	igt_stats_t stats;
	unsigned int i;

	igt_stats_init_with_size(&stats, 1);

	for (i = 0; i < 101; i++) {
		igt_stats_push(&stats, i);
		/* also triggers ->sorted reallocations */
		if (i > 10)
			igt_stats_get_median(&stats);
	}
	igt_assert(!stats.is_float);

	igt_assert_eq(stats.n_values, 101);
	for (i = 0; i < 101; i++)
		igt_assert_eq(stats.values_u64[i], i);
	igt_assert_eq_double(igt_stats_get_mean(&stats), 50.0);
	igt_assert_eq_double(igt_stats_get_median(&stats), 50.0);

	igt_stats_fini(&stats);
}

igt_simple_main
{
	test_init_zero();
	test_init();
	test_min_max();
	test_range();
	test_quartiles();
	test_invalidate_sorted();
	test_mean();
	test_invalidate_mean();
	test_std_deviation();
	test_reallocation();
}
