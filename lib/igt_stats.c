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

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "igt_core.h"
#include "igt_stats.h"

#define U64_MAX         ((uint64_t)~0ULL)

/**
 * SECTION:igt_stats
 * @short_description: Tools for statistical analysis
 * @title: Stats
 * @include: igt_stats.h
 *
 * Various tools to make sense of data.
 *
 * #igt_stats_t is a container of data samples. igt_stats_push() is used to add
 * new samples and various results (mean, variance, standard deviation, ...)
 * can then be retrieved.
 *
 * |[
 *	igt_stats_t stats;
 *
 *	igt_stats_init(&stats, 8);
 *
 *	igt_stats_push(&stats, 2);
 *	igt_stats_push(&stats, 4);
 *	igt_stats_push(&stats, 4);
 *	igt_stats_push(&stats, 4);
 *	igt_stats_push(&stats, 5);
 *	igt_stats_push(&stats, 5);
 *	igt_stats_push(&stats, 7);
 *	igt_stats_push(&stats, 9);
 *
 *	printf("Mean: %lf\n", igt_stats_get_mean(&stats));
 *
 *	igt_stats_fini(&stats);
 * ]|
 */

static unsigned int get_new_capacity(int need)
{
	unsigned int new_capacity;

	/* taken from Python's list */
	new_capacity = (need >> 6) + (need < 9 ? 3 : 6);
	new_capacity += need;

	return new_capacity;
}

static void igt_stats_ensure_capacity(igt_stats_t *stats,
				      unsigned int n_additional_values)
{
	unsigned int new_n_values = stats->n_values + n_additional_values;
	unsigned int new_capacity;

	if (new_n_values <= stats->capacity)
		return;

	new_capacity = get_new_capacity(new_n_values);
	stats->values = realloc(stats->values,
				sizeof(*stats->values) * new_capacity);
	igt_assert(stats->values);

	stats->capacity = new_capacity;

	free(stats->sorted);
	stats->sorted = NULL;
}

/**
 * igt_stats_init:
 * @stats: An #igt_stats_t instance
 *
 * Initializes an #igt_stats_t instance. igt_stats_fini() must be called once
 * finished with @stats.
 */
void igt_stats_init(igt_stats_t *stats)
{
	memset(stats, 0, sizeof(*stats));

	igt_stats_ensure_capacity(stats, 128);

	stats->min = U64_MAX;
	stats->max = 0;
}

/**
 * igt_stats_init_with_size:
 * @stats: An #igt_stats_t instance
 * @capacity: Number of data samples @stats can contain
 *
 * Like igt_stats_init() but with a size to avoid reallocating the underlying
 * array(s) when pushing new values. Useful if we have a good idea of the
 * number of data points we want @stats to hold.
 *
 * igt_stats_fini() must be called once finished with @stats.
 */
void igt_stats_init_with_size(igt_stats_t *stats, unsigned int capacity)
{
	memset(stats, 0, sizeof(*stats));

	igt_stats_ensure_capacity(stats, capacity);

	stats->min = U64_MAX;
	stats->max = 0;
}

/**
 * igt_stats_fini:
 * @stats: An #igt_stats_t instance
 *
 * Frees resources allocated in igt_stats_init().
 */
void igt_stats_fini(igt_stats_t *stats)
{
	free(stats->values);
	free(stats->sorted);
}


/**
 * igt_stats_is_population:
 * @stats: An #igt_stats_t instance
 *
 * Returns: #true if @stats represents a population, #false if only a sample.
 *
 * See igt_stats_set_population() for more details.
 */
bool igt_stats_is_population(igt_stats_t *stats)
{
	return stats->is_population;
}

/**
 * igt_stats_set_population:
 * @stats: An #igt_stats_t instance
 * @full_population: Whether we're dealing with sample data or a full
 *		     population
 *
 * In statistics, we usually deal with a subset of the full data (which may be
 * a continuous or infinite set). Data analysis is then done on a sample of
 * this population.
 *
 * This has some importance as only having a sample of the data leads to
 * [biased estimators](https://en.wikipedia.org/wiki/Bias_of_an_estimator). We
 * currently used the information given by this method to apply
 * [Bessel's correction](https://en.wikipedia.org/wiki/Bessel%27s_correction)
 * to the variance.
 *
 * Note that even if we manage to have an unbiased variance by multiplying
 * a sample variance by the Bessel's correction, n/(n - 1), the standard
 * deviation derived from the unbiased variance isn't itself unbiased.
 * Statisticians talk about a "corrected" standard deviation.
 *
 * When giving #true to this function, the data set in @stats is considered a
 * full population. It's considered a sample of a bigger population otherwise.
 *
 * When newly created, @stats defaults to holding sample data.
 */
