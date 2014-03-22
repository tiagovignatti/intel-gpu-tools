/*
 * Copyright © 2012 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *      Paulo Zanoni <paulo.r.zanoni@intel.com>
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "intel_io.h"
#include "intel_chipset.h"
#include "intel_reg.h"

int gen;

uint32_t HTOTAL[]     = { 0x60000, 0x61000, 0x62000 };
uint32_t VTOTAL[]     = { 0x6000C, 0x6100C, 0x6200C };
uint32_t PIPECONF[]   = { 0x70008, 0x71008, 0x72008 };
uint32_t PIPESRC[]    = { 0x6001C, 0x6101C, 0x6201C };
uint32_t PF_CTRL1[]   = { 0x68080, 0x68880, 0x69080 };
uint32_t PF_WIN_POS[] = { 0x68070, 0x68870, 0x69070 };
uint32_t PF_WIN_SZ[]  = { 0x68074, 0x68874, 0x69074 };

#define PIPECONF_ENABLE         (1 << 31)
#define PIPECONF_INTERLACE_MASK (7 << 21)
#define PIPECONF_PF_PD          (0 << 21)
#define PIPECONF_PF_ID          (1 << 21)
#define PIPECONF_IF_ID          (3 << 21)

#define HTOTAL_ACTIVE_MASK (0xFFF << 0)
#define VTOTAL_ACTIVE_MASK (0xFFF << 0)

#define PIPESRC_HORIZ_MASK (0xFFF << 16)
#define PIPESRC_VERT_MASK  (0xFFF << 0)

/*#define PF_ENABLE    (1 << 31)*/
#define PF_PIPE_MASK   (3 << 29)
#define PF_FILTER_MASK (3 << 23)
#define PF_FILTER_MED  (1 << 23)
#define PF_PIPE_A      (0 << 29)
#define PF_PIPE_B      (1 << 29)
#define PF_PIPE_C      (2 << 29)

#define PF_WIN_SZ_X_MASK (0x1FFF << 16)
#define PF_WIN_SZ_Y_MASK (0xFFF << 0)

struct pipe_info {
	bool enabled;
	bool pf_enabled;
	uint32_t interlace_mode;
	uint32_t tot_width;  /* htotal */
	uint32_t tot_height; /* vtotal */
	uint32_t src_width;  /* pipesrc.x */
	uint32_t src_height; /* pipesrc.y */
	uint32_t dst_width;  /* pf_win_sz.x */
	uint32_t dst_height; /* pf_win_sz.y */
};

static void read_pipe_info(int intel_pipe, struct pipe_info *info)
{
	uint32_t conf, vtotal, htotal, src, ctrl1, win_sz;

	conf   = INREG(PIPECONF[intel_pipe]);
	htotal = INREG(HTOTAL[intel_pipe]);
	vtotal = INREG(VTOTAL[intel_pipe]);
	src    = INREG(PIPESRC[intel_pipe]);
	ctrl1  = INREG(PF_CTRL1[intel_pipe]);
	win_sz = INREG(PF_WIN_SZ[intel_pipe]);

	info->enabled = (conf & PIPECONF_ENABLE) ? true : false;
	info->tot_width = (htotal & HTOTAL_ACTIVE_MASK) + 1;
	info->tot_height = (vtotal & VTOTAL_ACTIVE_MASK) + 1;
	info->src_width = ((src & PIPESRC_HORIZ_MASK) >> 16) + 1;
	info->src_height = (src & PIPESRC_VERT_MASK) + 1;
	info->interlace_mode = conf & PIPECONF_INTERLACE_MASK;
	info->pf_enabled = ctrl1 & PF_ENABLE;
	info->dst_width = (win_sz & PF_WIN_SZ_X_MASK) >> 16;
	info->dst_height = win_sz & PF_WIN_SZ_Y_MASK;
}

static void dump_pipe(int intel_pipe)
{
	struct pipe_info info;

	read_pipe_info(intel_pipe, &info);

	printf("\nPipe %c:\n", intel_pipe + 'A');

	printf("- %s\n", info.enabled ? "enabled" : "disabled");
	if (!info.enabled)
		return;

	switch (info.interlace_mode) {
	case PIPECONF_PF_PD:
		printf("- progressive\n");
		break;
	case PIPECONF_PF_ID:
		printf("- interlaced (progressive fetch)\n");
		break;
	case PIPECONF_IF_ID:
		printf("- interlaced (interlaced fetch)\n");
		break;
	default:
		assert(0);
	}

	printf("- pf %s\n", info.pf_enabled ? "enabled" : "disabled");
	if (!info.pf_enabled)
		return;

	printf("- tot %dx%d\n", info.tot_width, info.tot_height);
	printf("- src %dx%d\n", info.src_width, info.src_height);
	printf("- dst %dx%d\n", info.dst_width, info.dst_height);
}

static void dump_info(void)
{
	int i;
	int pipes;

	if (gen < 7)
		pipes = 2;
	else
		pipes = 3;

	for (i = 0; i < pipes; i++) {
		dump_pipe(i);
	}
}

