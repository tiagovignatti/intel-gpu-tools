/*
 * Copyright © 2016 Intel Corporation
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
 * Authors: Paulo Zanoni <paulo.r.zanoni@intel.com>
 *
 */

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <getopt.h>
#include "igt.h"

#define IA32_TIME_STAMP_COUNTER		0x10

#define MSR_PKG_CST_CONFIG_CONTROL	0xE2
#define  PKG_CST_LIMIT_MASK		0x7
#define  PKG_CST_LIMIT_C0		0x0
#define  PKG_CST_LIMIT_C2		0x1
#define  PKG_CST_LIMIT_C3		0x2
#define  PKG_CST_LIMIT_C6		0x3
#define  PKG_CST_LIMIT_C7		0x4
#define  PKG_CST_LIMIT_C7s		0x5
#define  PKG_CST_NO_LIMIT		0x7

#define MSR_PKG_C2_RESIDENCY		0x60D
#define MSR_PKG_C3_RESIDENCY		0x3F8
#define MSR_PKG_C6_RESIDENCY		0x3F9
#define MSR_PKG_C7_RESIDENCY		0x3FA
#define MSR_PKG_C8_RESIDENCY		0x630
#define MSR_PKG_C9_RESIDENCY		0x631
#define MSR_PKG_C10_RESIDENCY		0x632

#define NUM_PC_STATES 7

const char *res_msr_names[] = {
	"PC2", "PC3", "PC6", "PC7", "PC8", "PC9", "PC10"
};

const uint32_t res_msr_addrs[] = {
	MSR_PKG_C2_RESIDENCY,
	MSR_PKG_C3_RESIDENCY,
	MSR_PKG_C6_RESIDENCY,
	MSR_PKG_C7_RESIDENCY,
	MSR_PKG_C8_RESIDENCY,
	MSR_PKG_C9_RESIDENCY,
	MSR_PKG_C10_RESIDENCY,
};

int msr_fd;

uint32_t deepest_pc_state;
uint64_t idle_res;

#define MAX_CONNECTORS 32
#define MAX_PLANES 32
struct {
	int fd;
	drmModeResPtr res;
	drmModeConnectorPtr connectors[MAX_CONNECTORS];
	drm_intel_bufmgr *bufmgr;
} drm;

struct {
	uint32_t crtc_id;
	uint32_t connector_id;
	drmModeModeInfoPtr mode;
} modeset;

int vblank_interval_us;
struct igt_fb fbs[2], cursor, *front_fb, *back_fb;

struct {
	int draw_size;
	bool do_page_flip;
	bool do_draw;
	bool do_draw_and_flip;
	int res_warm_time;
	int res_calc_time;
	int loop_inc;
	char *test_name;
} opts = {
	.draw_size = 0,
	.do_page_flip = true,
	.do_draw = true,
	.do_draw_and_flip = true,
	.res_warm_time = 1,
	.res_calc_time = 4,
	.loop_inc = 2,
	.test_name = NULL,
};

static uint64_t msr_read(uint32_t addr)
{
	int rc;
	uint64_t ret;

	rc = pread(msr_fd, &ret, sizeof(uint64_t), addr);
	igt_assert(rc == sizeof(ret));

	return ret;
}

static void setup_msr(void)
{
#if 0
	uint64_t control;
	const char *limit;
#endif

	/* Make sure our Kernel supports MSR and the module is loaded. */
	igt_assert(system("modprobe -q msr > /dev/null 2>&1") != -1);

	msr_fd = open("/dev/cpu/0/msr", O_RDONLY);
	igt_assert_f(msr_fd >= 0,
		     "Can't open /dev/cpu/0/msr.\n");

#if 0
	/* FIXME: why is this code not printing the truth? */
	control = msr_read(MSR_PKG_CST_CONFIG_CONTROL);
	printf("Control: 0x016%" PRIx64 "\n", control);
	switch (control & PKG_CST_LIMIT_MASK) {
	case PKG_CST_LIMIT_C0:
		limit = "C0";
		break;
	case PKG_CST_LIMIT_C2:
		limit = "C2";
		break;
	case PKG_CST_LIMIT_C3:
		limit = "C3";
		break;
	case PKG_CST_LIMIT_C6:
		limit = "C6";
		break;
	case PKG_CST_LIMIT_C7:
		limit = "C7";
		break;
	case PKG_CST_LIMIT_C7s:
		limit = "C7s";
		break;
	case PKG_CST_NO_LIMIT:
		limit = "no limit";
		break;
	default:
		limit = "unknown";
		break;
	}
	printf("Package C state limit: %s\n", limit);
#endif
}

