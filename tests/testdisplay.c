/*
 * Copyright 2010 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * This program is intended for testing of display functionality.  It should
 * allow for testing of
 *   - hotplug
 *   - mode setting
 *   - clone & twin modes
 *   - panel fitting
 *   - test patterns & pixel generators
 * Additional programs can test the detected outputs against VBT provided
 * device lists (both docked & undocked).
 *
 * TODO:
 * - pixel generator in transcoder
 * - test pattern reg in pipe
 * - test patterns on outputs (e.g. TV)
 * - handle hotplug (leaks crtcs, can't handle clones)
 * - allow mode force
 * - expose output specific controls
 *  - e.g. DDC-CI brightness
 *  - HDMI controls
 *  - panel brightness
 *  - DP commands (e.g. poweroff)
 * - verify outputs against VBT/physical connectors
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <cairo.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <strings.h>
#include <unistd.h>
#include <termios.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "testdisplay.h"
#include "igt_kms.h"

#include <stdlib.h>
#include <signal.h>

static int tio_fd;
struct termios saved_tio;

drmModeRes *resources;
int drm_fd, modes;
int test_all_modes = 0, test_preferred_mode = 0, force_mode = 0, test_plane,
    test_stereo_modes, enable_tiling;
int sleep_between_modes = 5;
int do_dpms = 0; /* This aliases to DPMS_ON */
uint32_t depth = 24, stride, bpp;
int qr_code = 0;
int specified_mode_num = -1, specified_disp_id = -1;

drmModeModeInfo force_timing;

int crtc_x, crtc_y, crtc_w, crtc_h, width, height;
unsigned int plane_fb_id;
unsigned int plane_crtc_id;
unsigned int plane_id;
int plane_width, plane_height;
static const uint32_t SPRITE_COLOR_KEY = 0x00aaaaaa;

/*
 * Mode setting with the kernel interfaces is a bit of a chore.
 * First you have to find the connector in question and make sure the
 * requested mode is available.
 * Then you need to find the encoder attached to that connector so you
 * can bind it with a free crtc.
 */
struct connector {
	uint32_t id;
	int mode_valid;
	drmModeModeInfo mode;
	drmModeEncoder *encoder;
	drmModeConnector *connector;
	int crtc;
	int crtc_idx;
	int pipe;
};

static void dump_connectors_fd(int drmfd)
{
	int i, j;

	drmModeRes *mode_resources = drmModeGetResources(drmfd);

	if (!mode_resources) {
		fprintf(stderr, "drmModeGetResources failed: %s\n",
			strerror(errno));
		return;
	}

	printf("Connectors:\n");
	printf("id\tencoder\tstatus\t\ttype\tsize (mm)\tmodes\n");
	for (i = 0; i < mode_resources->count_connectors; i++) {
		drmModeConnector *connector;

		connector = drmModeGetConnector(drmfd, mode_resources->connectors[i]);
		if (!connector) {
			fprintf(stderr, "could not get connector %i: %s\n",
				mode_resources->connectors[i], strerror(errno));
			continue;
		}

		printf("%d\t%d\t%s\t%s\t%dx%d\t\t%d\n",
		       connector->connector_id,
		       connector->encoder_id,
		       kmstest_connector_status_str(connector->connection),
		       kmstest_connector_type_str(connector->connector_type),
		       connector->mmWidth, connector->mmHeight,
		       connector->count_modes);

		if (!connector->count_modes)
			continue;

		printf("  modes:\n");
		printf("  name refresh (Hz) hdisp hss hse htot vdisp "
		       "vss vse vtot flags type clock\n");
		for (j = 0; j < connector->count_modes; j++){
			fprintf(stdout, "[%d]", j );
			kmstest_dump_mode(&connector->modes[j]);
		}

		drmModeFreeConnector(connector);
	}
	printf("\n");

	drmModeFreeResources(mode_resources);
}

static void dump_crtcs_fd(int drmfd)
{
	int i;
	drmModeRes *mode_resources = drmModeGetResources(drmfd);

	printf("CRTCs:\n");
	printf("id\tfb\tpos\tsize\n");
	for (i = 0; i < mode_resources->count_crtcs; i++) {
		drmModeCrtc *crtc;

		crtc = drmModeGetCrtc(drmfd, mode_resources->crtcs[i]);
		if (!crtc) {
			fprintf(stderr, "could not get crtc %i: %s\n",
				mode_resources->crtcs[i], strerror(errno));
			continue;
		}
		printf("%d\t%d\t(%d,%d)\t(%dx%d)\n",
		       crtc->crtc_id,
		       crtc->buffer_id,
		       crtc->x, crtc->y,
		       crtc->width, crtc->height);
		kmstest_dump_mode(&crtc->mode);

		drmModeFreeCrtc(crtc);
	}
	printf("\n");

	drmModeFreeResources(mode_resources);
}

