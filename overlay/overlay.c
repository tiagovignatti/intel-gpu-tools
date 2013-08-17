#include <X11/Xlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "overlay.h"
#include "gpu-top.h"
#include "gem-objects.h"
#include "chart.h"

const cairo_user_data_key_t overlay_key;

static void overlay_show(cairo_surface_t *surface)
{
	struct overlay *overlay;

	overlay = cairo_surface_get_user_data(surface, &overlay_key);
	if (overlay == NULL)
		return;

	overlay->show(overlay);
}

#if 0
static void overlay_position(cairo_surface_t *surface, enum position p)
{
	struct overlay *overlay;

	overlay = cairo_surface_get_user_data(surface, &overlay_key);
	if (overlay == NULL)
		return;

	overlay->position(overlay, p);
}

static void overlay_hide(cairo_surface_t *surface)
{
	struct overlay *overlay;

	overlay = cairo_surface_get_user_data(surface, &overlay_key);
	if (overlay == NULL)
		return;

	overlay->hide(overlay);
}
#endif

struct overlay_gpu_top {
	struct gpu_top gpu_top;
	struct chart chart[MAX_RINGS];
};

static void init_gpu_top(struct overlay_gpu_top *gt,
			 cairo_surface_t *surface)
{
	const double rgba[][4] = {
		{ 1, 0, 0, 1 },
		{ 0, 1, 0, 1 },
		{ 0, 0, 1, 1 },
		{ 1, 1, 1, 1 },
	};
	int n;

	gpu_top_init(&gt->gpu_top);

	for (n = 0; n < gt->gpu_top.num_rings; n++) {
		chart_init(&gt->chart[n],
			   gt->gpu_top.ring[n].name,
			   120);
		chart_set_position(&gt->chart[n], 12, 12);
		chart_set_size(&gt->chart[n],
			       cairo_image_surface_get_width(surface)-24,
			       100);
		chart_set_rgba(&gt->chart[n],
			       rgba[n][0], rgba[n][1], rgba[n][2], rgba[n][3]);
		chart_set_range(&gt->chart[n], 0, 100);
	}
}

static void show_gpu_top(cairo_t *cr, struct overlay_gpu_top *gt)
{
	int y, n, update;

	update = gpu_top_update(&gt->gpu_top);
	for (n = 0; n < gt->gpu_top.num_rings; n++) {
		if (update)
			chart_add_sample(&gt->chart[n],
					 gt->gpu_top.ring[n].u.u.busy);
		chart_draw(&gt->chart[n], cr);
	}

	cairo_set_source_rgb(cr, 1, 1, 1);

	y = 12;
	for (n = 0; n < gt->gpu_top.num_rings; n++) {
		char txt[160];
		int len;

		len = sprintf(txt, "%s: %d%% busy",
			      gt->gpu_top.ring[n].name,
			      gt->gpu_top.ring[n].u.u.busy);
		if (gt->gpu_top.ring[n].u.u.wait)
			len += sprintf(txt + len, ", %d%% wait",
				       gt->gpu_top.ring[n].u.u.wait);
		if (gt->gpu_top.ring[n].u.u.sema)
			len += sprintf(txt + len, ", %d%% sema",
				       gt->gpu_top.ring[n].u.u.sema);

		cairo_move_to(cr, 12, y);
		cairo_show_text(cr, txt);
		y += 14;
	}
}

static void show_gem_objects(cairo_t *cr)
{
	char gem_objects[1024], *s, *t, *end;
	int len, y;

	len = gem_objects_update(gem_objects, sizeof(gem_objects));
	if (len <= 0)
		return;

	y = 130;

	s = gem_objects;
	end = s + len - 1;
	while (s < end) {
		t = strchr(s, '\n');
		if (t == NULL)
			t = end;
		*t = '\0';

		cairo_move_to(cr, 12, y);
		cairo_show_text(cr, s);
		y += 14;

		s = t+1;
	}
}

int main(int argc, char **argv)
{
	cairo_surface_t *surface;
	struct overlay_gpu_top gpu_top;
	int i = 0;

	if (argc > 1) {
		x11_overlay_stop();
		return 0;
	}

	surface = x11_overlay_create(POS_TOP_RIGHT, 640, 480);
	if (surface == NULL)
		return ENOMEM;

	init_gpu_top(&gpu_top, surface);

	while (1) {
		cairo_t *cr;

		usleep(500*1000);

		cr = cairo_create(surface);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

		{
			char buf[80];
			cairo_text_extents_t extents;
			sprintf(buf, "%d", i++);
			cairo_set_source_rgb(cr, .5, .5, .5);
			cairo_text_extents(cr, buf, &extents);
			cairo_move_to(cr,
				      cairo_image_surface_get_width(surface)-extents.width-6,
				      6+extents.height);
			cairo_show_text(cr, buf);
		}

		show_gpu_top(cr, &gpu_top);
		show_gem_objects(cr);

		cairo_destroy(cr);

		overlay_show(surface);
	}

	return 0;
}