static void teardown_msr(void)
{
	close(msr_fd);
}

static void setup_drm(void)
{
	int i;

	drm.fd = drm_open_driver_master(DRIVER_INTEL);

	drm.res = drmModeGetResources(drm.fd);
	igt_assert(drm.res->count_connectors <= MAX_CONNECTORS);

	for (i = 0; i < drm.res->count_connectors; i++)
		drm.connectors[i] = drmModeGetConnector(drm.fd,
						drm.res->connectors[i]);

	drm.bufmgr = drm_intel_bufmgr_gem_init(drm.fd, 4096);
	igt_assert(drm.bufmgr);
	drm_intel_bufmgr_gem_enable_reuse(drm.bufmgr);
}

static void teardown_drm(void)
{
	int i;

	drm_intel_bufmgr_destroy(drm.bufmgr);

	for (i = 0; i < drm.res->count_connectors; i++)
		drmModeFreeConnector(drm.connectors[i]);

	drmModeFreeResources(drm.res);
	close(drm.fd);
}

static void draw_rect(struct igt_fb *fb, enum igt_draw_method method,
		      uint32_t color)
{
	drmModeClip clip;
	int rc;

	switch (opts.draw_size) {
	case 0:
		clip.x1 = fb->width / 2 - 32;
		clip.x2 = fb->width / 2 + 32;
		clip.y1 = fb->height / 2 - 32;
		clip.y2 = fb->height / 2 + 32;
		break;
	case 1:
		clip.x1 = fb->width / 4;
		clip.x2 = fb->width / 4 + fb->width / 2;
		clip.y1 = fb->height / 4;
		clip.y2 = fb->height / 4 + fb->height / 2;
		break;
	case 2:
		clip.x1 = 0;
		clip.x2 = fb->width;
		clip.y1 = 0;
		clip.y2 = fb->height;
		break;
	default:
		igt_assert(false);
	}

	igt_draw_rect_fb(drm.fd, drm.bufmgr, NULL, fb, method, clip.x1, clip.y1,
			 clip.x2 - clip.x1, clip.y2 - clip.y1, color);

	if (method == IGT_DRAW_MMAP_WC) {
		rc = drmModeDirtyFB(drm.fd, fb->fb_id, &clip, 1);
		igt_assert(rc == 0 || rc == -ENOSYS);
	}
}

static void setup_modeset(void)
{
	int i;

	for (i = 0; i < drm.res->count_connectors; i++) {
		drmModeConnectorPtr c = drm.connectors[i];

		if (c->connection == DRM_MODE_CONNECTED &&
		    c->count_modes > 0) {
			modeset.connector_id = c->connector_id;
			modeset.mode = &c->modes[0];
			break;
		}
	}
	igt_assert(i < drm.res->count_connectors);

	modeset.crtc_id = drm.res->crtcs[0];

	for (i = 0; i < 2; i++) {
		igt_create_fb(drm.fd, modeset.mode->hdisplay,
			      modeset.mode->vdisplay,  DRM_FORMAT_XRGB8888,
			      LOCAL_I915_FORMAT_MOD_X_TILED, &fbs[i]);
		igt_draw_fill_fb(drm.fd, &fbs[i], 0x80);
	}
	draw_rect(&fbs[1], IGT_DRAW_BLT, 0x800000);

	igt_create_fb(drm.fd, 64, 64, DRM_FORMAT_ARGB8888,
		     LOCAL_DRM_FORMAT_MOD_NONE, &cursor);
	igt_draw_fill_fb(drm.fd, &cursor, 0xFF008000);
}

