/*
 * Copyright © 2007 Intel Corporation
 * Copyright © 2011 Intel Corporation
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
 *    Eugeni Dodonov <eugeni.dodonov@intel.com>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#include "intel_io.h"
#include "instdone.h"
#include "intel_reg.h"
#include "intel_chipset.h"

#define  FORCEWAKE	    0xA18C
#define  FORCEWAKE_ACK	    0x130090

#define SAMPLES_PER_SEC             10000
#define SAMPLES_TO_PERCENT_RATIO    (SAMPLES_PER_SEC / 100)

#define MAX_NUM_TOP_BITS            100

#define HAS_STATS_REGS(devid)		IS_965(devid)

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

enum stats_counts {
	IA_VERTICES,
	IA_PRIMITIVES,
	VS_INVOCATION,
	GS_INVOCATION,
	GS_PRIMITIVES,
	CL_INVOCATION,
	CL_PRIMITIVES,
	PS_INVOCATION,
	PS_DEPTH,
	STATS_COUNT
};

const uint32_t stats_regs[STATS_COUNT] = {
	IA_VERTICES_COUNT_QW,
	IA_PRIMITIVES_COUNT_QW,
	VS_INVOCATION_COUNT_QW,
	GS_INVOCATION_COUNT_QW,
	GS_PRIMITIVES_COUNT_QW,
	CL_INVOCATION_COUNT_QW,
	CL_PRIMITIVES_COUNT_QW,
	PS_INVOCATION_COUNT_QW,
	PS_DEPTH_COUNT_QW,
};

const char *stats_reg_names[STATS_COUNT] = {
	"vert fetch",
	"prim fetch",
	"VS invocations",
	"GS invocations",
	"GS prims",
	"CL invocations",
	"CL prims",
	"PS invocations",
	"PS depth pass",
};

uint64_t stats[STATS_COUNT];
uint64_t last_stats[STATS_COUNT];

static unsigned long
gettime(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return (t.tv_usec + (t.tv_sec * 1000000));
}

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

	if (top_bit->bit->reg == INSTDONE_1)
		reg_val = instdone1;
	else
		reg_val = instdone;

	if ((reg_val & top_bit->bit->bit) == 0)
		top_bit->count++;
}

static void
print_clock(const char *name, int clock) {
	if (clock == -1)
		printf("%s clock: unknown", name);
	else
		printf("%s clock: %d Mhz", name, clock);
}

static int
print_clock_info(struct pci_device *pci_dev)
{
	uint32_t devid = pci_dev->device_id;
	uint16_t gcfgc;

	if (IS_GM45(devid)) {
		int core_clock = -1;

		pci_device_cfg_read_u16(pci_dev, &gcfgc, I915_GCFGC);

		switch (gcfgc & 0xf) {
		case 8:
			core_clock = 266;
			break;
		case 9:
			core_clock = 320;
			break;
		case 11:
			core_clock = 400;
			break;
		case 13:
			core_clock = 533;
			break;
		}
		print_clock("core", core_clock);
	} else if (IS_965(devid) && IS_MOBILE(devid)) {
		int render_clock = -1, sampler_clock = -1;

		pci_device_cfg_read_u16(pci_dev, &gcfgc, I915_GCFGC);

		switch (gcfgc & 0xf) {
		case 2:
			render_clock = 250; sampler_clock = 267;
			break;
		case 3:
			render_clock = 320; sampler_clock = 333;
			break;
		case 4:
			render_clock = 400; sampler_clock = 444;
			break;
		case 5:
			render_clock = 500; sampler_clock = 533;
			break;
		}

		print_clock("render", render_clock);
		printf("  ");
		print_clock("sampler", sampler_clock);
	} else if (IS_945(devid) && IS_MOBILE(devid)) {
		int render_clock = -1, display_clock = -1;

		pci_device_cfg_read_u16(pci_dev, &gcfgc, I915_GCFGC);

		switch (gcfgc & 0x7) {
		case 0:
			render_clock = 166;
			break;
		case 1:
			render_clock = 200;
			break;
		case 3:
			render_clock = 250;
			break;
		case 5:
			render_clock = 400;
			break;
		}

		switch (gcfgc & 0x70) {
		case 0:
			display_clock = 200;
			break;
		case 4:
			display_clock = 320;
			break;
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
		case 0:
			render_clock = 160;
			break;
		case 1:
			render_clock = 190;
			break;
		case 4:
			render_clock = 333;
			break;
		}
		if (gcfgc & (1 << 13))
		    render_clock = 133;

		switch (gcfgc & 0x70) {
		case 0:
			display_clock = 190;
			break;
		case 4:
			display_clock = 333;
			break;
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

#define STATS_LEN (20)
#define PERCENTAGE_BAR_END	(79 - STATS_LEN)

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
	printf("%*s", PERCENTAGE_BAR_END - cur_line_len, "");
}

struct ring {
	const char *name;
	uint32_t mmio;
	int head, tail, size;
	uint64_t full;
	int idle;
};

static uint32_t ring_read(struct ring *ring, uint32_t reg)
{
	return INREG(ring->mmio + reg);
}

static void ring_init(struct ring *ring)
{
	ring->size = (((ring_read(ring, RING_LEN) & RING_NR_PAGES) >> 12) + 1) * 4096;
}

static void ring_reset(struct ring *ring)
{
	ring->idle = ring->full = 0;
}

static void ring_sample(struct ring *ring)
{
	int full;

	if (!ring->size)
		return;

	ring->head = ring_read(ring, RING_HEAD) & HEAD_ADDR;
	ring->tail = ring_read(ring, RING_TAIL) & TAIL_ADDR;

	if (ring->tail == ring->head)
		ring->idle++;

	full = ring->tail - ring->head;
	if (full < 0)
		full += ring->size;
	ring->full += full;
}

static void ring_print_header(FILE *out, struct ring *ring)
{
    fprintf(out, "%.6s%%\tops\t",
            ring->name
          );
}

static void ring_print(struct ring *ring, unsigned long samples_per_sec)
{
	int percent_busy, len;

	if (!ring->size)
		return;

	percent_busy = 100 - 100 * ring->idle / samples_per_sec;

	len = printf("%25s busy: %3d%%: ", ring->name, percent_busy);
	print_percentage_bar (percent_busy, len);
	printf("%24s space: %d/%d\n",
		   ring->name,
		   (int)(ring->full / samples_per_sec),
		   ring->size);
}

static void ring_log(struct ring *ring, unsigned long samples_per_sec,
		FILE *output)
{
	if (ring->size)
		fprintf(output, "%3d\t%d\t",
			(int)(100 - 100 * ring->idle / samples_per_sec),
			(int)(ring->full / samples_per_sec));
	else
		fprintf(output, "-1\t-1\t");
}

static void
usage(const char *appname)
{
	printf("intel_gpu_top - Display a top-like summary of Intel GPU usage\n"
			"\n"
			"usage: %s [parameters]\n"
			"\n"
			"The following parameters apply:\n"
			"[-s <samples>]       samples per seconds (default %d)\n"
			"[-e <command>]       command to profile\n"
			"[-o <file>]          output statistics to file. If file is '-',"
			"                     run in batch mode and output statistics to stdio only \n"
			"[-h]                 show this help screen\n"
			"\n",
			appname,
			SAMPLES_PER_SEC
		  );
	return;
}

int main(int argc, char **argv)
{
	uint32_t devid;
	struct pci_device *pci_dev;
	struct ring render_ring = {
		.name = "render",
		.mmio = 0x2030,
	}, bsd_ring = {
		.name = "bitstream",
		.mmio = 0x4030,
	}, bsd6_ring = {
		.name = "bitstream",
		.mmio = 0x12030,
	}, blt_ring = {
		.name = "blitter",
		.mmio = 0x22030,
	};
	int i, ch;
	int samples_per_sec = SAMPLES_PER_SEC;
	FILE *output = NULL;
	double elapsed_time=0;
	int print_headers=1;
	pid_t child_pid=-1;
	int child_stat;
	char *cmd=NULL;
	int interactive=1;

	/* Parse options? */
	while ((ch = getopt(argc, argv, "s:o:e:h")) != -1) {
		switch (ch) {
		case 'e': cmd = strdup(optarg);
			break;
		case 's': samples_per_sec = atoi(optarg);
			if (samples_per_sec < 100) {
				fprintf(stderr, "Error: samples per second must be >= 100\n");
				exit(1);
			}
			break;
		case 'o':
			if (!strcmp(optarg, "-")) {
				/* Running in non-interactive mode */
				interactive = 0;
				output = stdout;
			}
			else
				output = fopen(optarg, "w");
			if (!output)
			{
				perror("fopen");
				exit(1);
			}
			break;
		case 'h':
			usage(argv[0]);
			exit(0);
			break;
		default:
			fprintf(stderr, "Invalid flag %c!\n", (char)optopt);
			usage(argv[0]);
			exit(1);
			break;
		}
	}

	pci_dev = intel_get_pci_device();
	devid = pci_dev->device_id;
	intel_mmio_use_pci_bar(pci_dev);
	init_instdone_definitions(devid);

	/* Do we have a command to run? */
	if (cmd != NULL) {
		if (output) {
			fprintf(output, "# Profiling: %s\n", cmd);
			fflush(output);
		}
		child_pid = fork();
		if (child_pid < 0) {
			perror("fork");
			exit(1);
		}
		else if (child_pid == 0) {
			int res;
			res = system(cmd);
			if (res < 0)
				perror("running command");
			if (output) {
				fflush(output);
				fprintf(output, "# %s exited with status %d\n", cmd, res);
				fflush(output);
			}
			free(cmd);
			exit(0);
		} else {
			free(cmd);
		}
	}

	for (i = 0; i < num_instdone_bits; i++) {
		top_bits[i].bit = &instdone_bits[i];
		top_bits[i].count = 0;
		top_bits_sorted[i] = &top_bits[i];
	}

	/* Grab access to the registers */
	intel_register_access_init(pci_dev, 0);

	ring_init(&render_ring);
	if (IS_GEN4(devid) || IS_GEN5(devid))
		ring_init(&bsd_ring);
	if (IS_GEN6(devid) || IS_GEN7(devid)) {
		ring_init(&bsd6_ring);
		ring_init(&blt_ring);
	}

	/* Initialize GPU stats */
	if (HAS_STATS_REGS(devid)) {
		for (i = 0; i < STATS_COUNT; i++) {
			uint32_t stats_high, stats_low, stats_high_2;

			do {
				stats_high = INREG(stats_regs[i] + 4);
				stats_low = INREG(stats_regs[i]);
				stats_high_2 = INREG(stats_regs[i] + 4);
			} while (stats_high != stats_high_2);

			last_stats[i] = (uint64_t)stats_high << 32 |
				stats_low;
		}
	}

	for (;;) {
		int j;
		unsigned long long t1, ti, tf, t2;
		unsigned long long def_sleep = 1000000 / samples_per_sec;
		unsigned long long last_samples_per_sec = samples_per_sec;
		unsigned short int max_lines;
		struct winsize ws;
		char clear_screen[] = {0x1b, '[', 'H',
				       0x1b, '[', 'J',
				       0x0};
		int percent;
		int len;

		t1 = gettime();

		ring_reset(&render_ring);
		ring_reset(&bsd_ring);
		ring_reset(&bsd6_ring);
		ring_reset(&blt_ring);

		for (i = 0; i < samples_per_sec; i++) {
			long long interval;
			ti = gettime();
			if (IS_965(devid)) {
				instdone = INREG(INSTDONE_I965);
				instdone1 = INREG(INSTDONE_1);
			} else
				instdone = INREG(INSTDONE);

			for (j = 0; j < num_instdone_bits; j++)
				update_idle_bit(&top_bits[j]);

			ring_sample(&render_ring);
			ring_sample(&bsd_ring);
			ring_sample(&bsd6_ring);
			ring_sample(&blt_ring);

			tf = gettime();
			if (tf - t1 >= 1000000) {
				/* We are out of sync, bail out */
				last_samples_per_sec = i+1;
				break;
			}
			interval = def_sleep - (tf - ti);
			if (interval > 0)
				usleep(interval);
		}

		if (HAS_STATS_REGS(devid)) {
			for (i = 0; i < STATS_COUNT; i++) {
				uint32_t stats_high, stats_low, stats_high_2;

				do {
					stats_high = INREG(stats_regs[i] + 4);
					stats_low = INREG(stats_regs[i]);
					stats_high_2 = INREG(stats_regs[i] + 4);
				} while (stats_high != stats_high_2);

				stats[i] = (uint64_t)stats_high << 32 |
					stats_low;
			}
		}

		qsort(top_bits_sorted, num_instdone_bits,
		      sizeof(struct top_bit *), top_bits_sort);

		/* Limit the number of lines printed to the terminal height so the
		 * most important info (at the top) will stay on screen. */
		max_lines = -1;
		if (ioctl(0, TIOCGWINSZ, &ws) != -1)
			max_lines = ws.ws_row - 6; /* exclude header lines */
		if (max_lines >= num_instdone_bits)
			max_lines = num_instdone_bits;

		t2 = gettime();
		elapsed_time += (t2 - t1) / 1000000.0;

		if (interactive) {
			printf("%s", clear_screen);
			print_clock_info(pci_dev);

			ring_print(&render_ring, last_samples_per_sec);
			ring_print(&bsd_ring, last_samples_per_sec);
			ring_print(&bsd6_ring, last_samples_per_sec);
			ring_print(&blt_ring, last_samples_per_sec);

			printf("\n%30s  %s\n", "task", "percent busy");
			for (i = 0; i < max_lines; i++) {
				if (top_bits_sorted[i]->count > 0) {
					percent = (top_bits_sorted[i]->count * 100) /
						last_samples_per_sec;
					len = printf("%30s: %3d%%: ",
							 top_bits_sorted[i]->bit->name,
							 percent);
					print_percentage_bar (percent, len);
				} else {
					printf("%*s", PERCENTAGE_BAR_END, "");
				}

				if (i < STATS_COUNT && HAS_STATS_REGS(devid)) {
					printf("%13s: %llu (%lld/sec)",
						   stats_reg_names[i],
						   (long long)stats[i],
						   (long long)(stats[i] - last_stats[i]));
					last_stats[i] = stats[i];
				} else {
					if (!top_bits_sorted[i]->count)
						break;
				}
				printf("\n");
			}
		}
		if (output) {
			/* Print headers for columns at first run */
			if (print_headers) {
				fprintf(output, "# time\t");
				ring_print_header(output, &render_ring);
				ring_print_header(output, &bsd_ring);
				ring_print_header(output, &bsd6_ring);
				ring_print_header(output, &blt_ring);
				for (i = 0; i < MAX_NUM_TOP_BITS; i++) {
					if (i < STATS_COUNT && HAS_STATS_REGS(devid)) {
						fprintf(output, "%.6s\t",
							   stats_reg_names[i]
							   );
					}
					if (!top_bits[i].count)
						continue;
				}
				fprintf(output, "\n");
				print_headers = 0;
			}

			/* Print statistics */
			fprintf(output, "%.2f\t", elapsed_time);
			ring_log(&render_ring, last_samples_per_sec, output);
			ring_log(&bsd_ring, last_samples_per_sec, output);
			ring_log(&bsd6_ring, last_samples_per_sec, output);
			ring_log(&blt_ring, last_samples_per_sec, output);

			for (i = 0; i < MAX_NUM_TOP_BITS; i++) {
				if (i < STATS_COUNT && HAS_STATS_REGS(devid)) {
					fprintf(output, "%"PRIu64"\t",
						   stats[i] - last_stats[i]);
					last_stats[i] = stats[i];
				}
					if (!top_bits[i].count)
						continue;
			}
			fprintf(output, "\n");
			fflush(output);
		}

		for (i = 0; i < num_instdone_bits; i++) {
			top_bits_sorted[i]->count = 0;

			if (i < STATS_COUNT)
				last_stats[i] = stats[i];
		}

		/* Check if child has gone */
		if (child_pid > 0) {
			int res;
			if ((res = waitpid(child_pid, &child_stat, WNOHANG)) == -1) {
				perror("waitpid");
				exit(1);
			}
			if (res == 0)
				continue;
			if (WIFEXITED(child_stat))
				break;
		}
	}

	fclose(output);

	intel_register_access_fini();
	return 0;
}