static void dump_info(void)
{
	dump_connectors_fd(drm_fd);
	dump_crtcs_fd(drm_fd);
}

static void connector_find_preferred_mode(uint32_t connector_id,
					  unsigned long crtc_idx_mask,
					  int mode_num, struct connector *c)
{
	struct kmstest_connector_config config;

	if (kmstest_get_connector_config(drm_fd, connector_id, crtc_idx_mask,
					 &config) < 0) {
		c->mode_valid = 0;
		return;
	}

	c->connector = config.connector;
	c->encoder = config.encoder;
	c->crtc = config.crtc->crtc_id;
	c->crtc_idx = config.crtc_idx;
	c->pipe = config.pipe;

	if (mode_num != -1) {
		igt_assert(mode_num < config.connector->count_modes);
		c->mode = config.connector->modes[mode_num];
	} else {
		c->mode = config.default_mode;
	}
	c->mode_valid = 1;
}

static void
paint_color_key(struct igt_fb *fb_info)
{
	int i, j;
	uint32_t *fb_ptr;

	fb_ptr = gem_mmap(drm_fd, fb_info->gem_handle,
			  fb_info->size, PROT_READ | PROT_WRITE);
	igt_assert(fb_ptr);

	for (i = crtc_y; i < crtc_y + crtc_h; i++)
		for (j = crtc_x; j < crtc_x + crtc_w; j++) {
			uint32_t offset;

			offset = (i * fb_info->stride / 4) + j;
			fb_ptr[offset] = SPRITE_COLOR_KEY;
		}

	munmap(fb_ptr, fb_info->size);
}

static void paint_image(cairo_t *cr, const char *file)
{
	int img_x, img_y, img_w, img_h, img_w_o, img_h_o;
	double img_w_scale, img_h_scale;

	cairo_surface_t *image;

	img_y = height * (0.10 );
	img_h = height * 0.08 * 4;
	img_w = img_h;

	img_x = (width / 2) - (img_w / 2);

	image = cairo_image_surface_create_from_png(file);

	img_w_o = cairo_image_surface_get_width(image);
	img_h_o = cairo_image_surface_get_height(image);

	cairo_translate(cr, img_x, img_y);

	img_w_scale = (double)img_w / (double)img_w_o;
	img_h_scale = (double)img_h / (double)img_h_o;
	cairo_scale(cr, img_w_scale, img_h_scale);

	cairo_set_source_surface(cr, image, 0, 0);
	cairo_scale(cr, 1, 1);

	cairo_paint(cr);
	cairo_surface_destroy(image);
}

static void paint_output_info(struct connector *c, struct igt_fb *fb)
{
	cairo_t *cr = igt_get_cairo_ctx(drm_fd, fb);
	int l_width = fb->width;
	int l_height = fb->height;
	double str_width;
	double x, y, top_y;
	double max_width;
	int i;

	igt_paint_test_pattern(cr, l_width, l_height);

	cairo_move_to(cr, l_width / 2, l_height / 2);

	/* Print connector and mode name */
	cairo_set_font_size(cr, 48);
	igt_cairo_printf_line(cr, align_hcenter, 10, "%s",
		 kmstest_connector_type_str(c->connector->connector_type));

	cairo_set_font_size(cr, 36);
	str_width = igt_cairo_printf_line(cr, align_hcenter, 10,
		"%s @ %dHz on %s encoder", c->mode.name, c->mode.vrefresh,
		kmstest_encoder_type_str(c->encoder->encoder_type));

	cairo_rel_move_to(cr, -str_width / 2, 0);

	/* List available modes */
	cairo_set_font_size(cr, 18);
	str_width = igt_cairo_printf_line(cr, align_left, 10,
					      "Available modes:");
	cairo_rel_move_to(cr, str_width, 0);
	cairo_get_current_point(cr, &x, &top_y);

	max_width = 0;
	for (i = 0; i < c->connector->count_modes; i++) {
		cairo_get_current_point(cr, &x, &y);
		if (y >= l_height) {
			x += max_width + 10;
			max_width = 0;
			cairo_move_to(cr, x, top_y);
		}
		str_width = igt_cairo_printf_line(cr, align_right, 10,
			"%s @ %dHz", c->connector->modes[i].name,
			 c->connector->modes[i].vrefresh);
		if (str_width > max_width)
			max_width = str_width;
	}

	if (qr_code)
		paint_image(cr, IGT_DATADIR"/pass.png");

	igt_assert(!cairo_status(cr));
}