static void teardown_modeset(void)
{
	igt_remove_fb(drm.fd, &fbs[0]);
	igt_remove_fb(drm.fd, &fbs[1]);
	igt_remove_fb(drm.fd, &cursor);
}

static void setup_vblank_interval(void)
{
	uint64_t vrefresh, interval;

	vrefresh = ((uint64_t) modeset.mode->clock * 1000 * 1000) /
		   (modeset.mode->htotal * modeset.mode->vtotal);
	interval = 1000000000 / vrefresh;

	vblank_interval_us = interval;

	printf("Interval between vblanks:\t%dus\n", vblank_interval_us);
}

bool alarm_received;
static void alarm_handler(int signal)
{
	alarm_received = true;
}

static void setup_alarm(void)
{
	struct sigaction sa;

	sa.sa_handler = alarm_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGALRM, &sa, NULL);
}

static void set_alarm(time_t sec, suseconds_t usec)
{
	struct itimerval timerval = {{0, 0}, {sec, usec}};

	alarm_received = false;
	igt_assert(setitimer(ITIMER_REAL, &timerval, NULL) == 0);
}

static void unset_mode(void)
{
	int rc;

	kmstest_unset_all_crtcs(drm.fd, drm.res);
	rc = drmModeSetCursor(drm.fd, modeset.crtc_id, 0, 0, 0);
	igt_assert(rc == 0);
}

static void set_mode(void)
{
	int rc;

	front_fb = &fbs[0];
	back_fb = &fbs[1];
	rc = drmModeSetCrtc(drm.fd, modeset.crtc_id, front_fb->fb_id, 0, 0,
			    &modeset.connector_id, 1, modeset.mode);
	igt_assert(rc == 0);

	/* TODO: it seems we need a cursor in order to reach PC7 on BDW. Why? */
	rc = drmModeMoveCursor(drm.fd, modeset.crtc_id, 0, 0);
	igt_assert(rc == 0);

	rc = drmModeSetCursor(drm.fd, modeset.crtc_id, cursor.gem_handle,
			      cursor.width, cursor.height);
	igt_assert(rc == 0);
}

static void wait_vblanks(int n_vblanks)
{
	drmVBlank vblank;

	if (!n_vblanks)
		return;

	vblank.request.type = DRM_VBLANK_RELATIVE;
	vblank.request.sequence = n_vblanks;
	vblank.request.signal = 0;
	drmWaitVBlank(drm.fd, &vblank);
}

static void page_flip(void)
{
	struct igt_fb *tmp_fb;
	int rc;

	rc = drmModePageFlip(drm.fd, modeset.crtc_id, back_fb->fb_id, 0, NULL);
	igt_assert(rc == 0);

	tmp_fb = front_fb;
	front_fb = back_fb;
	back_fb = tmp_fb;
}

static void wait_until_idle(void)
{
	uint64_t tsc, pc, res;

	do {
		set_alarm(0, 500 * 1000);

		tsc = msr_read(IA32_TIME_STAMP_COUNTER);
		pc = msr_read(deepest_pc_state);

		while (!alarm_received)
			pause();

		pc = msr_read(deepest_pc_state) - pc;
		tsc = msr_read(IA32_TIME_STAMP_COUNTER) - tsc;

		res = pc * 100 / tsc;

		/*printf("res:%02"PRIu64"\n", res);*/
	} while (res < idle_res && idle_res - res > 3);

	if (res > idle_res && res - idle_res > 3)
		fprintf(stderr, "The calculated idle residency may be too low "
			"(got %02"PRIu64"%%)\n", res);
}

static uint64_t do_measurement(void (*callback)(void *ptr), void *ptr)
{
	uint64_t tsc, pc;

	wait_until_idle();

	set_alarm(opts.res_warm_time, 0);
	callback(ptr);

	set_alarm(opts.res_calc_time, 0);

	tsc = msr_read(IA32_TIME_STAMP_COUNTER);
	pc = msr_read(deepest_pc_state);

	callback(ptr);

	pc = msr_read(deepest_pc_state) - pc;
	tsc = msr_read(IA32_TIME_STAMP_COUNTER) - tsc;

	return pc * 100 / tsc;
}