static int change_screen_size(int intel_pipe, int x, int y)
{
	struct pipe_info info;
	uint32_t dst_width, dst_height, pos_x, pos_y;
	uint32_t ctrl1_val;
	uint32_t win_pos_val;
	uint32_t win_sz_val;

	read_pipe_info(intel_pipe, &info);

	if (x == 0) {
		if (info.dst_width != 0)
			dst_width = info.dst_width;
		else
			dst_width = info.src_width;
	} else {
		dst_width = x;
	}

	if (y == 0) {
		if (info.dst_height != 0)
			dst_height = info.dst_height;
		else
			dst_height = info.src_height;
	} else {
		dst_height = y;
	}

	pos_x = abs((info.tot_width - dst_width)) / 2;
	pos_y = abs((info.tot_height - dst_height)) / 2;

	if (pos_x == 1)
		pos_x = 0;

	if (info.src_width / (double) dst_width > 1.125) {
		printf("X is too small\n");
		return 1;
	} else if (info.tot_width < dst_width) {
		printf("X is too big\n");
		return 1;
	} else if (dst_width & 1) {
		printf("X must be even\n");
		return 1;
	} else if (info.src_height / (double) dst_height > 1.125) {
		printf("Y is too small\n");
		return 1;
	} else if (info.tot_height < dst_height) {
		printf("Y is too big\n");
		return 1;
	} else if (dst_height & 1) {
		printf("Y must be even\n");
		return 1;
	}

	printf("Changing size for pipe %c:\n"
	       "- width:  %d -> %d\n"
	       "- height: %d -> %d\n"
	       "- pos: %dx%d\n",
	       intel_pipe + 'A', info.src_width, dst_width, info.src_height,
	       dst_height, pos_x, pos_y);

	ctrl1_val = PF_ENABLE | PF_FILTER_MED;

	/* This can break stuff if the panel fitter is already enabled for
	 * another pipe */
	if (gen >= 7) {
		switch (intel_pipe) {
		case 0:
			ctrl1_val |= PF_PIPE_A;
			break;
		case 1:
			ctrl1_val |= PF_PIPE_B;
			break;
		case 2:
			ctrl1_val |= PF_PIPE_C;
			break;
		default:
			assert(0);
		}
	}
	OUTREG(PF_CTRL1[intel_pipe], ctrl1_val);

	win_pos_val = pos_x << 16;
	win_pos_val |= pos_y;
	OUTREG(PF_WIN_POS[intel_pipe], win_pos_val);

	win_sz_val = dst_width << 16;
	win_sz_val |= dst_height;
	OUTREG(PF_WIN_SZ[intel_pipe], win_sz_val);

	return 0;
}

static int disable_panel_fitter(int intel_pipe)
{
	OUTREG(PF_CTRL1[intel_pipe], 0);
	OUTREG(PF_WIN_POS[intel_pipe], 0);
	OUTREG(PF_WIN_SZ[intel_pipe], 0);
	return 0;
}

static void print_usage(void)
{
	printf("Options:\n"
"  -p pipe:    pipe to be used (A, B or C)\n"
"  -x value:   final screen width size in pixels\n"
"  -y value:   final screen height size in pixels\n"
"  -d:         disable panel fitter\n"
"  -l:         list the current state of each pipe\n"
"  -h:         prints this message\n");
}

int main (int argc, char *argv[])
{
	int opt;
	int ret = 0;
	char intel_pipe = '\0';
	int x = 0, y = 0;
	bool do_disable = false, do_dump = false, do_usage = false;
	struct pci_device *pci_dev;
	uint32_t devid;

	printf("WARNING:\n"
	       "This tool is a workaround for people that don't have a Kernel "
	       "with overscan compensation properties: it is just a temporary "
	       "solution that may or may not work. Use it at your own risk.\n");

	pci_dev = intel_get_pci_device();
	intel_register_access_init(pci_dev, 0);
	devid = pci_dev->device_id;

	if (!HAS_PCH_SPLIT(devid)) {
		printf("This tool was only tested on Ironlake and newer\n");
		ret = 1;
		goto out;
	}
	if (IS_GEN5(devid))
		gen = 5;
	else if (IS_GEN6(devid))
		gen = 6;
	else
		gen = 7;

	while ((opt = getopt(argc, argv, "p:x:y:dlh")) != -1) {
		switch (opt) {
		case 'p':
			intel_pipe = optarg[0];
			if (intel_pipe != 'A' && intel_pipe != 'B' &&
			    (gen <= 6 || intel_pipe != 'C')) {
				printf("Invalid pipe\n");
				ret = 1;
				goto out;
			}
			break;
		case 'x':
			x = atoi(optarg);
			break;
		case 'y':
			y = atoi(optarg);
			break;
		case 'd':
			do_disable = true;
			break;
		case 'l':
			do_dump = true;
			break;
		case 'h':
			do_usage = true;
			break;
		default:
			do_usage = true;
			ret = 1;
		}
	}

	if (do_usage) {
		print_usage();
	} else if (do_dump) {
		dump_info();
	} else if (intel_pipe) {
		if (do_disable)
			ret = disable_panel_fitter(intel_pipe - 'A');
		else
			ret = change_screen_size(intel_pipe - 'A', x, y);
	} else {
		print_usage();
		ret = 1;
	}

out:
	intel_register_access_fini();
	return ret;
}