static void sighandler(int signo)
{
	return;
}

static void set_single(void)
{
	int sigs[] = { SIGUSR1 };
	struct sigaction sa;
	sa.sa_handler = sighandler;

	sigemptyset(&sa.sa_mask);

	if (sigaction(sigs[0], &sa, NULL) == -1)
		perror("Could not set signal handler");
}

static void
set_mode(struct connector *c)
{
	unsigned int fb_id = 0;
	struct igt_fb fb_info[2] = { };
	int j, test_mode_num, current_fb = 0, old_fb = -1;

	test_mode_num = 1;
	if (force_mode){
		memcpy( &c->mode, &force_timing, sizeof(force_timing));
		c->mode.vrefresh =(force_timing.clock*1e3)/(force_timing.htotal*force_timing.vtotal);
		c->mode_valid = 1;
		sprintf(c->mode.name, "%dx%d", force_timing.hdisplay, force_timing.vdisplay);
	} else if (test_all_modes)
		test_mode_num = c->connector->count_modes;

	for (j = 0; j < test_mode_num; j++) {

		if (test_all_modes)
			c->mode = c->connector->modes[j];

		/* set_mode() only tests 2D modes */
		if (c->mode.flags & DRM_MODE_FLAG_3D_MASK)
			continue;

		if (!c->mode_valid)
			continue;

		width = c->mode.hdisplay;
		height = c->mode.vdisplay;

		fb_id = igt_create_fb(drm_fd, width, height,
					  igt_bpp_depth_to_drm_format(bpp, depth),
					  enable_tiling, &fb_info[current_fb]);
		paint_output_info(c, &fb_info[current_fb]);
		paint_color_key(&fb_info[current_fb]);

		fprintf(stdout, "CRTC(%u):[%d]",c->crtc, j);
		kmstest_dump_mode(&c->mode);
		if (drmModeSetCrtc(drm_fd, c->crtc, fb_id, 0, 0,
				   &c->id, 1, &c->mode)) {
			fprintf(stderr, "failed to set mode (%dx%d@%dHz): %s\n",
				width, height, c->mode.vrefresh,
				strerror(errno));
			continue;
		}

		if (old_fb != -1)
			igt_remove_fb(drm_fd, &fb_info[old_fb]);
		old_fb = current_fb;
		current_fb = 1 - current_fb;

		if (sleep_between_modes && test_all_modes && !qr_code)
			sleep(sleep_between_modes);

		if (do_dpms) {
			kmstest_set_connector_dpms(drm_fd, c->connector, do_dpms);
			sleep(sleep_between_modes);
			kmstest_set_connector_dpms(drm_fd, c->connector, DRM_MODE_DPMS_ON);
		}

		if (qr_code){
			set_single();
			pause();
		}
	}

	if (test_all_modes)
		igt_remove_fb(drm_fd, &fb_info[old_fb]);

	drmModeFreeEncoder(c->encoder);
	drmModeFreeConnector(c->connector);
}

struct box {
	int x, y, width, height;
};

struct stereo_fb_layout {
	int fb_width, fb_height;
	struct box left, right;
};

static void box_init(struct box *box, int x, int y, int bwidth, int bheight)
{
	box->x = x;
	box->y = y;
	box->width = bwidth;
	box->height = bheight;
}

static void stereo_fb_layout_from_mode(struct stereo_fb_layout *layout,
				       drmModeModeInfo *mode)
{
	unsigned int format = mode->flags & DRM_MODE_FLAG_3D_MASK;
	const int hdisplay = mode->hdisplay, vdisplay = mode->vdisplay;
	int middle;

	switch (format) {
	case DRM_MODE_FLAG_3D_TOP_AND_BOTTOM:
		layout->fb_width = hdisplay;
		layout->fb_height = vdisplay;

		middle = vdisplay / 2;
		box_init(&layout->left, 0, 0, hdisplay, middle);
		box_init(&layout->right,
			 0, middle, hdisplay, vdisplay - middle);
		break;
	case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_HALF:
		layout->fb_width = hdisplay;
		layout->fb_height = vdisplay;

		middle = hdisplay / 2;
		box_init(&layout->left, 0, 0, middle, vdisplay);
		box_init(&layout->right,
			 middle, 0, hdisplay - middle, vdisplay);
		break;
	case DRM_MODE_FLAG_3D_FRAME_PACKING:
	{
		int vactive_space = mode->vtotal - vdisplay;

		layout->fb_width = hdisplay;
		layout->fb_height = 2 * vdisplay + vactive_space;

		box_init(&layout->left,
			 0, 0, hdisplay, vdisplay);
		box_init(&layout->right,
			 0, vdisplay + vactive_space, hdisplay, vdisplay);
		break;
	}
	default:
		igt_assert(0);
	}
}