static void setup_idle(void)
{
	uint64_t tsc, pc[NUM_PC_STATES], res, best_res;
	int pc_i, best_pc_i = 0, retries, consecutive_not_best;

	for (retries = 0; ; retries++) {

		set_alarm(opts.res_warm_time, 0);
		while (!alarm_received)
			pause();

		set_alarm(opts.res_calc_time, 0);

		tsc = msr_read(IA32_TIME_STAMP_COUNTER);
		for (pc_i = best_pc_i; pc_i < NUM_PC_STATES; pc_i++)
			pc[pc_i] = msr_read(res_msr_addrs[pc_i]);

		while (!alarm_received)
			pause();

		for (pc_i = best_pc_i; pc_i < NUM_PC_STATES; pc_i++)
			pc[pc_i] = msr_read(res_msr_addrs[pc_i]) - pc[pc_i];
		tsc = msr_read(IA32_TIME_STAMP_COUNTER) - tsc;

		for (pc_i = NUM_PC_STATES -1; pc_i >= best_pc_i; pc_i--)
			if (pc[pc_i] != 0)
				break;
		igt_require_f(pc_i >= 0, "We're not reaching any PC states!\n");

		res = pc[pc_i] * 100 / tsc;

		if (retries == 0 || pc_i > best_pc_i || res > best_res) {
			best_pc_i = pc_i;
			best_res = res;
			consecutive_not_best = 0;
		} else {
			consecutive_not_best++;
			if (consecutive_not_best > 2)
				break;
		}
	}

	deepest_pc_state = res_msr_addrs[best_pc_i];
	idle_res = best_res;

	printf("Stable idle residency retries:\t%d\n", retries);
	printf("Deepest PC state reached when idle:\t%s\n",
	       res_msr_names[best_pc_i]);
	printf("Idle residency for this state:\t%02"PRIu64"%%\n", idle_res);
}

static void print_result(int ops, int vblanks, uint64_t res)
{
	printf("- %02d ops every %02d vblanks:\t%02"PRIu64"%%\n",
	       ops, vblanks, res);
	fflush(stdout);
}

struct page_flip_data {
	int n_vblanks;
};

static void page_flip_cb(void *ptr)
{
	struct page_flip_data *data = ptr;

	while (!alarm_received) {
		page_flip();
		wait_vblanks(data->n_vblanks);
	}
}

static void page_flip_test(void)
{
	struct page_flip_data data;
	int n_vblanks;
	uint64_t res;

	printf("\nPage flip test:\n");

	for (n_vblanks = 1; n_vblanks <= 64; n_vblanks *= opts.loop_inc) {
		data.n_vblanks = n_vblanks;
		res = do_measurement(page_flip_cb, &data);
		print_result(1, n_vblanks, res);
	}
}

struct draw_data {
	enum igt_draw_method method;
	int n_vblanks;
	int ops_per_vblank;
};

static void draw_cb(void *ptr)
{
	struct draw_data *data = ptr;
	struct timespec req;
	int i, ops;

	req.tv_sec = 0;
	req.tv_nsec = vblank_interval_us * 1000 / data->ops_per_vblank;

	for (i = 0; !alarm_received; i++) {
		for (ops = 0; ops < data->ops_per_vblank; ops++) {
			draw_rect(front_fb, data->method, i << 8);

			/* The code that stops the callbacks relies on SIGALRM,
			 * so we have to use nanosleep since it doesn't use
			 * signals. */
			if (data->ops_per_vblank > 1)
				nanosleep(&req, NULL);
		}

		if (data->n_vblanks)
			wait_vblanks(data->n_vblanks);
	}
}

static void draw_test(void)
{
	struct draw_data data;
	enum igt_draw_method method;
	int i;
	uint64_t res;

	for (method = 0; method < IGT_DRAW_METHOD_COUNT; method++) {
		data.method = method;

		printf("\nDraw %s test:\n",
		       igt_draw_get_method_name(method));

		data.n_vblanks = 0;
		for (i = 32; i >= 2; i /= opts.loop_inc) {
			data.ops_per_vblank = i;
			res = do_measurement(draw_cb, &data);
			print_result(i, 1, res);
		}

		data.ops_per_vblank = 1;
		for (i = 1; i <= 64; i *= opts.loop_inc) {
			data.n_vblanks = i ;
			res = do_measurement(draw_cb, &data);
			print_result(1, i, res);
		}
	}
}

