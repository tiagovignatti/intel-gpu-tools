struct chart {
	const char *name;
	int x, y, w, h;
	int num_samples;
	int current_sample;
	int range_automatic;
	enum chart_mode {
		CHART_STROKE = 0,
		CHART_FILL,
		CHART_FILL_STROKE,
	} mode;
	enum chart_smooth {
		CHART_LINE = 0,
		CHART_CURVE,
	} smooth;
	float fill_rgb[4];
	float stroke_rgb[4];
	double stroke_width;
	double range[2];
	double *samples;
};

int chart_init(struct chart *chart, const char *name, int num_samples);
void chart_set_mode(struct chart *chart, enum chart_mode mode);
void chart_set_smooth(struct chart *chart, enum chart_smooth smooth);
void chart_set_fill_rgba(struct chart *chart, float red, float green, float blue, float alpha);
void chart_set_stroke_width(struct chart *chart, float width);
void chart_set_stroke_rgba(struct chart *chart, float red, float green, float blue, float alpha);
void chart_set_position(struct chart *chart, int x, int y);
void chart_set_size(struct chart *chart, int w, int h);
void chart_set_range(struct chart *chart, double min, double max);
void chart_add_sample(struct chart *chart, double value);
void chart_draw(struct chart *chart, cairo_t *cr);
void chart_fini(struct chart *chart);

void chart_get_range(struct chart *chart, double *range);
