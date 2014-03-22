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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Ben Widawsky <ben@bwidawsk.net>
 *
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include "intel_chipset.h"
#include "intel_io.h"
#include "drmtest.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#if HAVE_UDEV
#include <libudev.h>
#include <syslog.h>
#endif
#include "intel_l3_parity.h"

static unsigned int devid;
/* L3 size is always a function of banks. The number of banks cannot be
 * determined by number of slices however */
static inline int num_banks(void) {
	if (IS_HSW_GT3(devid))
		return 8; /* 4 per each slice */
	else if (IS_HSW_GT1(devid) ||
			devid == PCI_CHIP_IVYBRIDGE_GT1 ||
			devid == PCI_CHIP_IVYBRIDGE_M_GT1)
		return 2;
	else
		return 4;
}
#define NUM_SUBBANKS 8
#define BYTES_PER_BANK (128 << 10)
/* Each row addresses [up to] 4b. This multiplied by the number of subbanks
 * will give the L3 size per bank.
 * TODO: Row size is fixed on IVB, and variable on HSW.*/
#define MAX_ROW (1<<12)
#define MAX_BANKS_PER_SLICE 4
#define NUM_REGS (MAX_BANKS_PER_SLICE * NUM_SUBBANKS)
#define MAX_SLICES (IS_HSW_GT3(devid) ? 2 : 1)
#define REAL_MAX_SLICES 2
/* TODO support SLM config */
#define L3_SIZE ((MAX_ROW * 4) * NUM_SUBBANKS *  num_banks())

struct __attribute__ ((__packed__)) l3_log_register {
	uint32_t row0_enable	: 1;
	uint32_t rsvd2		: 4;
	uint32_t row0		: 11;
	uint32_t row1_enable	: 1;
	uint32_t rsvd1		: 4;
	uint32_t row1		: 11;
} l3logs[REAL_MAX_SLICES][MAX_BANKS_PER_SLICE][NUM_SUBBANKS];

static int which_slice = -1;
#define for_each_slice(__i) \
	for ((__i) = (which_slice == -1) ? 0 : which_slice; \
			(__i) < ((which_slice == -1) ? MAX_SLICES : (which_slice + 1)); \
			(__i)++)

static void decode_dft(uint32_t dft)
{
	if (IS_IVYBRIDGE(devid) || !(dft & 1)) {
		printf("Error injection disabled\n");
		return;
	}
	printf("Error injection enabled\n");
	printf("  Hang = %s\n", (dft >> 28) & 0x1 ? "yes" : "no");
	printf("  Row = %d\n", (dft >> 7) & 0x7ff);
	printf("  Bank = %d\n", (dft >> 2) & 0x3);
	printf("  Subbank = %d\n", (dft >> 4) & 0x7);
	printf("  Slice = %d\n", (dft >> 1) & 0x1);
}

static void dumpit(int slice)
{
	int i, j;

	for (i = 0; i < MAX_BANKS_PER_SLICE; i++) {
		for (j = 0; j < NUM_SUBBANKS; j++) {
			struct l3_log_register *reg = &l3logs[slice][i][j];

			if (reg->row0_enable)
				printf("Slice %d, Row %d, Bank %d, Subbank %d is disabled\n",
				       slice, reg->row0, i, j);
			if (reg->row1_enable)
				printf("Slice %d, Row %d, Bank %d, Subbank %d is disabled\n",
				       slice, reg->row1, i, j);
		}
	}
}

static int disable_rbs(int row, int bank, int sbank, int slice)
{
	struct l3_log_register *reg = &l3logs[slice][bank][sbank];

	// can't map more than 2 rows
	if (reg->row0_enable && reg->row1_enable)
		return -1;

	// can't remap the same row twice
	if ((reg->row0_enable && reg->row0 == row) ||
	    (reg->row1_enable && reg->row1 == row)) {
		return -1;
	}

	if (reg->row0_enable) {
		reg->row1 = row;
		reg->row1_enable = 1;
	} else {
		reg->row0 = row;
		reg->row0_enable = 1;
	}

	return 0;
}

static void enables_rbs(int row, int bank, int sbank, int slice)
{
	struct l3_log_register *reg = &l3logs[slice][bank][sbank];

	if (!reg->row0_enable && !reg->row1_enable)
		return;

	if (reg->row1_enable && reg->row1 == row)
		reg->row1_enable = 0;
	else if (reg->row0_enable && reg->row0 == row)
		reg->row0_enable = 0;
}