static const char *stereo_mode_str(drmModeModeInfo *mode)
{
	unsigned int layout = mode->flags & DRM_MODE_FLAG_3D_MASK;

	switch (layout) {
	case DRM_MODE_FLAG_3D_TOP_AND_BOTTOM:
		return "TB";
	case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_HALF:
		return "SbSH";
	case DRM_MODE_FLAG_3D_FRAME_PACKING:
		return "FP";
	default:
		igt_assert(0);
	}
}

static uint32_t create_stereo_fb(drmModeModeInfo *mode, struct igt_fb *fb)
{
	struct stereo_fb_layout layout;
	cairo_t *cr;
	uint32_t fb_id;

	stereo_fb_layout_from_mode(&layout, mode);
	fb_id = igt_create_fb(drm_fd, layout.fb_width, layout.fb_height,
				  igt_bpp_depth_to_drm_format(bpp, depth),
				  enable_tiling, fb);
	cr = igt_get_cairo_ctx(drm_fd, fb);

	igt_paint_image(cr, IGT_DATADIR"/1080p-left.png",
			    layout.left.x, layout.left.y,
			    layout.left.width, layout.left.height);
	igt_paint_image(cr, IGT_DATADIR"/1080p-right.png",
			    layout.right.x, layout.right.y,
			    layout.right.width, layout.right.height);

	cairo_destroy(cr);

	{
		char buffer[64];

		snprintf(buffer, sizeof(buffer), "%dx%d@%dHz-%s.png",
			 mode->hdisplay,
			 mode->vdisplay,
			 mode->vrefresh,
			 stereo_mode_str(mode));

		igt_write_fb_to_png(drm_fd, fb, buffer);
	}

	return fb_id;
}

static void do_set_stereo_mode(struct connector *c)
{
	uint32_t fb_id;
	struct igt_fb fb_info;

	fb_id = create_stereo_fb(&c->mode, &fb_info);

	if (drmModeSetCrtc(drm_fd, c->crtc, fb_id, 0, 0,
			   &c->id, 1, &c->mode)) {
		fprintf(stderr, "failed to set mode (%dx%d@%dHz): %s\n",
			width, height, c->mode.vrefresh,
			strerror(errno));
	}
}

static void
set_stereo_mode(struct connector *c)
{
	int i, n;


	if (specified_mode_num != -1)
		n = 1;
	else
		n = c->connector->count_modes;

	for (i = 0; i < n; i++) {
		if (specified_mode_num == -1)
			c->mode = c->connector->modes[i];

		if (!c->mode_valid)
			continue;

		if (!(c->mode.flags & DRM_MODE_FLAG_3D_MASK))
			continue;

		fprintf(stdout, "CRTC(%u): [%d]", c->crtc, i);
		kmstest_dump_mode(&c->mode);
		do_set_stereo_mode(c);

		if (qr_code) {
			set_single();
			pause();
		} else if (sleep_between_modes)
			sleep(sleep_between_modes);

		if (do_dpms) {
			kmstest_set_connector_dpms(drm_fd, c->connector, DRM_MODE_DPMS_OFF);
			sleep(sleep_between_modes);
			kmstest_set_connector_dpms(drm_fd, c->connector, DRM_MODE_DPMS_ON);
		}
	}

	drmModeFreeEncoder(c->encoder);
	drmModeFreeConnector(c->connector);
}

/*
 * Re-probe outputs and light up as many as possible.
 *
 * On Intel, we have two CRTCs that we can drive independently with
 * different timings and scanout buffers.
 *
 * Each connector has a corresponding encoder, except in the SDVO case
 * where an encoder may have multiple connectors.
 */