static void draw_and_flip_cb(void *ptr)
{
	struct draw_data *data = ptr;
	int i, ops;

	for (i = 0; !alarm_received; i++) {
		for (ops = 0; ops < data->ops_per_vblank; ops++)
			draw_rect(back_fb, data->method, i << 8);

		page_flip();
		wait_vblanks(1);
	}
}

static void draw_and_flip_test(void)
{
	struct draw_data data;
	enum igt_draw_method method;
	int i;
	uint64_t res;

	for (method = 0; method < IGT_DRAW_METHOD_COUNT; method++) {
		data.method = method;

		/* Doing everything consumes too much time! */
		if (method != IGT_DRAW_MMAP_CPU && method != IGT_DRAW_BLT)
			continue;

		printf("\nDraw and flip %s test:\n",
		       igt_draw_get_method_name(method));

		for (i = 16; i >= 1; i /= opts.loop_inc) {
			data.ops_per_vblank = 1;
			res = do_measurement(draw_and_flip_cb, &data);
			print_result(i, 1, res);
		}
	}
}

static void parse_opts(int argc, char *argv[])
{
	int opt;
	char short_opts[] = "d:lrbw:c:i:fsn:";
	struct option long_opts[] = {
		{ "draw-size",        required_argument, NULL, 'd'},
		{ "no-flip",          no_argument,       NULL, 'l'},
		{ "no-draw",          no_argument,       NULL, 'r'},
		{ "no-draw-and-flip", no_argument,       NULL, 'b'},
		{ "warm-time",        required_argument, NULL, 'w'},
		{ "calc-time",        required_argument, NULL, 'c'},
		{ "loop-increment",   required_argument, NULL, 'i'},
		{ "fast",             no_argument,       NULL, 'f'},
		{ "slow",             no_argument,       NULL, 's'},
		{ "name",             required_argument, NULL, 'n'},
		{ 0 },
	};

	while (1) {
		opt = getopt_long(argc, argv, short_opts, long_opts, NULL);

		switch (opt) {
		case 'd':
			if (strcmp(optarg, "s") == 0)
				opts.draw_size = 0;
			else if (strcmp(optarg, "m") == 0)
				opts.draw_size = 1;
			else if (strcmp(optarg, "l") == 0)
				opts.draw_size = 2;
			else
				igt_assert(false);
			break;
		case 'l':
			opts.do_page_flip = false;
			break;
		case 'r':
			opts.do_draw = false;
			break;
		case 'b':
			opts.do_draw_and_flip = false;
			break;
		case 'w':
			opts.res_warm_time = atoi(optarg);
			break;
		case 'c':
			opts.res_calc_time = atoi(optarg);
			break;
		case 'i':
			opts.loop_inc = atoi(optarg);
			break;
		case 'f':
			opts.res_warm_time = 1;
			opts.res_calc_time = 2;
			opts.loop_inc = 4;
			break;
		case 's':
			opts.res_warm_time = 2;
			opts.res_calc_time = 6;
			opts.loop_inc = 2;
			break;
		case 'n':
			opts.test_name = optarg;
			break;
		case -1:
			return;
		default:
			igt_assert(false);
		}
	}
}

int main(int argc, char *argv[])
{
	parse_opts(argc, argv);

	setup_msr();
	setup_drm();
	setup_modeset();
	setup_vblank_interval();
	setup_alarm();

	printf("Test name:\t%s\n", opts.test_name);

	unset_mode();
	set_mode();

	setup_idle();

	if (opts.do_page_flip)
		page_flip_test();

	if (opts.do_draw)
		draw_test();

	if (opts.do_draw_and_flip)
		draw_and_flip_test();

	teardown_modeset();
	teardown_drm();
	teardown_msr();
	return 0;
}