static void usage(const char *name)
{
	printf("usage: %s [OPTIONS] [ACTION]\n"
		"Operate on the i915 L3 GPU cache (should be run as root)\n\n"
		" OPTIONS:\n"
		"  -r, --row=[row]			The row to act upon (default 0)\n"
		"  -b, --bank=[bank]			The bank to act upon (default 0)\n"
		"  -s, --subbank=[subbank]		The subbank to act upon (default 0)\n"
		"  -w, --slice=[slice]			Which slice to act on (default: -1 [all])\n"
		"    , --daemon				Run the listener (-L) as a daemon\n"
		" ACTIONS (only 1 may be specified at a time):\n"
		"  -h, --help				Display this help\n"
		"  -H, --hw-info				Display the current L3 properties\n"
		"  -l, --list				List the current L3 logs\n"
		"  -a, --clear-all			Clear all disabled rows\n"
		"  -e, --enable				Enable row, bank, subbank (undo -d)\n"
		"  -d, --disable=<row,bank,subbank>	Disable row, bank, subbank (inline arguments are deprecated. Please use -r, -b, -s instead\n"
		"  -i, --inject				[HSW only] Cause hardware to inject a row errors\n"
		"  -u, --uninject			[HSW only] Turn off hardware error injectection (undo -i)\n"
		"  -L, --listen				Listen for uevent errors\n",
		name);
}

