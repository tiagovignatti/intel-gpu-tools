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

#include "overlay.h"
#include "chart.h"
#include "cpu-top.h"
#include "debugfs.h"
#include "gem-objects.h"
#include "gpu-freq.h"
#include "gpu-top.h"
#include "gpu-perf.h"
#include "power.h"
#include "rc6.h"

#define is_power_of_two(x)  (((x) & ((x)-1)) == 0)

const cairo_user_data_key_t overlay_key;

static void overlay_show(cairo_surface_t *surface)
{
	struct overlay *overlay;

	cairo_surface_flush(surface);

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
};

struct overlay_gpu_freq {
	struct gpu_freq gpu_freq;
	struct rc6 rc6;
	struct power power;
	struct chart current;
	struct chart request;
	struct chart power_chart;
	double power_range[2];
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

	gpu_top_init(&gt->gpu_top);
	memset(&gt->cpu, 0, sizeof(gt->cpu));

	chart_init(&gt->cpu, "CPU", 120);
	chart_set_position(&gt->cpu, 12, 12);
	chart_set_size(&gt->cpu, ctx->width/2 - 18, ctx->height/2 - 18);
	chart_set_stroke_rgba(&gt->cpu, 0.75, 0.25, 0.75, 1.);
	chart_set_mode(&gt->cpu, CHART_STROKE);
	chart_set_range(&gt->cpu, 0, 100);

	for (n = 0; n < gt->gpu_top.num_rings; n++) {
		chart_init(&gt->busy[n],
			   gt->gpu_top.ring[n].name,
			   120);
		chart_set_position(&gt->busy[n], 12, 12);
		chart_set_size(&gt->busy[n], ctx->width/2 - 18, ctx->height/2 - 18);
		chart_set_stroke_rgba(&gt->busy[n],
				    rgba[n][0], rgba[n][1], rgba[n][2], rgba[n][3]);
		chart_set_mode(&gt->busy[n], CHART_STROKE);
		chart_set_range(&gt->busy[n], 0, 100);
	}

	for (n = 0; n < gt->gpu_top.num_rings; n++) {
		chart_init(&gt->wait[n],
			   gt->gpu_top.ring[n].name,
			   120);
		chart_set_position(&gt->wait[n], 12, 12);
		chart_set_size(&gt->wait[n], ctx->width/2 - 18, ctx->height/2 - 18);
		chart_set_fill_rgba(&gt->wait[n],
				    rgba[n][0], rgba[n][1], rgba[n][2], rgba[n][3] * 0.70);
		chart_set_mode(&gt->wait[n], CHART_FILL);
		chart_set_range(&gt->wait[n], 0, 100);
	}
}

