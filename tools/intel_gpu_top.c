/*
 * Copyright © 2007 Intel Corporation
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
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include "intel_gpu_tools.h"

#define MAX_NUM_TOP_BITS	100

struct top_bit {
	/* initial setup */
	uint32_t *reg;
	uint32_t bit;
	char *name;
	void (*update)(struct top_bit *top_bit);

	/* runtime */
	int count;
} top_bits[MAX_NUM_TOP_BITS];
struct top_bit *top_bits_sorted[MAX_NUM_TOP_BITS];

int num_top_bits;

uint32_t instdone;

static int
top_bits_sort(const void *a, const void *b)
{
	struct top_bit * const *bit_a = a;
	struct top_bit * const *bit_b = b;
	int a_count = (*bit_a)->count;
	int b_count = (*bit_b)->count;

	if (a_count < b_count)
		return 1;
	else if (a_count == b_count)
		return 0;
	else
		return -1;
}

static void
update_idle_bit(struct top_bit *top_bit)
{
	if ((*top_bit->reg & top_bit->bit) == 0)
		top_bit->count++;
}

static void
add_instdone_bit(uint32_t bit, char *name)
{
	top_bits[num_top_bits].reg = &instdone;
	top_bits[num_top_bits].bit = bit;
	top_bits[num_top_bits].name = name;
	top_bits[num_top_bits].update = update_idle_bit;
	top_bits_sorted[num_top_bits] = &top_bits[num_top_bits];
	num_top_bits++;
}

static void
print_clock(char *name, int clock) {
	if (clock == -1)
		printf("%s clock: unknown", name);
	else
		printf("%s clock: %d Mhz", name, clock);
}

static int
print_clock_info(void)
{
	uint16_t gcfgc;

	if (IS_GM45(devid)) {
		int core_clock = -1;

		pci_device_cfg_read_u16(pci_dev, &gcfgc, I915_GCFGC);

		switch (gcfgc & 0xf) {
		case 8: core_clock = 266;
		case 9: core_clock = 320;
		case 11: core_clock = 400;
		case 13: core_clock = 533;
		}
		print_clock("core", core_clock);
	} else if (IS_965(devid) && IS_MOBILE(devid)) {
		int render_clock = -1, sampler_clock = -1;

		pci_device_cfg_read_u16(pci_dev, &gcfgc, I915_GCFGC);

		switch (gcfgc & 0xf) {
		case 2: render_clock = 250; sampler_clock = 267;
		case 3: render_clock = 320; sampler_clock = 333;
		case 4: render_clock = 400; sampler_clock = 444;
		case 5: render_clock = 500; sampler_clock = 533;
		}

		print_clock("render", render_clock);
		printf("  ");
		print_clock("sampler", sampler_clock);
	} else if (IS_945(devid) && IS_MOBILE(devid)) {
		int render_clock = -1, display_clock = -1;

		pci_device_cfg_read_u16(pci_dev, &gcfgc, I915_GCFGC);

		switch (gcfgc & 0x7) {
		case 0: render_clock = 166;
		case 1: render_clock = 200;
		case 3: render_clock = 250;
		case 5: render_clock = 400;
		}

		switch (gcfgc & 0x70) {
		case 0: display_clock = 200;
		case 4: display_clock = 320;
		}
		if (gcfgc & (1 << 7))
		    display_clock = 133;

		print_clock("render", render_clock);
		printf("  ");
		print_clock("display", display_clock);
	} else if (IS_915(devid) && IS_MOBILE(devid)) {
		int render_clock = -1, display_clock = -1;

		pci_device_cfg_read_u16(pci_dev, &gcfgc, I915_GCFGC);

		switch (gcfgc & 0x7) {
		case 0: render_clock = 160;
		case 1: render_clock = 190;
		case 4: render_clock = 333;
		}
		if (gcfgc & (1 << 13))
		    render_clock = 133;

		switch (gcfgc & 0x70) {
		case 0: display_clock = 190;
		case 4: display_clock = 333;
		}
		if (gcfgc & (1 << 7))
		    display_clock = 133;

		print_clock("render", render_clock);
		printf("  ");
		print_clock("display", display_clock);
	}


	printf("\n");
	return -1;
}