int update_display(void)
{
	struct connector *connectors;
	int c;

	resources = drmModeGetResources(drm_fd);
	if (!resources) {
		fprintf(stderr, "drmModeGetResources failed: %s\n",
			strerror(errno));
		return 0;
	}

	connectors = calloc(resources->count_connectors,
			    sizeof(struct connector));
	if (!connectors)
		return 0;

	if (test_preferred_mode || test_all_modes ||
	    force_mode || specified_disp_id != -1) {
		unsigned long crtc_idx_mask = -1UL;

		/* Find any connected displays */
		for (c = 0; c < resources->count_connectors; c++) {
			struct connector *connector = &connectors[c];

			connector->id = resources->connectors[c];
			if (specified_disp_id != -1 &&
			    connector->id != specified_disp_id)
				continue;

			connector_find_preferred_mode(connector->id,
						      crtc_idx_mask,
						      specified_mode_num,
						      connector);
			if (!connector->mode_valid)
				continue;

			set_mode(connector);

			if (test_preferred_mode || force_mode ||
			    specified_mode_num != -1)
				crtc_idx_mask &= ~(1 << connector->crtc_idx);

		}
	}

	if (test_stereo_modes) {
		for (c = 0; c < resources->count_connectors; c++) {
			struct connector *connector = &connectors[c];

			connector->id = resources->connectors[c];
			if (specified_disp_id != -1 &&
			    connector->id != specified_disp_id)
				continue;

			connector_find_preferred_mode(connector->id,
						      -1UL,
						      specified_mode_num,
						      connector);
			if (!connector->mode_valid)
				continue;

			set_stereo_mode(connector);
		}
	}

	free(connectors);
	drmModeFreeResources(resources);
	return 1;
}

static char optstr[] = "3hiaf:s:d:p:mrto:j:";

static void __attribute__((noreturn)) usage(char *name)
{
	fprintf(stderr, "usage: %s [-hiasdpmtf]\n", name);
	fprintf(stderr, "\t-i\tdump info\n");
	fprintf(stderr, "\t-a\ttest all modes\n");
	fprintf(stderr, "\t-s\t<duration>\tsleep between each mode test\n");
	fprintf(stderr, "\t-d\t<depth>\tbit depth of scanout buffer\n");
	fprintf(stderr, "\t-p\t<planew,h>,<crtcx,y>,<crtcw,h> test overlay plane\n");
	fprintf(stderr, "\t-m\ttest the preferred mode\n");
	fprintf(stderr, "\t-3\ttest all 3D modes\n");
	fprintf(stderr, "\t-t\tuse a tiled framebuffer\n");
	fprintf(stderr, "\t-j\tdo dpms off, optional arg to select dpms leve (1-3)\n");
	fprintf(stderr, "\t-r\tprint a QR code on the screen whose content is \"pass\" for the automatic test\n");
	fprintf(stderr, "\t-o\t<id of the display>,<number of the mode>\tonly test specified mode on the specified display\n");
	fprintf(stderr, "\t-f\t<clock MHz>,<hdisp>,<hsync-start>,<hsync-end>,<htotal>,\n");
	fprintf(stderr, "\t\t<vdisp>,<vsync-start>,<vsync-end>,<vtotal>\n");
	fprintf(stderr, "\t\ttest force mode\n");
	fprintf(stderr, "\tDefault is to test all modes.\n");
	exit(0);
}

#define dump_resource(res) if (res) dump_##res()

static void cleanup_and_exit(int ret)
{
	close(drm_fd);
	exit(ret);
}

static gboolean input_event(GIOChannel *source, GIOCondition condition,
				gpointer data)
{
	gchar buf[2];
	gsize count;

	count = read(g_io_channel_unix_get_fd(source), buf, sizeof(buf));
	if (buf[0] == 'q' && (count == 1 || buf[1] == '\n')) {
		cleanup_and_exit(0);
	}

	return TRUE;
}

static void enter_exec_path( char **argv )
{
	char *exec_path = NULL;
	char *pos = NULL;
	short len_path = 0;
	int ret;

	len_path = strlen( argv[0] );
	exec_path = (char*) malloc(len_path);

	memcpy(exec_path, argv[0], len_path);
	pos = strrchr(exec_path, '/');
	if (pos != NULL)
		*(pos+1) = '\0';

	ret = chdir(exec_path);
	igt_assert(ret == 0);
	free(exec_path);
}

static void restore_termio_mode(int sig)
{
	tcsetattr(tio_fd, TCSANOW, &saved_tio);
	close(tio_fd);
}

