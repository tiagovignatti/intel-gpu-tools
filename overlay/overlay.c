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

#include "overlay.h"
#include "cpu-top.h"
#include "gem-objects.h"
#include "gpu-freq.h"
#include "gpu-top.h"
#include "gpu-perf.h"
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

struct overlay_context {
	cairo_t *cr;
	int last_y;
};

struct overlay_gpu_top {
	struct gpu_top gpu_top;
	struct cpu_top cpu_top;
	struct chart busy[MAX_RINGS];
	struct chart wait[MAX_RINGS];
	struct chart cpu;
};

static void init_gpu_top(struct overlay_gpu_top *gt,
			 cairo_surface_t *surface)
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
	chart_set_size(&gt->cpu,
		       cairo_image_surface_get_width(surface)-24,
		       100);
	chart_set_stroke_rgba(&gt->cpu, 0.75, 0.25, 0.75, 1.);
	chart_set_mode(&gt->cpu, CHART_STROKE);
	chart_set_range(&gt->cpu, 0, 100);

	for (n = 0; n < gt->gpu_top.num_rings; n++) {
		chart_init(&gt->busy[n],
			   gt->gpu_top.ring[n].name,
			   120);
		chart_set_position(&gt->busy[n], 12, 12);
		chart_set_size(&gt->busy[n],
			       cairo_image_surface_get_width(surface)-24,
			       100);
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
		chart_set_size(&gt->wait[n],
			       cairo_image_surface_get_width(surface)-24,
			       100);
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

	y = 12;
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

	ctx->last_y = 112;
}

struct overlay_gpu_perf {
	struct gpu_perf gpu_perf;
};