int main(int argc, char **argv)
{
	intel_get_mmio();
	uint32_t ring_size;

	if (IS_965(devid)) {
		add_instdone_bit(I965_ROW_0_EU_0_DONE, "Row 0, EU 0");
		add_instdone_bit(I965_ROW_0_EU_1_DONE, "Row 0, EU 1");
		add_instdone_bit(I965_ROW_0_EU_2_DONE, "Row 0, EU 2");
		add_instdone_bit(I965_ROW_0_EU_3_DONE, "Row 0, EU 3");
		add_instdone_bit(I965_ROW_1_EU_0_DONE, "Row 1, EU 0");
		add_instdone_bit(I965_ROW_1_EU_1_DONE, "Row 1, EU 1");
		add_instdone_bit(I965_ROW_1_EU_2_DONE, "Row 1, EU 2");
		add_instdone_bit(I965_ROW_1_EU_3_DONE, "Row 1, EU 3");
		add_instdone_bit(I965_SF_DONE, "Strips and Fans");
		add_instdone_bit(I965_SE_DONE, "Setup Engine");
		add_instdone_bit(I965_WM_DONE, "Windowizer");
		add_instdone_bit(I965_DISPATCHER_DONE, "Dispatcher");
		add_instdone_bit(I965_PROJECTION_DONE, "Projection and LOD");
		add_instdone_bit(I965_DG_DONE, "Dependent address generator");
		add_instdone_bit(I965_QUAD_CACHE_DONE, "Texture fetch");
		add_instdone_bit(I965_TEXTURE_FETCH_DONE, "Texture fetch");
		add_instdone_bit(I965_TEXTURE_DECOMPRESS_DONE, "Texture decompress");
		add_instdone_bit(I965_SAMPLER_CACHE_DONE, "Sampler cache");
		add_instdone_bit(I965_FILTER_DONE, "Filtering");
		add_instdone_bit(I965_BYPASS_DONE, "Bypass FIFO");
		add_instdone_bit(I965_PS_DONE, "Pixel shader");
		add_instdone_bit(I965_CC_DONE, "Color calculator");
		add_instdone_bit(I965_MAP_FILTER_DONE, "Map filter");
		add_instdone_bit(I965_MAP_L2_IDLE, "Map L2");
		add_instdone_bit(I965_MA_ROW_0_DONE, "Message Arbiter row 0");
		add_instdone_bit(I965_MA_ROW_1_DONE, "Message Arbiter row 1");
		add_instdone_bit(I965_IC_ROW_0_DONE, "Instruction cache row 0");
		add_instdone_bit(I965_IC_ROW_1_DONE, "Instruction cache row 1");
		add_instdone_bit(I965_CP_DONE, "Command Processor");
	} else if (IS_9XX(devid)) {
		add_instdone_bit(IDCT_DONE, "IDCT");
		add_instdone_bit(IQ_DONE, "IQ");
		add_instdone_bit(PR_DONE, "PR");
		add_instdone_bit(VLD_DONE, "VLD");
		add_instdone_bit(IP_DONE, "Instruction parser");
		add_instdone_bit(FBC_DONE, "Framebuffer Compression");
		add_instdone_bit(BINNER_DONE, "Binner");
		add_instdone_bit(SF_DONE, "Strips and fans");
		add_instdone_bit(SE_DONE, "Setup engine");
		add_instdone_bit(WM_DONE, "Windowizer");
		add_instdone_bit(IZ_DONE, "Intermediate Z");
		add_instdone_bit(PERSPECTIVE_INTERP_DONE, "Perspective interpolation");
		add_instdone_bit(DISPATCHER_DONE, "Dispatcher");
		add_instdone_bit(PROJECTION_DONE, "Projection and LOD");
		add_instdone_bit(DEPENDENT_ADDRESS_DONE, "Dependent address calculation");
		add_instdone_bit(TEXTURE_FETCH_DONE, "Texture fetch");
		add_instdone_bit(TEXTURE_DECOMPRESS_DONE, "Texture decompression");
		add_instdone_bit(SAMPLER_CACHE_DONE, "Sampler Cache");
		add_instdone_bit(FILTER_DONE, "Filtering");
		add_instdone_bit(BYPASS_FIFO_DONE, "Bypass FIFO");
		add_instdone_bit(PS_DONE, "Pixel shader");
		add_instdone_bit(CC_DONE, "Color calculator");
		add_instdone_bit(MAP_FILTER_DONE, "Map filter");
		add_instdone_bit(MAP_L2_IDLE, "Map L2");
	}

	ring_size = ((INREG(LP_RING + RING_LEN) & RING_NR_PAGES) >> 12) * 4096;

	for (;;) {
		int i, j;
		char clear_screen[] = {0x1b, '[', 'H',
				       0x1b, '[', 'J',
				       0x0};
		int total_ring_full = 0;
		int ring_idle = 0;

		for (i = 0; i < 100; i++) {
			uint32_t ring_head, ring_tail;
			int ring_full;

			if (IS_965(devid))
				instdone = INREG(INST_DONE_I965);
			else
				instdone = INREG(INST_DONE);

			for (j = 0; j < num_top_bits; j++)
				top_bits[j].update(&top_bits[j]);

			ring_head = INREG(LP_RING + RING_HEAD) & HEAD_ADDR;
			ring_tail = INREG(LP_RING + RING_TAIL) & TAIL_ADDR;

			if (ring_tail == ring_head)
				ring_idle++;

			ring_full = ring_tail - ring_head;
			if (ring_full < 0)
				ring_full += ring_size;

			total_ring_full += ring_full;

			usleep(10000);
		}

		qsort(top_bits_sorted, num_top_bits, sizeof(struct top_bit *),
		      top_bits_sort);

		printf(clear_screen);

		print_clock_info();

		printf("ring idle: %3d%%  ring space: %d/%d (%d%%)\n",
		       ring_idle,
		       total_ring_full / 100, ring_size,
		       total_ring_full / ring_size);
		printf("%30s  %s\n\n", "task", "percent busy");
		for (i = 0; i < num_top_bits; i++) {
			if (top_bits_sorted[i]->count == 0)
				break;

			printf("%30s: %d%%\n",
			       top_bits_sorted[i]->name,
			       top_bits_sorted[i]->count);

			top_bits_sorted[i]->count = 0;
		}
	}

	return 0;
}
