/*
 * Copyright © 2013 Intel Corporation
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

#include <sys/types.h>
#include <sys/mman.h>
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>

#include "overlay.h"
#include "chart.h"
#include "config.h"
#include "cpu-top.h"
#include "debugfs.h"
#include "gem-interrupts.h"
#include "gem-objects.h"
#include "gpu-freq.h"
#include "gpu-top.h"
#include "gpu-perf.h"
#include "power.h"
#include "rc6.h"

#define is_power_of_two(x)  (((x) & ((x)-1)) == 0)

#define PAD 10
#define HALF_PAD 5
#define SIZE_PAD (PAD + HALF_PAD)

#define IDLE_TIME 30

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
	struct cpu_top cpu_top;
	struct chart busy[MAX_RINGS];
	struct chart wait[MAX_RINGS];
	struct chart cpu;
};

struct overlay_gpu_perf {
	struct gpu_perf gpu_perf;
	time_t show_ctx;
	time_t show_flips;
};

struct overlay_gpu_freq {
	struct gpu_freq gpu_freq;
	struct rc6 rc6;
	struct gem_interrupts irqs;
	struct power power;
	struct chart current;
	struct chart request;
	struct chart power_chart;
	double power_max;
};

struct overlay_gem_objects {
	struct gem_objects gem_objects;
	struct chart aperture;
	struct chart gtt;
	int error;
};

struct overlay_context {
	cairo_surface_t *surface;
	cairo_t *cr;
	int width, height;

	time_t time;

	struct overlay_gpu_top gpu_top;
	struct overlay_gpu_perf gpu_perf;
	struct overlay_gpu_freq gpu_freq;
	struct overlay_gem_objects gem_objects;
};

static void init_gpu_top(struct overlay_context *ctx,
			 struct overlay_gpu_top *gt)
{
	const double rgba[][4] = {
		{ 1, 0.25, 0.25, 1 },
		{ 0.25, 1, 0.25, 1 },
		{ 0.25, 0.25, 1, 1 },
		{ 1, 1, 1, 1 },
	};
	int n;

	cpu_top_init(&gt->cpu_top);
	gpu_top_init(&gt->gpu_top);

	chart_init(&gt->cpu, "CPU", 120);
	chart_set_position(&gt->cpu, PAD, PAD);
	chart_set_size(&gt->cpu, ctx->width/2 - SIZE_PAD, ctx->height/2 - SIZE_PAD);
	chart_set_stroke_rgba(&gt->cpu, 0.75, 0.25, 0.75, 1.);
	chart_set_mode(&gt->cpu, CHART_STROKE);
	chart_set_range(&gt->cpu, 0, 100);

	for (n = 0; n < gt->gpu_top.num_rings; n++) {
		chart_init(&gt->busy[n],
			   gt->gpu_top.ring[n].name,
			   120);
		chart_set_position(&gt->busy[n], PAD, PAD);
		chart_set_size(&gt->busy[n], ctx->width/2 - SIZE_PAD, ctx->height/2 - SIZE_PAD);
		chart_set_stroke_rgba(&gt->busy[n],
				    rgba[n][0], rgba[n][1], rgba[n][2], rgba[n][3]);
		chart_set_mode(&gt->busy[n], CHART_STROKE);
		chart_set_range(&gt->busy[n], 0, 100);
	}

	for (n = 0; n < gt->gpu_top.num_rings; n++) {
		chart_init(&gt->wait[n],
			   gt->gpu_top.ring[n].name,
			   120);
		chart_set_position(&gt->wait[n], PAD, PAD);
		chart_set_size(&gt->wait[n], ctx->width/2 - SIZE_PAD, ctx->height/2 - SIZE_PAD);
		chart_set_fill_rgba(&gt->wait[n],
				    rgba[n][0], rgba[n][1], rgba[n][2], rgba[n][3] * 0.70);
		chart_set_mode(&gt->wait[n], CHART_FILL);
		chart_set_range(&gt->wait[n], 0, 100);
	}
}

static void show_gpu_top(struct overlay_context *ctx, struct overlay_gpu_top *gt)
{
	int y, y1, y2, n, update, len;
	cairo_pattern_t *linear;
	char txt[160];
	int rewind;
	int do_rewind;

	update = gpu_top_update(&gt->gpu_top);

	cairo_rectangle(ctx->cr, PAD-.5, PAD-.5, ctx->width/2-SIZE_PAD+1, ctx->height/2-SIZE_PAD+1);
	cairo_set_source_rgb(ctx->cr, .15, .15, .15);
	cairo_set_line_width(ctx->cr, 1);
	cairo_stroke(ctx->cr);

	if (update && cpu_top_update(&gt->cpu_top) == 0)
		chart_add_sample(&gt->cpu, gt->cpu_top.busy);

	for (n = 0; n < gt->gpu_top.num_rings; n++) {
		if (update)
			chart_add_sample(&gt->wait[n],
					 gt->gpu_top.ring[n].u.u.wait + gt->gpu_top.ring[n].u.u.sema);
		chart_draw(&gt->wait[n], ctx->cr);
	}
	for (n = 0; n < gt->gpu_top.num_rings; n++) {
		if (update)
			chart_add_sample(&gt->busy[n],
					 gt->gpu_top.ring[n].u.u.busy);
		chart_draw(&gt->busy[n], ctx->cr);
	}
	chart_draw(&gt->cpu, ctx->cr);

	y1 = PAD - 2;
	y2 = y1 + (gt->gpu_top.num_rings+1) * 14 + 4;

	cairo_rectangle(ctx->cr, PAD, y1, ctx->width/2-SIZE_PAD, y2-y1);
	linear = cairo_pattern_create_linear(PAD, 0, PAD+ctx->width/2-SIZE_PAD, 0);
	cairo_pattern_add_color_stop_rgba(linear, 0, 0, 0, 0, .5);
	cairo_pattern_add_color_stop_rgba(linear, 1, 0, 0, 0, .0);
	cairo_set_source(ctx->cr, linear);
	cairo_pattern_destroy(linear);
	cairo_fill(ctx->cr);

	y = PAD + 12 - 2;
	cairo_set_source_rgba(ctx->cr, 0.75, 0.25, 0.75, 1.);
	cairo_move_to(ctx->cr, PAD, y);
	rewind = len = sprintf(txt, "CPU: %3d%% busy", gt->cpu_top.busy * gt->cpu_top.nr_cpu);
	do_rewind = 1;
	len += sprintf(txt + len, " (");
	if (gt->cpu_top.nr_cpu > 1) {
		len += sprintf(txt + len, "%s%d cores", do_rewind ? "" : ", ", gt->cpu_top.nr_cpu);
		do_rewind = 0;
	}
	if (gt->cpu_top.nr_running) {
		len += sprintf(txt + len, "%s%d processes", do_rewind ? "" : ", ", gt->cpu_top.nr_running);
		do_rewind = 0;
	}
	sprintf(txt + len, ")");
	if (do_rewind)
		txt[rewind] = '\0';
	cairo_show_text(ctx->cr, txt);
	y += 14;

	for (n = 0; n < gt->gpu_top.num_rings; n++) {
		struct chart *c =&gt->busy[n];

		len = sprintf(txt, "%s: %3d%% busy",
			      gt->gpu_top.ring[n].name,
			      gt->gpu_top.ring[n].u.u.busy);
		if (gt->gpu_top.ring[n].u.u.wait)
			len += sprintf(txt + len, ", %d%% wait",
				       gt->gpu_top.ring[n].u.u.wait);
		if (gt->gpu_top.ring[n].u.u.sema)
			len += sprintf(txt + len, ", %d%% sema",
				       gt->gpu_top.ring[n].u.u.sema);

		cairo_set_source_rgba(ctx->cr,
				      c->stroke_rgb[0],
				      c->stroke_rgb[1],
				      c->stroke_rgb[2],
				      c->stroke_rgb[3]);
		cairo_move_to(ctx->cr, PAD, y);
		cairo_show_text(ctx->cr, txt);
		y += 14;
	}
}

static void init_gpu_perf(struct overlay_context *ctx,
			  struct overlay_gpu_perf *gp)
{
	gpu_perf_init(&gp->gpu_perf, 0);

	gp->show_ctx = 0;
	gp->show_flips = 0;
}

static char *get_comm(pid_t pid, char *comm, int len)
{
	char filename[1024];
	int fd;

	*comm = '\0';
	snprintf(filename, sizeof(filename), "/proc/%d/comm", pid);

	fd = open(filename, 0);
	if (fd >= 0) {
		len = read(fd, comm, len);
		if (len >= 0)
			comm[len-1] = '\0';
		close(fd);
	}

	return comm;
}

static void show_gpu_perf(struct overlay_context *ctx, struct overlay_gpu_perf *gp)
{
	static int last_color;
	const double rgba[][4] = {
		{ 1, 0.25, 0.25, 1 },
		{ 0.25, 1, 0.25, 1 },
		{ 0.25, 0.25, 1, 1 },
		{ 1, 1, 1, 1 },
	};
	struct gpu_perf_comm *comm, **prev;
	const char *ring_name[] = {
		"R",
		"V",
		"B",
	};
	double range[2];
	char buf[1024];
	cairo_pattern_t *linear;
	int x, y, y1, y2, n;
	int has_ctx = 0;
	int has_flips = 0;

	gpu_perf_update(&gp->gpu_perf);

	for (n = 0; n < 4; n++) {
		if (gp->gpu_perf.ctx_switch[n])
			has_ctx = n + 1;
		if (gp->gpu_perf.flip_complete[n])
			has_flips = n + 1;
	}

	cairo_rectangle(ctx->cr, ctx->width/2+HALF_PAD-.5, PAD-.5, ctx->width/2-SIZE_PAD+1, ctx->height/2-SIZE_PAD+1);
	cairo_set_source_rgb(ctx->cr, .15, .15, .15);
	cairo_set_line_width(ctx->cr, 1);
	cairo_stroke(ctx->cr);

	if (gp->gpu_perf.error) {
		cairo_text_extents_t extents;
		cairo_text_extents(ctx->cr, gp->gpu_perf.error, &extents);
		cairo_move_to(ctx->cr,
			      ctx->width/2+HALF_PAD + (ctx->width/2-SIZE_PAD - extents.width)/2.,
			      PAD + (ctx->height/2-SIZE_PAD + extents.height)/2.);
		cairo_show_text(ctx->cr, gp->gpu_perf.error);
		return;
	}

	if (gp->gpu_perf.comm == NULL && (has_ctx|has_flips) == 0) {
		cairo_text_extents_t extents;
		cairo_text_extents(ctx->cr, gp->gpu_perf.error, &extents);
		cairo_move_to(ctx->cr,
			      ctx->width/2+HALF_PAD + (ctx->width/2-SIZE_PAD - extents.width)/2.,
			      PAD + (ctx->height/2-SIZE_PAD + extents.height)/2.);
		cairo_show_text(ctx->cr, "idle");
		return;
	}

	y = PAD + 12 - 2;
	x = ctx->width/2 + HALF_PAD;

	for (comm = gp->gpu_perf.comm; comm; comm = comm->next) {
		int total;

		if (comm->user_data == NULL) {
			comm->user_data = malloc(sizeof(struct chart));
			if (comm->user_data == NULL)
				continue;

			chart_init(comm->user_data, comm->name, 120);
			chart_set_position(comm->user_data, ctx->width/2+HALF_PAD, PAD);
			chart_set_size(comm->user_data, ctx->width/2-SIZE_PAD, ctx->height/2 - SIZE_PAD);
			chart_set_mode(comm->user_data, CHART_STROKE);
			chart_set_stroke_rgba(comm->user_data,
					      rgba[last_color][0],
					      rgba[last_color][1],
					      rgba[last_color][2],
					      rgba[last_color][3]);
			last_color = (last_color + 1) % 4;
			chart_set_stroke_width(comm->user_data, 1);
		}

		total = 0;
		for (n = 0; n < 3; n++)
			total += comm->nr_requests[n];
		chart_add_sample(comm->user_data, total);
	}

	range[0] = range[1] = 0;
	for (comm = gp->gpu_perf.comm; comm; comm = comm->next)
		chart_get_range(comm->user_data, range);

	y2 = y1 = y;
	for (comm = gp->gpu_perf.comm; comm; comm = comm->next) {
		chart_set_range(comm->user_data, range[0], range[1]);
		chart_draw(comm->user_data, ctx->cr);
		y2 += 14;
	}
	if (has_flips || gp->show_flips)
		y2 += 14;
	if (has_ctx || gp->show_ctx)
		y2 += 14;
	y1 += -12 - 2;
	y2 += -14 + 4;

	cairo_rectangle(ctx->cr, x, y1, ctx->width/2-SIZE_PAD, y2-y1);
	linear = cairo_pattern_create_linear(x, 0, x + ctx->width/2-SIZE_PAD, 0);
	cairo_pattern_add_color_stop_rgba(linear, 0, 0, 0, 0, .5);
	cairo_pattern_add_color_stop_rgba(linear, 1, 0, 0, 0, .0);
	cairo_set_source(ctx->cr, linear);
	cairo_pattern_destroy(linear);
	cairo_fill(ctx->cr);

	for (prev = &gp->gpu_perf.comm; (comm = *prev) != NULL; ) {
		int need_comma = 0, len;

		if (comm->name[0] == '\0')
			goto skip_comm;

		len = sprintf(buf, "%s:", comm->name);
		for (n = 0; n < 3; n++) {
			if (comm->nr_requests[n] == 0)
				continue;
			len += sprintf(buf + len, "%s %d%s", need_comma ? "," : "", comm->nr_requests[n], ring_name[n]);
			need_comma = true;
			comm->show = ctx->time;
		}
		if (comm->wait_time) {
			if (comm->wait_time > 1000*1000) {
				len += sprintf(buf + len, "%s %.1fms waits",
					       need_comma ? "," : "",
					       comm->wait_time / (1000*1000.));
			} else if (comm->wait_time > 100) {
				len += sprintf(buf + len, "%s %.1fus waits",
					       need_comma ? "," : "",
					       comm->wait_time / 1000.);
			} else {
				len += sprintf(buf, "%s %.0fns waits",
					       need_comma ? "," : "",
					       (double)comm->wait_time);
			}
			need_comma = true;
			comm->wait_time = 0;
			comm->show = ctx->time;
		}
		if (comm->nr_sema) {
			len += sprintf(buf + len, "%s %d syncs",
				       need_comma ? "," : "",
				       comm->nr_sema);
			need_comma = true;
			comm->nr_sema = 0;
			comm->show = ctx->time;
		}

		if (comm->user_data) {
			struct chart *c = comm->user_data;
			cairo_set_source_rgba(ctx->cr,
					      c->stroke_rgb[0],
					      c->stroke_rgb[1],
					      c->stroke_rgb[2],
					      c->stroke_rgb[3]);
		} else
			cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
		cairo_move_to(ctx->cr, x, y);
		cairo_show_text(ctx->cr, buf);
		y += 14;

skip_comm:
		memset(comm->nr_requests, 0, sizeof(comm->nr_requests));
		if (comm->show < ctx->time - IDLE_TIME ||
		    strcmp(comm->name, get_comm(comm->pid, buf, sizeof(buf)))) {
			*prev = comm->next;
			if (comm->user_data) {
				chart_fini(comm->user_data);
				free(comm->user_data);
			}
			free(comm);
		} else
			prev = &comm->next;
	}

	cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
	cairo_move_to(ctx->cr, x, y);
	if (has_flips) {
		int len = sprintf(buf, "Flips:");
		for (n = 0; n < has_flips; n++)
			len += sprintf(buf + len, "%s %d",
				       n ? "," : "",
				       gp->gpu_perf.flip_complete[n]);
		memset(gp->gpu_perf.flip_complete, 0, sizeof(gp->gpu_perf.flip_complete));
		gp->show_flips = ctx->time;

		cairo_show_text(ctx->cr, buf);
		y += 14;
	} else if (gp->show_flips) {
		cairo_show_text(ctx->cr, "Flips: 0");
		if (ctx->time - gp->show_flips > IDLE_TIME)
			gp->show_flips = 0;
		y += 14;
	}

	cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
	cairo_move_to(ctx->cr, x, y);
	if (has_ctx) {
		int len = sprintf(buf, "Contexts:");
		for (n = 0; n < has_ctx; n++)
			len += sprintf(buf + len, "%s %d",
				       n ? "," : "",
				       gp->gpu_perf.ctx_switch[n]);

		memset(gp->gpu_perf.ctx_switch, 0, sizeof(gp->gpu_perf.ctx_switch));
		gp->show_ctx = ctx->time;

		cairo_show_text(ctx->cr, buf);
		y += 14;
	} else if (gp->show_ctx) {
		cairo_show_text(ctx->cr, "Contexts: 0");
		y += 14;
		if (ctx->time - gp->show_ctx > IDLE_TIME)
			gp->show_ctx = 0;
	}
}

static void init_gpu_freq(struct overlay_context *ctx,
			  struct overlay_gpu_freq *gf)
{
	if (gpu_freq_init(&gf->gpu_freq) == 0) {
		chart_init(&gf->current, "current", 120);
		chart_set_position(&gf->current, PAD, ctx->height/2 + HALF_PAD);
		chart_set_size(&gf->current, ctx->width/2 - SIZE_PAD, ctx->height/2 - SIZE_PAD);
		chart_set_stroke_rgba(&gf->current, 0.75, 0.25, 0.50, 1.);
		chart_set_mode(&gf->current, CHART_STROKE);
		chart_set_smooth(&gf->current, CHART_LINE);
		chart_set_range(&gf->current, 0, gf->gpu_freq.max);

		chart_init(&gf->request, "request", 120);
		chart_set_position(&gf->request, PAD, ctx->height/2 + HALF_PAD);
		chart_set_size(&gf->request, ctx->width/2 - SIZE_PAD, ctx->height/2 - SIZE_PAD);
		chart_set_fill_rgba(&gf->request, 0.25, 0.25, 0.50, 1.);
		chart_set_mode(&gf->request, CHART_FILL);
		chart_set_smooth(&gf->request, CHART_LINE);
		chart_set_range(&gf->request, 0, gf->gpu_freq.max);
	}

	if (power_init(&gf->power) == 0) {
		chart_init(&gf->power_chart, "power", 120);
		chart_set_position(&gf->power_chart, PAD, ctx->height/2 + HALF_PAD);
		chart_set_size(&gf->power_chart, ctx->width/2 - SIZE_PAD, ctx->height/2 - SIZE_PAD);
		chart_set_stroke_rgba(&gf->power_chart, 0.45, 0.55, 0.45, 1.);
		gf->power_max = 0;
	}

	rc6_init(&gf->rc6);
	gem_interrupts_init(&gf->irqs);
}

static void show_gpu_freq(struct overlay_context *ctx, struct overlay_gpu_freq *gf)
{
	char buf[160];
	int y1, y2, y, len;

	int has_freq = gpu_freq_update(&gf->gpu_freq) == 0;
	int has_rc6 = rc6_update(&gf->rc6) == 0;
	int has_power = power_update(&gf->power) == 0;
	int has_irqs = gem_interrupts_update(&gf->irqs) == 0;
	cairo_pattern_t *linear;

	cairo_rectangle(ctx->cr, PAD-.5, ctx->height/2+HALF_PAD-.5, ctx->width/2-SIZE_PAD+1, ctx->height/2-SIZE_PAD+1);
	cairo_set_source_rgb(ctx->cr, .15, .15, .15);
	cairo_set_line_width(ctx->cr, 1);
	cairo_stroke(ctx->cr);

	if (gf->gpu_freq.error) {
		const char *txt = "GPU frequency not found in debugfs";
		cairo_text_extents_t extents;
		cairo_text_extents(ctx->cr, txt, &extents);
		cairo_move_to(ctx->cr,
			      PAD + (ctx->width/2-SIZE_PAD - extents.width)/2.,
			      ctx->height/2+HALF_PAD + (ctx->height/2-SIZE_PAD + extents.height)/2.);
		cairo_show_text(ctx->cr, txt);
		return;
	}

	if (has_freq) {
		if (gf->gpu_freq.current)
			chart_add_sample(&gf->current, gf->gpu_freq.current);
		if (gf->gpu_freq.request)
			chart_add_sample(&gf->request, gf->gpu_freq.request);

		chart_draw(&gf->request, ctx->cr);
		chart_draw(&gf->current, ctx->cr);
	}

	if (has_power) {
		chart_add_sample(&gf->power_chart, gf->power.power_mW);
		if (gf->power.new_sample) {
			if (gf->power.power_mW > gf->power_max)
				gf->power_max = gf->power.power_mW;
			chart_set_range(&gf->power_chart, 0, gf->power_max);
			gf->power.new_sample = 0;
		}
		chart_draw(&gf->power_chart, ctx->cr);
	}

	y = ctx->height/2 + HALF_PAD + 12 - 2;

	y1 = y2 = y;
	if (has_freq) {
		y2 += 12;
		y2 += 12;
	}
	if (has_rc6)
		y2 += 14;
	if (has_power)
		y2 += 14;
	if (has_irqs)
		y2 += 14;
	y1 += -12 - 2;
	y2 += -14 + 4;

	cairo_rectangle(ctx->cr, PAD, y1, ctx->width/2-SIZE_PAD, y2-y1);
	linear = cairo_pattern_create_linear(PAD, 0, PAD+ctx->width/2-SIZE_PAD, 0);
	cairo_pattern_add_color_stop_rgba(linear, 0, 0, 0, 0, .5);
	cairo_pattern_add_color_stop_rgba(linear, 1, 0, 0, 0, .0);
	cairo_set_source(ctx->cr, linear);
	cairo_pattern_destroy(linear);
	cairo_fill(ctx->cr);

	if (has_freq) {
		cairo_text_extents_t extents;

		len = sprintf(buf, "Frequency: %dMHz", gf->gpu_freq.current);
		if (gf->gpu_freq.request)
		cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
			sprintf(buf + len, " (requested %dMHz)", gf->gpu_freq.request);
		cairo_move_to(ctx->cr, PAD, y);
		cairo_show_text(ctx->cr, buf);
		y += 12;

		cairo_text_extents(ctx->cr, "Frequency: ", &extents);

		cairo_set_font_size(ctx->cr, 8);
		sprintf(buf, " min: %dMHz, max: %dMHz", gf->gpu_freq.min, gf->gpu_freq.max);
		cairo_set_source_rgba(ctx->cr, .8, .8, .8, 1);
		cairo_move_to(ctx->cr, PAD + extents.width, y);
		cairo_show_text(ctx->cr, buf);
		cairo_set_font_size(ctx->cr, 10);
		y += 12;
	}

	if (has_rc6) {
		len = sprintf(buf, "RC6: %d%%", gf->rc6.rc6_combined);
		cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
		cairo_move_to(ctx->cr, PAD, y);
		if (gf->rc6.rc6_combined) {
			int need_comma = 0;
			int rewind = len;
			len += sprintf(buf + len, " (");
			if (gf->rc6.rc6) {
				len += sprintf(buf + len, "%src6=%d%%",
					       need_comma ? ", " : "",
					       gf->rc6.rc6);
				need_comma++;
			}
			if (gf->rc6.rc6p) {
				len += sprintf(buf + len, "%src6p=%d%%",
					       need_comma ? ", " : "",
					       gf->rc6.rc6p);
				need_comma++;
			}
			if (gf->rc6.rc6pp) {
				len += sprintf(buf + len, "%src6pp=%d%%",
					       need_comma ? ", " : "",
					       gf->rc6.rc6pp);
				need_comma++;
			}
			sprintf(buf + len, ")");
			if (need_comma <= 1)
				buf[rewind] = '\0';
		}
		cairo_show_text(ctx->cr, buf);
		y += 14;
	}

	if (has_power) {
		sprintf(buf, "Power: %llumW", (long long unsigned)gf->power.power_mW);
		cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
		cairo_move_to(ctx->cr, PAD, y);
		cairo_show_text(ctx->cr, buf);
		y += 14;
	}

	if (has_irqs) {
		sprintf(buf, "Interrupts: %llu", (long long unsigned)gf->irqs.delta);
		cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
		cairo_move_to(ctx->cr, PAD, y);
		cairo_show_text(ctx->cr, buf);
		y += 14;
	}
}

static void init_gem_objects(struct overlay_context *ctx,
			     struct overlay_gem_objects *go)
{
	go->error = gem_objects_init(&go->gem_objects);
	if (go->error)
		return;

	chart_init(&go->aperture, "aperture", 120);
	chart_set_position(&go->aperture, ctx->width/2+HALF_PAD, ctx->height/2 + HALF_PAD);
	chart_set_size(&go->aperture, ctx->width/2 - SIZE_PAD, ctx->height/2 - SIZE_PAD);
	chart_set_stroke_rgba(&go->aperture, 0.75, 0.25, 0.50, 1.);
	chart_set_mode(&go->aperture, CHART_STROKE);
	chart_set_range(&go->aperture, 0, go->gem_objects.max_gtt);

	chart_init(&go->gtt, "gtt", 120);
	chart_set_position(&go->gtt, ctx->width/2+HALF_PAD, ctx->height/2 + HALF_PAD);
	chart_set_size(&go->gtt, ctx->width/2 - SIZE_PAD, ctx->height/2 - SIZE_PAD);
	chart_set_fill_rgba(&go->gtt, 0.25, 0.5, 0.5, 1.);
	chart_set_mode(&go->gtt, CHART_FILL);
	chart_set_range(&go->gtt, 0, go->gem_objects.max_gtt);
}

static void show_gem_objects(struct overlay_context *ctx, struct overlay_gem_objects *go)
{
	struct gem_objects_comm *comm;
	char buf[160];
	cairo_pattern_t *linear;
	int x, y, y1, y2;

	if (go->error == 0)
		go->error = gem_objects_update(&go->gem_objects);
	if (go->error)
		return;

	cairo_rectangle(ctx->cr, ctx->width/2+HALF_PAD-.5, ctx->height/2+HALF_PAD-.5, ctx->width/2-SIZE_PAD+1, ctx->height/2-SIZE_PAD+1);
	cairo_set_source_rgb(ctx->cr, .15, .15, .15);
	cairo_set_line_width(ctx->cr, 1);
	cairo_stroke(ctx->cr);

	chart_add_sample(&go->gtt, go->gem_objects.total_gtt);
	chart_add_sample(&go->aperture, go->gem_objects.total_aperture);

	chart_draw(&go->gtt, ctx->cr);
	chart_draw(&go->aperture, ctx->cr);


	y = ctx->height/2 + HALF_PAD + 12 - 2;
	x = ctx->width/2 + HALF_PAD;

	y2 = y1 = y;
	y2 += 14;
	for (comm = go->gem_objects.comm; comm; comm = comm->next) {
		if ((comm->bytes >> 20) == 0)
			break;
		y2 += 12;
	}
	y1 += -12 - 2;
	y2 += -12 + 4;

	cairo_rectangle(ctx->cr, x, y1, ctx->width/2-SIZE_PAD, y2-y1);
	linear = cairo_pattern_create_linear(x, 0, x+ctx->width/2-SIZE_PAD, 0);
	cairo_pattern_add_color_stop_rgba(linear, 0, 0, 0, 0, .5);
	cairo_pattern_add_color_stop_rgba(linear, 1, 0, 0, 0, .0);
	cairo_set_source(ctx->cr, linear);
	cairo_pattern_destroy(linear);
	cairo_fill(ctx->cr);

	sprintf(buf, "Total: %ldMB, %ld objects",
		go->gem_objects.total_bytes >> 20, go->gem_objects.total_count);
	cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
	cairo_move_to(ctx->cr, x, y);
	cairo_show_text(ctx->cr, buf);
	y += 12;

	cairo_set_source_rgba(ctx->cr, .8, .8, .8, 1);
	cairo_set_font_size(ctx->cr, 8);
	for (comm = go->gem_objects.comm; comm; comm = comm->next) {
		if ((comm->bytes >> 20) == 0)
			break;

		sprintf(buf, "%s %ldMB, %ld objects",
			comm->name, comm->bytes >> 20, comm->count);
		cairo_move_to(ctx->cr, x, y);
		cairo_show_text(ctx->cr, buf);
		y += 12;
	}
}

static int take_snapshot;

static void signal_snapshot(int sig)
{
	take_snapshot = sig;
}

static int get_sample_period(struct config *config)
{
	const char *value;

	value = config_get_value(config, "sampling", "period");
	if (value && atoi(value) > 0)
		return atoi(value);

	value = config_get_value(config, "sampling", "frequency");
	if (value && atoi(value) > 0)
		return 1000000 / atoi(value);

	return 500000;
}

static void overlay_snapshot(struct overlay_context *ctx)
{
	char buf[1024];
	sprintf(buf, "/tmp/overlay-snapshot-%ld.png", (long)time(NULL));
	cairo_surface_write_to_png(ctx->surface, buf);
}

static void usage(const char *progname)
{
	printf("intel-gpu-overlay -- realtime display of GPU statistics\n");
	printf("Usage: %s [options]\n", progname);
	printf("\t--config|-c <string> | <filename>\t\t\tSpecify an ini-style configuration string or file\n");
	printf("\t--geometry|-G <width>x<height>+<x-offset>+<y-offset>\tExact window placement and size\n");
	printf("\t--position|-P (top|middle|bottom)-(left|centre|right)\tPlace the window in a particular corner\n");
	printf("\t--size|-S <width>x<height> | <scale>%%\t\t\tWindow size\n");
	printf("\t--help|-h\t\t\t\t\t\tThis help message\n");
}

int main(int argc, char **argv)
{
	static struct option long_options[] = {
		{"config", 1, 0, 'c'},
		{"geometry", 1, 0, 'G'},
		{"position", 1, 0, 'P'},
		{"size", 1, 0, 'S'},
		{"help", 0, 0, 'h'},
		{NULL, 0, 0, 0,}
	};
	struct overlay_context ctx;
	struct config config;
	int index, sample_period;
	int daemonize = 1, renice = 0;
	int i;

	config_init(&config);

	opterr = 0;
	while ((i = getopt_long(argc, argv, "c:fhn?", long_options, &index)) != -1) {
		switch (i) {
		case 'c':
			config_parse_string(&config, optarg);
			break;
		case 'G':
			config_set_value(&config, "window", "geometry", optarg);
			break;
		case 'P':
			config_set_value(&config, "window", "position", optarg);
			break;
		case 'S':
			config_set_value(&config, "window", "size", optarg);
			break;
		case 'f':
			daemonize = 0;
			break;
		case 'n':
			renice = -20;
			if (optarg)
				renice = atoi(optarg);
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		}
	}

	if (argc > optind) {
		x11_overlay_stop();
		return 0;
	}

	ctx.width = 640;
	ctx.height = 236;
	ctx.surface = NULL;
	if (ctx.surface == NULL)
		ctx.surface = x11_overlay_create(&config, &ctx.width, &ctx.height);
	if (ctx.surface == NULL)
		ctx.surface = x11_window_create(&config, &ctx.width, &ctx.height);
	if (ctx.surface == NULL)
		ctx.surface = kms_overlay_create(&config, &ctx.width, &ctx.height);
	if (ctx.surface == NULL)
		return ENOMEM;

	if (daemonize && daemon(0, 0))
		return EINVAL;

	if (renice)
		nice(renice);

	signal(SIGUSR1, signal_snapshot);

	debugfs_init();

	init_gpu_top(&ctx, &ctx.gpu_top);
	init_gpu_perf(&ctx, &ctx.gpu_perf);
	init_gpu_freq(&ctx, &ctx.gpu_freq);
	init_gem_objects(&ctx, &ctx.gem_objects);

	sample_period = get_sample_period(&config);

	i = 0;
	while (1) {
		ctx.time = time(NULL);

		ctx.cr = cairo_create(ctx.surface);
		cairo_set_operator(ctx.cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint(ctx.cr);
		cairo_set_operator(ctx.cr, CAIRO_OPERATOR_OVER);

		show_gpu_top(&ctx, &ctx.gpu_top);
		show_gpu_perf(&ctx, &ctx.gpu_perf);
		show_gpu_freq(&ctx, &ctx.gpu_freq);
		show_gem_objects(&ctx, &ctx.gem_objects);

		{
			char buf[80];
			cairo_text_extents_t extents;
			gethostname(buf, sizeof(buf));
			cairo_set_source_rgb(ctx.cr, .5, .5, .5);
			cairo_set_font_size(ctx.cr, PAD-2);
			cairo_text_extents(ctx.cr, buf, &extents);
			cairo_move_to(ctx.cr,
				      (ctx.width-extents.width)/2.,
				      1+extents.height);
			cairo_show_text(ctx.cr, buf);
		}

		cairo_destroy(ctx.cr);

		overlay_show(ctx.surface);

		if (take_snapshot) {
			overlay_snapshot(&ctx);
			take_snapshot = 0;
		}

		usleep(sample_period);
	}

	return 0;
}
