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
#include <string.h>

#include "igt_core.h"
#include "igt_stats.h"

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

/**
 * igt_stats_init:
 * @stats: An #igt_stats_t instance
 * @capacity: Number of data samples @stats can contain
 *
 * Initializes an #igt_stats_t instance to hold @capacity samples.
 * igt_stats_fini() must be called once finished with @stats.
 *
 * We currently assume the user knows how many data samples upfront and there's
 * no need to grow the array of values.
 */
void igt_stats_init(igt_stats_t *stats, unsigned int capacity)
{
	memset(stats, 0, sizeof(*stats));

	stats->values = calloc(capacity, sizeof(*stats->values));
	igt_assert(stats->values);
	stats->capacity = capacity;
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
	igt_assert(stats->n_values < stats->capacity);
	stats->values[stats->n_values++] = value;
	stats->mean_variance_valid = false;
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
