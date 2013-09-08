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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include "intel_chipset.h"
#include "intel_gpu_tools.h"
#include "drmtest.h"

static unsigned int devid;
/* L3 size is always a function of banks. The number of banks cannot be
 * determined by number of slices however */
#define MAX_BANKS 4
#define NUM_BANKS \
	((devid == PCI_CHIP_IVYBRIDGE_GT1 || devid == PCI_CHIP_IVYBRIDGE_M_GT1) ? 2 : 4)
#define NUM_SUBBANKS 8
#define BYTES_PER_BANK (128 << 10)
/* Each row addresses [up to] 4b. This multiplied by the number of subbanks
 * will give the L3 size per bank.
 * TODO: Row size is fixed on IVB, and variable on HSW.*/
#define MAX_ROW (1<<12)
#define L3_SIZE ((MAX_ROW * 4) * NUM_SUBBANKS *  NUM_BANKS)
#define NUM_REGS (NUM_BANKS * NUM_SUBBANKS)

struct __attribute__ ((__packed__)) l3_log_register {
	uint32_t row0_enable	: 1;
	uint32_t rsvd2		: 4;
	uint32_t row0		: 11;
	uint32_t row1_enable	: 1;
	uint32_t rsvd1		: 4;
	uint32_t row1		: 11;
} l3log[MAX_BANKS][NUM_SUBBANKS];

static void dumpit(void)
{
	int i, j;

	for (i = 0; i < NUM_BANKS; i++) {
		for (j = 0; j < NUM_SUBBANKS; j++) {
			struct l3_log_register *reg = &l3log[i][j];

			if (reg->row0_enable)
				printf("Row %d, Bank %d, Subbank %d is disabled\n",
				       reg->row0, i, j);
			if (reg->row1_enable)
				printf("Row %d, Bank %d, Subbank %d is disabled\n",
				       reg->row1, i, j);
		}
	}
}

static int disable_rbs(int row, int bank, int sbank)
{
	struct l3_log_register *reg = &l3log[bank][sbank];

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

static void enables_rbs(int row, int bank, int sbank)
{
	struct l3_log_register *reg = &l3log[bank][sbank];

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
		" ACTIONS (only 1 may be specified at a time):\n"
		"  -h, --help				Display this help\n"
		"  -H, --hw-info				Display the current L3 properties\n"
		"  -l, --list				List the current L3 logs\n"
		"  -a, --clear-all			Clear all disabled rows\n"
		"  -e, --enable				Enable row, bank, subbank (undo -d)\n"
		"  -d, --disable=<row,bank,subbank>	Disable row, bank, subbank (inline arguments are deprecated. Please use -r, -b, -s instead\n",
		name);
}

int main(int argc, char *argv[])
{
	const int device = drm_get_card();
	char *path;
	int row = 0, bank = 0, sbank = 0;
	int drm_fd, fd, ret;
	int action = '0';

	drm_fd = drm_open_any();
	devid = intel_get_drm_devid(drm_fd);

	if (intel_gen(devid) < 7 || IS_VALLEYVIEW(devid))
		exit(EXIT_SUCCESS);

	ret = asprintf(&path, "/sys/class/drm/card%d/l3_parity", device);
	assert(ret != -1);

	fd = open(path, O_RDWR);
	assert(fd != -1);

	ret = read(fd, l3log, NUM_REGS * sizeof(uint32_t));
	if (ret == -1) {
		perror("Reading sysfs");
		exit(EXIT_FAILURE);
	}

	assert(lseek(fd, 0, SEEK_SET) == 0);

	while (1) {
		int c, option_index = 0;
		static struct option long_options[] = {
			{ "help", no_argument, 0, 'h' },
			{ "list", no_argument, 0, 'l' },
			{ "clear-all", no_argument, 0, 'a' },
			{ "enable", no_argument, 0, 'e' },
			{ "disable", optional_argument, 0, 'd' },
			{ "hw-info", no_argument, 0, 'H' },
			{ "row", required_argument, 0, 'r' },
			{ "bank", required_argument, 0, 'b' },
			{ "subbank", required_argument, 0, 's' },
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "hHr:b:s:aled::", long_options,
				&option_index);
		if (c == -1)
			break;

		switch (c) {
			case '?':
			case 'h':
				usage(argv[0]);
				exit(EXIT_SUCCESS);
			case 'H':
				printf("Number of banks: %d\n", NUM_BANKS);
				printf("Subbanks per bank: %d\n", NUM_SUBBANKS);
				printf("L3 size: %dK\n", L3_SIZE >> 10);
				exit(EXIT_SUCCESS);
			case 'r':
				row = atoi(optarg);
				if (row >= MAX_ROW)
					exit(EXIT_FAILURE);
				break;
			case 'b':
				bank = atoi(optarg);
				if (bank >= NUM_BANKS)
					exit(EXIT_FAILURE);
				break;
			case 's':
				sbank = atoi(optarg);
				if (sbank >= NUM_SUBBANKS)
					exit(EXIT_FAILURE);
				break;
			case 'd':
				if (optarg) {
					ret = sscanf(optarg, "%d,%d,%d", &row, &bank, &sbank);
					if (ret != 3)
						exit(EXIT_FAILURE);
				}
			case 'a':
			case 'l':
			case 'e':
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

	switch (action) {
		case 'l':
			dumpit();
			exit(EXIT_SUCCESS);
		case 'a':
			memset(l3log, 0, sizeof(l3log));
			break;
		case 'e':
			enables_rbs(row, bank, sbank);
			break;
		case 'd':
			assert(disable_rbs(row, bank, sbank) == 0);
			break;
		default:
			abort();
	}

	ret = write(fd, l3log, NUM_REGS * sizeof(uint32_t));
	if (ret == -1) {
		perror("Writing sysfs");
		exit(EXIT_FAILURE);
	}

	close(fd);

	exit(EXIT_SUCCESS);
}
