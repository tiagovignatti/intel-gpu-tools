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
#include <sys/ioctl.h>
#include "intel_gpu_tools.h"
#include "instdone.h"

#define SAMPLES_PER_SEC             10000
#define SAMPLES_TO_PERCENT_RATIO    (SAMPLES_PER_SEC / 100)

#define MAX_NUM_TOP_BITS            100

struct top_bit {
	struct instdone_bit *bit;
	int count;
} top_bits[MAX_NUM_TOP_BITS];
struct top_bit *top_bits_sorted[MAX_NUM_TOP_BITS];

static uint32_t instdone, instdone1;

static const char *bars[] = {
	" ",
	"▏",
	"▎",
	"▍",
	"▌",
	"▋",
	"▊",
	"▉",
	"█"
};

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
	uint32_t reg_val;

	if (top_bit->bit->reg == INST_DONE_1)
		reg_val = instdone1;
	else
		reg_val = instdone;

	if ((reg_val & top_bit->bit->bit) == 0)
		top_bit->count++;
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

#define PERCENTAGE_BAR_END	79
static void
print_percentage_bar(float percent, int cur_line_len)
{
	int bar_avail_len = (PERCENTAGE_BAR_END - cur_line_len - 1) * 8;
	int bar_len = bar_avail_len * (percent + .5) / 100.0;
	int i;

	for (i = bar_len; i >= 8; i -= 8) {
		printf("%s", bars[8]);
		cur_line_len++;
	}
	if (i) {
		printf("%s", bars[i]);
		cur_line_len++;
	}

	/* NB: We can't use a field width with utf8 so we manually
	* guarantee a field with of 45 chars for any bar. */
	printf("%*s\n", PERCENTAGE_BAR_END - cur_line_len, "");
}

int main(int argc, char **argv)
{
	intel_get_mmio();
	uint32_t ring_size;
	int i;

	init_instdone_definitions();
	for (i = 0; i < num_instdone_bits; i++) {
		top_bits[i].bit = &instdone_bits[i];
		top_bits[i].count = 0;
		top_bits_sorted[i] = &top_bits[i];
	}

	ring_size = ((INREG(LP_RING + RING_LEN) & RING_NR_PAGES) >> 12) * 4096;

	for (;;) {
		int j;
		char clear_screen[] = {0x1b, '[', 'H',
				       0x1b, '[', 'J',
				       0x0};
		int total_ring_full = 0;
		int ring_idle = 0;
		int percent;
		int len;

		for (i = 0; i < SAMPLES_PER_SEC; i++) {
			uint32_t ring_head, ring_tail;
			int ring_full;

			if (IS_965(devid)) {
				instdone = INREG(INST_DONE_I965);
				instdone1 = INREG(INST_DONE_1);
			} else
				instdone = INREG(INST_DONE);

			for (j = 0; j < num_instdone_bits; j++)
				update_idle_bit(&top_bits[j]);

			ring_head = INREG(LP_RING + RING_HEAD) & HEAD_ADDR;
			ring_tail = INREG(LP_RING + RING_TAIL) & TAIL_ADDR;

			if (ring_tail == ring_head)
				ring_idle++;

			ring_full = ring_tail - ring_head;
			if (ring_full < 0)
				ring_full += ring_size;

			total_ring_full += ring_full;

			usleep(1000000 / SAMPLES_PER_SEC);
		}

		qsort(top_bits_sorted, num_instdone_bits,
		      sizeof(struct top_bit *), top_bits_sort);

		/* Limit the number of lines printed to the terminal height so the
		 * most important info (at the top) will stay on screen. */
		unsigned short int max_lines = -1;
		struct winsize ws;
		if (ioctl(0, TIOCGWINSZ, &ws) != -1)
			max_lines = ws.ws_row - 6; /* exclude header lines */

		printf("%s", clear_screen);

		print_clock_info();

		percent = ring_idle / SAMPLES_TO_PERCENT_RATIO;
		len = printf("%30s: %3d%%: ", "ring idle", percent);
		print_percentage_bar (percent, len);

		printf("%30s: %d/%d (%d%%)\n", "ring space",
		       total_ring_full / SAMPLES_PER_SEC,
		       ring_size,
		       (total_ring_full / SAMPLES_TO_PERCENT_RATIO) / ring_size);

		printf("%30s  %s\n\n", "task", "percent busy");
		for (i = 0; i < num_instdone_bits; i++) {
			if (top_bits_sorted[i]->count < 1)
				break;

			if (i < max_lines) {
				percent = top_bits_sorted[i]->count / SAMPLES_TO_PERCENT_RATIO;
				len = printf("%30s: %3d%%: ",
					     top_bits_sorted[i]->bit->name,
					     percent);
				print_percentage_bar (percent, len);
			}

			top_bits_sorted[i]->count = 0;
		}
	}

	return 0;
}