static void init_gpu_perf(struct overlay_gpu_perf *gp,
			  cairo_surface_t *surface)
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
		len = read(fd, comm, len-1);
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
	int y, n;

	gpu_perf_update(&gp->gpu_perf);

	y = ctx->last_y + 18;

	for (comm = gp->gpu_perf.comm; comm; comm = comm->next) {
		int total;

		if (comm->user_data == NULL) {
			comm->user_data = malloc(sizeof(struct chart));
			if (comm->user_data == NULL)
				continue;

			chart_init(comm->user_data, comm->name, 120);
			chart_set_position(comm->user_data, 12, y);
			chart_set_size(comm->user_data,
				       cairo_image_surface_get_width(cairo_get_target(ctx->cr))-24,
				       100);
			chart_set_mode(comm->user_data, CHART_STROKE);
			chart_set_stroke_rgba(comm->user_data,
					      rgba[last_color][0],
					      rgba[last_color][1],
					      rgba[last_color][2],
					      rgba[last_color][3]);
			last_color++;
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
		cairo_move_to(ctx->cr, 12, y);
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
	cairo_move_to(ctx->cr, 12, y);
	cairo_show_text(ctx->cr, buf);
	y += 14;

	ctx->last_y += 118;
}

struct overlay_gpu_freq {
	struct gpu_freq gpu_freq;
	struct chart current;
	struct chart request;
	int error;
};

static void init_gpu_freq(struct overlay_gpu_freq *gf,
			 cairo_surface_t *surface)
{
	gf->error = gpu_freq_init(&gf->gpu_freq);
	if (gf->error)
		return;

	chart_init(&gf->current, "current", 120);
	chart_set_size(&gf->current,
		       cairo_image_surface_get_width(surface)-24,
		       100);
	chart_set_stroke_rgba(&gf->current, 0.75, 0.25, 0.50, 1.);
	chart_set_mode(&gf->current, CHART_STROKE);
	chart_set_range(&gf->current, 0, gf->gpu_freq.max);

	chart_init(&gf->request, "request", 120);
	chart_set_size(&gf->request,
		       cairo_image_surface_get_width(surface)-24,
		       100);
	chart_set_fill_rgba(&gf->request, 0.25, 0.75, 0.50, 1.);
	chart_set_mode(&gf->request, CHART_FILL);
	chart_set_range(&gf->request, 0, gf->gpu_freq.max);
}

static void show_gpu_freq(struct overlay_context *ctx, struct overlay_gpu_freq *gf)
{
	char buf[160];
	int y, len;

	if (gf->error == 0)
		gf->error = gpu_freq_update(&gf->gpu_freq);
	if (gf->error)
		return;

	if (gf->gpu_freq.current)
		chart_add_sample(&gf->current, gf->gpu_freq.current);
	if (gf->gpu_freq.request)
		chart_add_sample(&gf->request, gf->gpu_freq.request);

	y = ctx->last_y + 6;

	chart_set_position(&gf->current, 12, y);
	chart_set_position(&gf->request, 12, y);

	chart_draw(&gf->request, ctx->cr);
	chart_draw(&gf->current, ctx->cr);

	len = sprintf(buf, "Frequency: %dMHz", gf->gpu_freq.current);
	if (gf->gpu_freq.request)
		sprintf(buf + len, " (requested %dMHz)", gf->gpu_freq.request);
	cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
	cairo_move_to(ctx->cr, 12, y);
	cairo_show_text(ctx->cr, buf);
	y += 14;

	ctx->last_y += 112;
}

struct overlay_gem_objects {
	struct gem_objects gem_objects;
	struct chart aperture;
	struct chart gtt;
	int error;
};

static void init_gem_objects(struct overlay_gem_objects *go,
			 cairo_surface_t *surface)
{
	go->error = gem_objects_init(&go->gem_objects);
	if (go->error)
		return;

	chart_init(&go->aperture, "aperture", 120);
	chart_set_size(&go->aperture,
		       cairo_image_surface_get_width(surface)-24,
		       100);
	chart_set_stroke_rgba(&go->aperture, 0.75, 0.25, 0.50, 1.);
	chart_set_mode(&go->aperture, CHART_STROKE);
	chart_set_range(&go->aperture, 0, go->gem_objects.max_gtt);

	chart_init(&go->gtt, "gtt", 120);
	chart_set_size(&go->gtt,
		       cairo_image_surface_get_width(surface)-24,
		       100);
	chart_set_fill_rgba(&go->gtt, 0.25, 0.75, 0.50, 1.);
	chart_set_mode(&go->gtt, CHART_FILL);
	chart_set_range(&go->gtt, 0, go->gem_objects.max_gtt);
}

static void show_gem_objects(struct overlay_context *ctx, struct overlay_gem_objects *go)
{
	struct gem_objects_comm *comm;
	char buf[160];
	int y;

	if (go->error == 0)
		go->error = gem_objects_update(&go->gem_objects);
	if (go->error)
		return;

	chart_add_sample(&go->gtt, go->gem_objects.total_gtt);
	chart_add_sample(&go->aperture, go->gem_objects.total_aperture);

	y = ctx->last_y + 6;

	chart_set_position(&go->gtt, 12, y);
	chart_set_position(&go->aperture, 12, y);

	chart_draw(&go->gtt, ctx->cr);
	chart_draw(&go->aperture, ctx->cr);

	sprintf(buf, "Total: %ldMB, %d objects",
		go->gem_objects.total_bytes >> 20, go->gem_objects.total_count);
	cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
	cairo_move_to(ctx->cr, 12, y);
	cairo_show_text(ctx->cr, buf);
	y += 14;

	for (comm = go->gem_objects.comm; comm; comm = comm->next) {
		if ((comm->bytes >> 20) == 0)
			break;

		sprintf(buf, "    %s: %ldMB, %d objects",
			comm->name, comm->bytes >> 20, comm->count);
		cairo_set_source_rgba(ctx->cr, 1, 1, 1, 1);
		cairo_move_to(ctx->cr, 12, y);
		cairo_show_text(ctx->cr, buf);
		y += 14;
	}

	ctx->last_y += 112;
}

int main(int argc, char **argv)
{
	cairo_surface_t *surface;
	struct overlay_gpu_top gpu_top;
	struct overlay_gpu_perf gpu_perf;
	struct overlay_gpu_freq gpu_freq;
	struct overlay_gem_objects gem_objects;
	int i = 0;

	if (argc > 1) {
		x11_overlay_stop();
		return 0;
	}

	surface = x11_overlay_create(POS_TOP_RIGHT, 640, 480);
	if (surface == NULL)
		return ENOMEM;

	init_gpu_top(&gpu_top, surface);
	init_gpu_perf(&gpu_perf, surface);
	init_gpu_freq(&gpu_freq, surface);
	init_gem_objects(&gem_objects, surface);

	while (1) {
		struct overlay_context ctx;

		usleep(500*1000);

		ctx.cr = cairo_create(surface);
		cairo_set_operator(ctx.cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint(ctx.cr);
		cairo_set_operator(ctx.cr, CAIRO_OPERATOR_OVER);
		ctx.last_y = 6;

		{
			char buf[80];
			cairo_text_extents_t extents;
			sprintf(buf, "%d", i++);
			cairo_set_source_rgb(ctx.cr, .5, .5, .5);
			cairo_text_extents(ctx.cr, buf, &extents);
			cairo_move_to(ctx.cr,
				      cairo_image_surface_get_width(surface)-extents.width-6,
				      6+extents.height);
			cairo_show_text(ctx.cr, buf);
		}

		show_gpu_top(&ctx, &gpu_top);
		show_gpu_perf(&ctx, &gpu_perf);
		show_gpu_freq(&ctx, &gpu_freq);
		show_gem_objects(&ctx, &gem_objects);

		cairo_destroy(ctx.cr);

		overlay_show(surface);
	}

	return 0;
}