int main(int argc, char *argv[])
{
	const int device = drm_get_card();
	char *path[REAL_MAX_SLICES];
	uint32_t dft;
	int row = 0, bank = 0, sbank = 0;
	int fd[REAL_MAX_SLICES] = {0}, ret, i;
	int action = '0';
	int drm_fd = drm_open_any();
	int daemonize = 0;
	devid = intel_get_drm_devid(drm_fd);

	if (intel_gen(devid) < 7 || IS_VALLEYVIEW(devid))
		exit(EXIT_SUCCESS);

	assert(intel_register_access_init(intel_get_pci_device(), 0) == 0);

	ret = asprintf(&path[0], "/sys/class/drm/card%d/l3_parity", device);
	assert(ret != -1);
	ret = asprintf(&path[1], "/sys/class/drm/card%d/l3_parity_slice_1", device);
	assert(ret != -1);

	for_each_slice(i) {
		fd[i] = open(path[i], O_RDWR);
		assert(fd[i]);
		ret = read(fd[i], l3logs[i], NUM_REGS * sizeof(uint32_t));
		if (ret == -1) {
			perror("Reading sysfs");
			exit(EXIT_FAILURE);
		}
		assert(lseek(fd[i], 0, SEEK_SET) == 0);
	}

	/* NB: It is potentially unsafe to read this register if the kernel is
	 * actively using this register range, or we're running multiple
	 * instances of this tool. Since neither of those cases should occur
	 * (and the tool should be root only) we can safely ignore this for
	 * now. Just be aware of this if for some reason a hang is reported
	 * when using this tool.
	 */
	dft = intel_register_read(0xb038);

	while (1) {
		int c, option_index = 0;
		struct option long_options[] = {
			{ "help", no_argument, 0, 'h' },
			{ "list", no_argument, 0, 'l' },
			{ "clear-all", no_argument, 0, 'a' },
			{ "enable", no_argument, 0, 'e' },
			{ "disable", optional_argument, 0, 'd' },
			{ "inject", no_argument, 0, 'i' },
			{ "uninject", no_argument, 0, 'u' },
			{ "hw-info", no_argument, 0, 'H' },
			{ "listen", no_argument, 0, 'L' },
			{ "row", required_argument, 0, 'r' },
			{ "bank", required_argument, 0, 'b' },
			{ "subbank", required_argument, 0, 's' },
			{ "slice", required_argument, 0, 'w' },
			{ "daemon", no_argument, &daemonize, 1 },
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "hHr:b:s:w:aled::iuL", long_options,
				&option_index);
		if (c == -1)
			break;

		if (c == 0)
			continue;

		switch (c) {
			case '?':
			case 'h':
				usage(argv[0]);
				exit(EXIT_SUCCESS);
			case 'H':
				printf("Number of slices: %d\n", MAX_SLICES);
				printf("Number of banks: %d\n", num_banks());
				printf("Subbanks per bank: %d\n", NUM_SUBBANKS);
				printf("Max L3 size: %dK\n", L3_SIZE >> 10);
				printf("Has error injection: %s\n", IS_HASWELL(devid) ? "yes" : "no");
				exit(EXIT_SUCCESS);
			case 'r':
				row = atoi(optarg);
				if (row >= MAX_ROW)
					exit(EXIT_FAILURE);
				break;
			case 'b':
				bank = atoi(optarg);
				if (bank >= num_banks() || bank >= MAX_BANKS_PER_SLICE)
					exit(EXIT_FAILURE);
				break;
			case 's':
				sbank = atoi(optarg);
				if (sbank >= NUM_SUBBANKS)
					exit(EXIT_FAILURE);
				break;
			case 'w':
				which_slice = atoi(optarg);
				if (which_slice >= MAX_SLICES)
					exit(EXIT_FAILURE);
				break;
			case 'i':
			case 'u':
				if (!IS_HASWELL(devid)) {
					fprintf(stderr, "Error injection supported on HSW+ only\n");
					exit(EXIT_FAILURE);
				}
			case 'd':
				if (optarg) {
					ret = sscanf(optarg, "%d,%d,%d", &row, &bank, &sbank);
					if (ret != 3)
						exit(EXIT_FAILURE);
				}
			case 'a':
			case 'l':
			case 'e':
			case 'L':
				if (action != '0') {
					fprintf(stderr, "Only one action may be specified\n");
					exit(EXIT_FAILURE);
				}
				action = c;
				break;
			default:
				abort();
		}
	}

	if (action == 'i') {
		if (((dft >> 1) & 1) != which_slice) {
			fprintf(stderr, "DFT register already has slice %d enabled, and we don't support multiple slices. Try modifying -w; but sometimes the register sticks in the wrong way\n", (dft >> 1) & 1);
			exit(EXIT_FAILURE);
		}

		if (which_slice == -1) {
			fprintf(stderr, "Cannot inject errors to multiple slices (modify -w)\n");
			exit(EXIT_FAILURE);
		}
		if (dft & 1 && ((dft >> 1) && 1) == which_slice)
			printf("warning: overwriting existing injections. This is very dangerous.\n");
	}

	/* Daemon doesn't work like the other commands */
	if (action == 'L') {
#ifndef HAVE_UDEV
		fprintf(stderr, "Daemon requires udev support. Please reconfigure.\n");
		exit(EXIT_FAILURE);
#else
		struct l3_parity par;
		struct l3_location loc;
		if (daemonize) {
			assert(daemon(0, 0) == 0);
			openlog(argv[0], LOG_CONS | LOG_PID, LOG_USER);
		}
		memset(&par, 0, sizeof(par));
		assert(l3_uevent_setup(&par) == 0);
		assert(l3_listen(&par, daemonize == 1, &loc) == 0);
		exit(EXIT_SUCCESS);
#endif
	}

	if (action == 'l')
		decode_dft(dft);

	/* Per slice operations */
	for_each_slice(i) {
		switch (action) {
			case 'l':
				dumpit(i);
				break;
			case 'a':
				memset(l3logs[i], 0, NUM_REGS * sizeof(struct l3_log_register));
				break;
			case 'e':
				enables_rbs(row, bank, sbank, i);
				break;
			case 'd':
				assert(disable_rbs(row, bank, sbank, i) == 0);
				break;
			case 'i':
				if (bank == 3) {
					fprintf(stderr, "The hardware does not support error inject on bank 3.\n");
					exit(EXIT_FAILURE);
				}
				dft |= row << 7;
				dft |= sbank << 4;
				dft |= bank << 2;
				assert(i < 2);
				dft |= i << 1; /* slice */
				dft |= 1 << 0; /* enable */
				intel_register_write(0xb038, dft);
				break;
			case 'u':
				intel_register_write(0xb038, dft & ~(1<<0));
				break;
			case 'L':
				break;
			default:
				abort();
		}
	}

	intel_register_access_fini();
	if (action == 'l')
		exit(EXIT_SUCCESS);

	for_each_slice(i) {
		ret = write(fd[i], l3logs[i], NUM_REGS * sizeof(uint32_t));
		if (ret == -1) {
			perror("Writing sysfs");
			exit(EXIT_FAILURE);
		}
		close(fd[i]);
	}


	exit(EXIT_SUCCESS);
}