void igt_stats_set_population(igt_stats_t *stats, bool full_population)
{
	if (full_population == stats->is_population)
		return;

	stats->is_population = full_population;
	stats->mean_variance_valid = false;
}

/**
 * igt_stats_push:
 * @stats: An #igt_stats_t instance
 * @value: An integer value
 *
 * Adds a new value to the @stats dataset.
 */
void igt_stats_push(igt_stats_t *stats, uint64_t value)
{
	igt_stats_ensure_capacity(stats, 1);

	stats->values[stats->n_values++] = value;

	stats->mean_variance_valid = false;
	stats->sorted_array_valid = false;

	if (value < stats->min)
		stats->min = value;
	if (value > stats->max)
		stats->max = value;
}

/**
 * igt_stats_push_array:
 * @stats: An #igt_stats_t instance
 * @values: (array length=n_values): A pointer to an array of data points
 * @n_values: The number of data points to add
 *
 * Adds an array of values to the @stats dataset.
 */
void igt_stats_push_array(igt_stats_t *stats,
			  const uint64_t *values, unsigned int n_values)
{
	unsigned int i;

	igt_stats_ensure_capacity(stats, n_values);

	for (i = 0; i < n_values; i++)
		igt_stats_push(stats, values[i]);
}

/**
 * igt_stats_get_min:
 * @stats: An #igt_stats_t instance
 *
 * Retrieves the minimal value in @stats
 */
uint64_t igt_stats_get_min(igt_stats_t *stats)
{
	return stats->min;
}

/**
 * igt_stats_get_max:
 * @stats: An #igt_stats_t instance
 *
 * Retrieves the maximum value in @stats
 */
uint64_t igt_stats_get_max(igt_stats_t *stats)
{
	return stats->max;
}

/**
 * igt_stats_get_range:
 * @stats: An #igt_stats_t instance
 *
 * Retrieves the range of the values in @stats. The range is the difference
 * between the highest and the lowest value.
 *
 * The range can be a deceiving characterization of the values, because there
 * can be extreme minimal and maximum values that are just anomalies. Prefer
 * the interquatile range (see igt_stats_get_iqr()) or an histogram.
 */
uint64_t igt_stats_get_range(igt_stats_t *stats)
{
	return igt_stats_get_max(stats) - igt_stats_get_min(stats);
}

static int cmp_u64(const void *pa, const void *pb)
{
	const uint64_t *a = pa, *b = pb;

	if (*a < *b)
		return -1;
	if (*a > *b)
		return 1;
	return 0;
}

static void igt_stats_ensure_sorted_values(igt_stats_t *stats)
{
	if (stats->sorted_array_valid)
		return;

	if (!stats->sorted) {
		/*
		 * igt_stats_ensure_capacity() will free ->sorted when the
		 * capacity increases, which also correspond to an invalidation
		 * of the sorted array. We'll then reallocate it here on
		 * demand.
		 */
		stats->sorted = calloc(stats->capacity, sizeof(*stats->values));
		igt_assert(stats->sorted);
	}

	memcpy(stats->sorted, stats->values,
	       sizeof(*stats->values) * stats->n_values);

	qsort(stats->sorted, stats->n_values, sizeof(*stats->values), cmp_u64);

	stats->sorted_array_valid = true;
}

/*
 * We use Tukey's hinge for our quartiles determination.
 * ends (end, lower_end) are exclusive.
 */