static void show_gpu_top(struct overlay_context *ctx, struct overlay_gpu_top *gt)
{
	int y, n, update, len;
	char txt[160];

	update = gpu_top_update(&gt->gpu_top);

	if (update && cpu_top_update(&gt->cpu_top) == 0)
		chart_add_sample(&gt->cpu, gt->cpu_top.busy);
	chart_draw(&gt->cpu, ctx->cr);

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

	cairo_set_source_rgb(ctx->cr, 1, 1, 1);

	y = 12 + 12 - 2;
	cairo_set_source_rgba(ctx->cr, 0.75, 0.25, 0.75, 1.);
	cairo_move_to(ctx->cr, 12, y);
	sprintf(txt, "CPU: %d%% busy", gt->cpu_top.busy);
	cairo_show_text(ctx->cr, txt);
	y += 14;

	for (n = 0; n < gt->gpu_top.num_rings; n++) {
		struct chart *c =&gt->busy[n];

		len = sprintf(txt, "%s: %d%% busy",
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
		cairo_move_to(ctx->cr, 12, y);
		cairo_show_text(ctx->cr, txt);
		y += 14;
	}
}

static void init_gpu_perf(struct overlay_context *ctx,
			  struct overlay_gpu_perf *gp)
{
	gpu_perf_init(&gp->gpu_perf, 0);
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
		"render",
		"video",
		"blt",
	};
	double range[2];
	char buf[1024];
	int x, y, n;

	gpu_perf_update(&gp->gpu_perf);

	y = 12 + 12 - 2;
	x = ctx->width/2 + 12;

	for (comm = gp->gpu_perf.comm; comm; comm = comm->next) {
		int total;

		if (comm->user_data == NULL) {
			comm->user_data = malloc(sizeof(struct chart));
			if (comm->user_data == NULL)
				continue;

			chart_init(comm->user_data, comm->name, 120);
			chart_set_position(comm->user_data, ctx->width/2+6, 12);
			chart_set_size(comm->user_data, ctx->width/2-18, ctx->height/2 - 18);
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
	for (comm = gp->gpu_perf.comm; comm; comm = comm->next) {
		chart_set_range(comm->user_data, range[0], range[1]);
		chart_draw(comm->user_data, ctx->cr);
	}

	for (prev = &gp->gpu_perf.comm; (comm = *prev) != NULL; ) {
		int need_comma = 0;

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
		sprintf(buf, "%s:", comm->name);
		cairo_show_text(ctx->cr, buf);
		for (n = 0; n < 3; n++) {
			if (comm->nr_requests[n] == 0)
				continue;
			sprintf(buf, "%s %d %s", need_comma ? "," : "", comm->nr_requests[n], ring_name[n]);
			cairo_show_text(ctx->cr, buf);
			need_comma = true;
		}
		if (comm->wait_time) {
			buf[0] = '\0';
			if (comm->wait_time > 1000*1000) {
				sprintf(buf, "%s %.1f ms waiting",
					need_comma ? "," : "",
					comm->wait_time / (1000*1000.));
			} else if (comm->wait_time > 100) {
				sprintf(buf, "%s %.1f us waiting",
					need_comma ? "," : "",
					comm->wait_time / 1000.);
			} else {
				sprintf(buf, "%s %.0f ns waiting",
					need_comma ? "," : "",
					(double)comm->wait_time);
			}
			if (buf[0] != '\0') {
				cairo_show_text(ctx->cr, buf);
				need_comma = true;
			}
			comm->wait_time = 0;
		}
		if (comm->busy_time) {
			buf[0] = '\0';
			if (comm->busy_time > 1000*1000) {
				sprintf(buf, "%s %.1f ms busy",
					need_comma ? "," : "",
					comm->busy_time / (1000*1000.));
			} else if (comm->busy_time > 100) {
				sprintf(buf, "%s %.1f us busy",
					need_comma ? "," : "",
					comm->busy_time / 1000.);
			} else {
				sprintf(buf, "%s %.0f ns busy",
					need_comma ? "," : "",
					(double)comm->busy_time);
			}
			if (buf[0] != '\0') {
				cairo_show_text(ctx->cr, buf);
				need_comma = true;
			}
			comm->busy_time = 0;
		}
		y += 14;

		memset(comm->nr_requests, 0, sizeof(comm->nr_requests));
		if (strcmp(comm->name, get_comm(comm->pid, buf, sizeof(buf)))) {
			*prev = comm->next;
			if (comm->user_data) {
				chart_fini(comm->user_data);
				free(comm->user_data);
			}
			free(comm);
		} else
			prev = &comm->next;
	}

	{
		int has_flips = 0, len;
		for (n = 0; n < 4; n++) {
			if (gp->gpu_perf.flip_complete[n])
				has_flips = n + 1;
		}
		if (has_flips) {
			len = sprintf(buf, "Flips:");
			for (n = 0; n < has_flips; n++)
				len += sprintf(buf + len, "%s %d",
					       n ? "," : "",
					       gp->gpu_perf.flip_complete[n]);
		} else {
			sprintf(buf, "Flips: 0");
		}
		memset(gp->gpu_perf.flip_complete, 0, sizeof(gp->gpu_perf.flip_complete));
	}
	cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
	cairo_move_to(ctx->cr, x, y);
	cairo_show_text(ctx->cr, buf);
	y += 14;
}

static void init_gpu_freq(struct overlay_context *ctx,
			  struct overlay_gpu_freq *gf)
{
	if (gpu_freq_init(&gf->gpu_freq) == 0) {
		chart_init(&gf->current, "current", 120);
		chart_set_position(&gf->current, 12, ctx->height/2 + 6);
		chart_set_size(&gf->current, ctx->width/2 - 18, ctx->height/2 - 18);
		chart_set_stroke_rgba(&gf->current, 0.75, 0.25, 0.50, 1.);
		chart_set_mode(&gf->current, CHART_STROKE);
		chart_set_smooth(&gf->current, CHART_LINE);
		chart_set_range(&gf->current, 0, gf->gpu_freq.max);

		chart_init(&gf->request, "request", 120);
		chart_set_position(&gf->request, 12, ctx->height/2 + 6);
		chart_set_size(&gf->request, ctx->width/2 - 18, ctx->height/2 - 18);
		chart_set_fill_rgba(&gf->request, 0.25, 0.25, 0.50, 1.);
		chart_set_mode(&gf->request, CHART_FILL);
		chart_set_smooth(&gf->request, CHART_LINE);
		chart_set_range(&gf->request, 0, gf->gpu_freq.max);
	}

	if (power_init(&gf->power) == 0) {
		chart_init(&gf->power_chart, "power", 120);
		chart_set_position(&gf->power_chart, 12, ctx->height/2 + 6);
		chart_set_size(&gf->power_chart, ctx->width/2 - 18, ctx->height/2 - 18);
		chart_set_stroke_rgba(&gf->power_chart, 0.45, 0.55, 0.45, 1.);
		memset(gf->power_range, 0, sizeof(gf->power_range));
	}

	rc6_init(&gf->rc6);
}

static void show_gpu_freq(struct overlay_context *ctx, struct overlay_gpu_freq *gf)
{
	char buf[160];
	int y, len;

	y = ctx->height/2 + 6 + 12 - 2;

	if (gpu_freq_update(&gf->gpu_freq) == 0) {
		if (gf->gpu_freq.current)
			chart_add_sample(&gf->current, gf->gpu_freq.current);
		if (gf->gpu_freq.request)
			chart_add_sample(&gf->request, gf->gpu_freq.request);

		chart_draw(&gf->request, ctx->cr);
		chart_draw(&gf->current, ctx->cr);

		len = sprintf(buf, "Frequency: %dMHz", gf->gpu_freq.current);
		if (gf->gpu_freq.request)
			sprintf(buf + len, " (requested %dMHz)", gf->gpu_freq.request);
		cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
		cairo_move_to(ctx->cr, 12, y);
		cairo_show_text(ctx->cr, buf);
		y += 14;

		sprintf(buf, "min: %dMHz, max: %dMHz", gf->gpu_freq.min, gf->gpu_freq.max);
		cairo_move_to(ctx->cr, 12, y);
		cairo_show_text(ctx->cr, buf);
		y += 14;
	}

	if (rc6_update(&gf->rc6) == 0) {
		sprintf(buf, "RC6: %d%%", gf->rc6.rc6_combined);
		cairo_move_to(ctx->cr, 12, y);
		cairo_show_text(ctx->cr, buf);
		if (gf->rc6.rc6_combined && !is_power_of_two(gf->rc6.enabled)) {
			len = 0;
			sprintf(buf, " (");
			if (gf->rc6.enabled & 1) {
				if (len)
					len += sprintf(buf + 3 + len, ", ");
				len += sprintf(buf + 3 + len, "rc6=%d%%", gf->rc6.rc6);
			}
			if (gf->rc6.enabled & 2) {
				if (len)
					len += sprintf(buf + 3 + len, ", ");
				len += sprintf(buf + 3 + len, "rc6p=%d%%", gf->rc6.rc6p);
			}
			if (gf->rc6.enabled & 4) {
				if (len)
					len += sprintf(buf + 3 + len, ", ");
				len += sprintf(buf + 3 + len, "rc6pp=%d%%", gf->rc6.rc6pp);
			}
			sprintf(buf + 3 + len, ")");
			cairo_show_text(ctx->cr, buf);
		}
		y += 14;
	}

	if (power_update(&gf->power) == 0) {
		chart_add_sample(&gf->power_chart, gf->power.power_mW);
		if (gf->power.new_sample) {
			chart_get_range(&gf->power_chart, gf->power_range);
			chart_set_range(&gf->power_chart, gf->power_range[0], gf->power_range[1]);
			gf->power.new_sample = 0;
		}
		chart_draw(&gf->power_chart, ctx->cr);

		sprintf(buf, "Power: %llumW", (long long unsigned)gf->power.power_mW);
		cairo_move_to(ctx->cr, 12, y);
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
	chart_set_position(&go->aperture, ctx->width/2+6, ctx->height/2 + 6);
	chart_set_size(&go->aperture, ctx->width/2 - 18, ctx->height/2 - 18);
	chart_set_stroke_rgba(&go->aperture, 0.75, 0.25, 0.50, 1.);
	chart_set_mode(&go->aperture, CHART_STROKE);
	chart_set_range(&go->aperture, 0, go->gem_objects.max_gtt);

	chart_init(&go->gtt, "gtt", 120);
	chart_set_position(&go->gtt, ctx->width/2+6, ctx->height/2 + 6);
	chart_set_size(&go->gtt, ctx->width/2 - 18, ctx->height/2 - 18);
	chart_set_fill_rgba(&go->gtt, 0.25, 0.5, 0.5, 1.);
	chart_set_mode(&go->gtt, CHART_FILL);
	chart_set_range(&go->gtt, 0, go->gem_objects.max_gtt);
}

static void show_gem_objects(struct overlay_context *ctx, struct overlay_gem_objects *go)
{
	struct gem_objects_comm *comm;
	char buf[160];
	int x, y;

	if (go->error == 0)
		go->error = gem_objects_update(&go->gem_objects);
	if (go->error)
		return;

	chart_add_sample(&go->gtt, go->gem_objects.total_gtt);
	chart_add_sample(&go->aperture, go->gem_objects.total_aperture);

	chart_draw(&go->gtt, ctx->cr);
	chart_draw(&go->aperture, ctx->cr);

	y = ctx->height/2 + 6 + 12 - 2;
	x = ctx->width/2 + 12;
	sprintf(buf, "Total: %ldMB, %d objects",
		go->gem_objects.total_bytes >> 20, go->gem_objects.total_count);
	cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
	cairo_move_to(ctx->cr, x, y);
	cairo_show_text(ctx->cr, buf);
	y += 14;

	for (comm = go->gem_objects.comm; comm; comm = comm->next) {
		if ((comm->bytes >> 20) == 0)
			break;

		sprintf(buf, "    %s %ldMB, %d objects",
			comm->name, comm->bytes >> 20, comm->count);
		cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
		cairo_move_to(ctx->cr, x, y);
		cairo_show_text(ctx->cr, buf);
		y += 14;
	}
}

static int take_snapshot;

static void signal_snapshot(int sig)
{
	take_snapshot = sig;
}

int main(int argc, char **argv)
{
	struct overlay_context ctx;
	int i = 0;

	if (argc > 1) {
		x11_overlay_stop();
		return 0;
	}

	signal(SIGUSR1, signal_snapshot);

	ctx.width = 640;
	ctx.height = 236;
	ctx.surface = x11_overlay_create(POS_TOP_RIGHT, &ctx.width, &ctx.height);
	if (ctx.surface == NULL)
		ctx.surface = x11_window_create(POS_TOP_RIGHT, &ctx.width, &ctx.height);
	if (ctx.surface == NULL)
		return ENOMEM;

	debugfs_init();

	init_gpu_top(&ctx, &ctx.gpu_top);
	init_gpu_perf(&ctx, &ctx.gpu_perf);
	init_gpu_freq(&ctx, &ctx.gpu_freq);
	init_gem_objects(&ctx, &ctx.gem_objects);

	while (1) {
		usleep(500*1000);

		ctx.cr = cairo_create(ctx.surface);
		cairo_set_operator(ctx.cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint(ctx.cr);
		cairo_set_operator(ctx.cr, CAIRO_OPERATOR_OVER);

		{
			char buf[80];
			cairo_text_extents_t extents;
			sprintf(buf, "%d", i++);
			cairo_set_source_rgb(ctx.cr, .5, .5, .5);
			cairo_text_extents(ctx.cr, buf, &extents);
			cairo_move_to(ctx.cr,
				      ctx.width-extents.width-6,
				      6+extents.height);
			cairo_show_text(ctx.cr, buf);
		}

		show_gpu_top(&ctx, &ctx.gpu_top);
		show_gpu_perf(&ctx, &ctx.gpu_perf);
		show_gpu_freq(&ctx, &ctx.gpu_freq);
		show_gem_objects(&ctx, &ctx.gem_objects);

		cairo_destroy(ctx.cr);

		overlay_show(ctx.surface);

		if (take_snapshot) {
			char buf[80];
			sprintf(buf, "overlay-snapshot-%d.png", i-1);
			cairo_surface_write_to_png(ctx.surface, buf);
			take_snapshot = 0;
		}
	}

	return 0;
}