static void set_termio_mode(void)
{
	struct termios tio;

	tio_fd = dup(STDIN_FILENO);
	tcgetattr(tio_fd, &saved_tio);
	igt_install_exit_handler(restore_termio_mode);
	tio = saved_tio;
	tio.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(tio_fd, TCSANOW, &tio);
}

int main(int argc, char **argv)
{
	int c;
	int ret = 0;
	GIOChannel *stdinchannel;
	GMainLoop *mainloop;
	float force_clock;
	bool opt_dump_info = false;

	igt_skip_on_simulation();

	enter_exec_path( argv );

	while ((c = getopt(argc, argv, optstr)) != -1) {
		switch (c) {
		case '3':
			test_stereo_modes = 1;
			break;
		case 'i':
			opt_dump_info = true;
			break;
		case 'a':
			test_all_modes = 1;
			break;
		case 'f':
			force_mode = 1;
			if(sscanf(optarg,"%f,%hu,%hu,%hu,%hu,%hu,%hu,%hu,%hu",
				&force_clock,&force_timing.hdisplay, &force_timing.hsync_start,&force_timing.hsync_end,&force_timing.htotal,
				&force_timing.vdisplay, &force_timing.vsync_start, &force_timing.vsync_end, &force_timing.vtotal)!= 9)
				usage(argv[0]);
			force_timing.clock = force_clock*1000;

			break;
		case 's':
			sleep_between_modes = atoi(optarg);
			break;
		case 'j':
			do_dpms = atoi(optarg);
			if (do_dpms == 0)
				do_dpms = DRM_MODE_DPMS_OFF;
			break;
		case 'd':
			depth = atoi(optarg);
			fprintf(stderr, "using depth %d\n", depth);
			break;
		case 'p':
			if (sscanf(optarg, "%d,%d,%d,%d,%d,%d", &plane_width,
				   &plane_height, &crtc_x, &crtc_y,
				   &crtc_w, &crtc_h) != 6)
				usage(argv[0]);
			test_plane = 1;
			break;
		case 'm':
			test_preferred_mode = 1;
			break;
		case 't':
			enable_tiling = 1;
			break;
		case 'r':
			qr_code = 1;
			break;
		case 'o':
			sscanf(optarg, "%d,%d", &specified_disp_id, &specified_mode_num);
			break;
		default:
			/* fall through */
		case 'h':
			usage(argv[0]);
			break;
		}
	}

	set_termio_mode();

	if (depth <= 8)
		bpp = 8;
	else if (depth <= 16)
		bpp = 16;
	else if (depth <= 32)
		bpp = 32;

	if (!test_all_modes && !force_mode && !test_preferred_mode &&
	    specified_mode_num == -1 && !test_stereo_modes)
		test_all_modes = 1;

	drm_fd = drm_open_any();

	if (test_stereo_modes &&
	    drmSetClientCap(drm_fd, DRM_CLIENT_CAP_STEREO_3D, 1) < 0) {
		fprintf(stderr, "DRM_CLIENT_CAP_STEREO_3D failed\n");
		goto out_close;
	}

	if (opt_dump_info) {
		dump_info();
		goto out_close;
	}

	igt_set_vt_graphics_mode();

	mainloop = g_main_loop_new(NULL, FALSE);
	if (!mainloop) {
		fprintf(stderr, "failed to create glib mainloop\n");
		ret = -1;
		goto out_close;
	}

	if (!testdisplay_setup_hotplug()) {
		fprintf(stderr, "failed to initialize hotplug support\n");
		goto out_mainloop;
	}

	stdinchannel = g_io_channel_unix_new(0);
	if (!stdinchannel) {
		fprintf(stderr, "failed to create stdin GIO channel\n");
		goto out_hotplug;
	}

	ret = g_io_add_watch(stdinchannel, G_IO_IN | G_IO_ERR, input_event,
			     NULL);
	if (ret < 0) {
		fprintf(stderr, "failed to add watch on stdin GIO channel\n");
		goto out_stdio;
	}

	ret = 0;

	if (!update_display()) {
		ret = 1;
		goto out_stdio;
	}

	if (test_all_modes)
		goto out_stdio;

	g_main_loop_run(mainloop);

out_stdio:
	g_io_channel_shutdown(stdinchannel, TRUE, NULL);
out_hotplug:
	testdisplay_cleanup_hotplug();
out_mainloop:
	g_main_loop_unref(mainloop);
out_close:
	close(drm_fd);

	return ret;
}