static double
igt_stats_get_median_internal(igt_stats_t *stats,
			      unsigned int start, unsigned int end,
			      unsigned int *lower_end /* out */,
			      unsigned int *upper_start /* out */)
{
	unsigned int mid, n_values = end - start;
	double median;

	igt_stats_ensure_sorted_values(stats);

	/* odd number of data points */
	if (n_values % 2 == 1) {
		/* median is the value in the middle (actual datum) */
		mid = start + n_values / 2;
		median = stats->sorted[mid];

		/* the two halves contain the median value */
		if (lower_end)
			*lower_end = mid + 1;
		if (upper_start)
			*upper_start = mid;

	/* even number of data points */
	} else {
		/*
		 * The middle is in between two indexes, 'mid' points at the
		 * lower one. The median is then the average between those two
		 * values.
		 */
		mid = start + n_values / 2 - 1;
		median = (stats->sorted[mid] + stats->sorted[mid + 1]) / 2.;

		if (lower_end)
			*lower_end = mid + 1;
		if (upper_start)
			*upper_start = mid + 1;
	}

	return median;
}

/**
 * igt_stats_get_quartiles:
 * @stats: An #igt_stats_t instance
 * @q1: (out): lower or 25th quartile
 * @q2: (out): median or 50th quartile
 * @q3: (out): upper or 75th quartile
 *
 * Retrieves the [quartiles](https://en.wikipedia.org/wiki/Quartile) of the
 * @stats dataset.
 */
void igt_stats_get_quartiles(igt_stats_t *stats,
			     double *q1, double *q2, double *q3)
{
	unsigned int lower_end, upper_start;
	double ret;

	if (stats->n_values < 3) {
		if (q1)
			*q1 = 0.;
		if (q2)
			*q2 = 0.;
		if (q3)
			*q3 = 0.;
		return;
	}

	ret = igt_stats_get_median_internal(stats, 0, stats->n_values,
					    &lower_end, &upper_start);
	if (q2)
		*q2 = ret;

	ret = igt_stats_get_median_internal(stats, 0, lower_end, NULL, NULL);
	if (q1)
		*q1 = ret;

	ret = igt_stats_get_median_internal(stats, upper_start, stats->n_values,
					    NULL, NULL);
	if (q3)
		*q3 = ret;
}

/**
 * igt_stats_get_iqr:
 * @stats: An #igt_stats_t instance
 *
 * Retrieves the
 * [interquartile range](https://en.wikipedia.org/wiki/Interquartile_range)
 * (IQR) of the @stats dataset.
 */
double igt_stats_get_iqr(igt_stats_t *stats)
{
	double q1, q3;

	igt_stats_get_quartiles(stats, &q1, NULL, &q3);
	return (q3 - q1);
}

/**
 * igt_stats_get_median:
 * @stats: An #igt_stats_t instance
 *
 * Retrieves the median of the @stats dataset.
 */
double igt_stats_get_median(igt_stats_t *stats)
{
	return igt_stats_get_median_internal(stats, 0, stats->n_values,
					     NULL, NULL);
}

/*
 * Algorithm popularised by Knuth in:
 *
 * The Art of Computer Programming, volume 2: Seminumerical Algorithms,
 * 3rd edn., p. 232. Boston: Addison-Wesley
 *
 * Source: https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
 */
static void igt_stats_knuth_mean_variance(igt_stats_t *stats)
{
	double mean = 0., m2 = 0.;
	unsigned int i;

	if (stats->mean_variance_valid)
		return;

	for (i = 0; i < stats->n_values; i++) {
		double delta = stats->values[i] - mean;

		mean += delta / (i + 1);
		m2 += delta * (stats->values[i] - mean);
	}

	stats->mean = mean;
	if (stats->n_values > 1 && !stats->is_population)
		stats->variance = m2 / (stats->n_values - 1);
	else
		stats->variance = m2 / stats->n_values;
	stats->mean_variance_valid = true;
}

/**
 * igt_stats_get_mean:
 * @stats: An #igt_stats_t instance
 *
 * Retrieves the mean of the @stats dataset.
 */
double igt_stats_get_mean(igt_stats_t *stats)
{
	igt_stats_knuth_mean_variance(stats);

	return stats->mean;
}

/**
 * igt_stats_get_variance:
 * @stats: An #igt_stats_t instance
 *
 * Retrieves the variance of the @stats dataset.
 */
double igt_stats_get_variance(igt_stats_t *stats)
{
	igt_stats_knuth_mean_variance(stats);

	return stats->variance;
}

/**
 * igt_stats_get_std_deviation:
 * @stats: An #igt_stats_t instance
 *
 * Retrieves the standard deviation of the @stats dataset.
 */
double igt_stats_get_std_deviation(igt_stats_t *stats)
{
	igt_stats_knuth_mean_variance(stats);

	return sqrt(stats->variance);
}
