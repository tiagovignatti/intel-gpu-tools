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
#include "intel_chipset.h"
#include "intel_gpu_tools.h"
#include "drmtest.h"

#define NUM_BANKS 4
#define NUM_SUBBANKS 8
#define NUM_REGS (NUM_BANKS * NUM_SUBBANKS)

struct __attribute__ ((__packed__)) l3_log_register {
	uint32_t row0_enable	: 1;
	uint32_t rsvd2		: 4;
	uint32_t row0		: 11;
	uint32_t row1_enable	: 1;
	uint32_t rsvd1		: 4;
	uint32_t row1		: 11;
} l3log[NUM_BANKS][NUM_SUBBANKS];

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

static int do_parse(int argc, char *argv[])
{
	int row, bank, sbank, i, ret;

	for (i = 1; i < argc; i++) {
		ret = sscanf(argv[i], "%d,%d,%d", &row, &bank, &sbank);
		if (ret != 3)
			return i;
		assert(disable_rbs(row, bank, sbank) == 0);
	}
	return 0;
}

int main(int argc, char *argv[])
{
	const int device = drm_get_card(0);
	char *path;
	unsigned int devid;
	int drm_fd, fd, ret;

	drm_fd = drm_open_any();
	devid = intel_get_drm_devid(drm_fd);

	ret = asprintf(&path, "/sys/class/drm/card%d/l3_parity", device);
	assert(ret != -1);

	fd = open(path, O_RDWR);
	if (fd == -1 && IS_IVYBRIDGE(devid)) {
		perror("Opening sysfs");
		exit(EXIT_FAILURE);
	} else if (fd == -1)
		exit(EXIT_SUCCESS);

	ret = read(fd, l3log, NUM_REGS * sizeof(uint32_t));
	if (ret == -1) {
		perror("Reading sysfs");
		exit(EXIT_FAILURE);
	}

	assert(lseek(fd, 0, SEEK_SET) == 0);

	if (argc == 1) {
		dumpit();
		exit(EXIT_SUCCESS);
	} else if (!strncmp("-c", argv[1], 2)) {
		memset(l3log, 0, sizeof(l3log));
	} else {
		ret = do_parse(argc, argv);
		if (ret != 0) {
			fprintf(stderr, "Malformed command line at %s\n", argv[ret]);
			exit(EXIT_FAILURE);
		}
	}

	ret = write(fd, l3log, NUM_REGS * sizeof(uint32_t));
	if (ret == -1) {
		perror("Writing sysfs");
		exit(EXIT_FAILURE);
	}

	close(fd);

	exit(EXIT_SUCCESS);
}
