#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <cairo.h>

#include <stdio.h>

#include "chart.h"

int chart_init(struct chart *chart, const char *name, int num_samples)
{
	memset(chart, 0, sizeof(*chart));
	chart->name = name;
	chart->samples = malloc(sizeof(*chart->samples)*num_samples);
	if (chart->samples == NULL)
		return ENOMEM;

	chart->num_samples = num_samples;
	chart->range_automatic = 1;
	return 0;
}

void chart_set_rgba(struct chart *chart, float red, float green, float blue, float alpha)
{
	chart->rgb[0] = red;
	chart->rgb[1] = green;
	chart->rgb[2] = blue;
	chart->rgb[3] = alpha;
}

void chart_set_position(struct chart *chart, int x, int y)
{
	chart->x = x;
	chart->y = y;
}

void chart_set_size(struct chart *chart, int w, int h)
{
	chart->w = w;
	chart->h = h;
}

void chart_set_range(struct chart *chart, double min, double max)
{
	chart->range[0] = min;
	chart->range[1] = max;
	chart->range_automatic = 0;
}

void chart_add_sample(struct chart *chart, double value)
{
	int pos;

	pos = chart->current_sample % chart->num_samples;
	chart->samples[pos] = value;
	chart->current_sample++;
}

static void chart_update_range(struct chart *chart)
{
	int n, max = chart->current_sample;
	if (max > chart->num_samples)
		max = chart->num_samples;
	chart->range[0] = chart->range[1] = chart->samples[0];
	for (n = 1; n < max; n++) {
		if (chart->samples[n] < chart->range[0])
			chart->range[0] = chart->samples[n];
		else if (chart->samples[n] > chart->range[1])
			chart->range[1] = chart->samples[n];
	}
}

static double value_at(struct chart *chart, int n)
{
	if (n <= chart->current_sample - chart->num_samples)
		n = chart->current_sample;
	else if (n >= chart->current_sample)
		n = chart->current_sample - 1;

	return chart->samples[n % chart->num_samples];
}

static double gradient_at(struct chart *chart, int n)
{
	double y0, y1;

	y0 = value_at(chart, n-1);
	y1 = value_at(chart, n+1);

	return (y1 - y0) / 2.;
}

void chart_draw(struct chart *chart, cairo_t *cr)
{
	int i, n, max, x;

	if (chart->current_sample == 0)
		return;

	if (chart->range_automatic)
		chart_update_range(chart);

	cairo_save(cr);

	cairo_translate(cr, chart->x, chart->y + chart->h);
	cairo_scale(cr,
		    chart->w / (double)chart->num_samples,
		    -chart->h / (chart->range[1] - chart->range[0]));

	x = 0;
	max = chart->current_sample;
	if (max >= chart->num_samples) {
		max = chart->num_samples;
		i = chart->current_sample - max;
	} else {
		i = 0;
		x = chart->num_samples - max;
	}
	cairo_translate(cr, x, -chart->range[0]);

	cairo_new_path(cr);
	for (n = 0; n < max; n++) {
		cairo_curve_to(cr,
			       n-2/3., value_at(chart, i + n -1) + gradient_at(chart, i + n - 1)/3.,
			       n-1/3., value_at(chart, i + n) - gradient_at(chart, i + n)/3.,
			       n, value_at(chart, i + n));
	}

	cairo_identity_matrix(cr);
	cairo_set_line_width(cr, 1);
	cairo_set_source_rgba(cr, chart->rgb[0], chart->rgb[1], chart->rgb[2], chart->rgb[3]);
	cairo_stroke(cr);

	cairo_restore(cr);
}
