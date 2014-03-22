/*
 * Copyright © 2013 Intel Corporation
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
 *	Ville Syrjälä <ville.syrjala@linux.intel.com>
 *
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/io.h>
#include "intel_io.h"
#include "intel_chipset.h"

static uint8_t read_reg(uint32_t reg, bool use_mmio)
{
	if (use_mmio)
		return *((volatile uint8_t *)mmio + reg);
	else
		return inb(reg);
}

static void usage(const char *cmdname)
{
	printf("Usage: %s [-m] [addr1] [addr2] .. [addrN]\n", cmdname);
	printf("\t -m : use MMIO instead of port IO\n");
	printf("\t addr : in 0xXXXX format\n");
}

int main(int argc, char *argv[])
{
	bool use_mmio = false;
	int ret = 0;
	uint32_t reg;
	int i, ch;
	const char *cmdname = argv[0];

	while ((ch = getopt(argc, argv, "m")) != -1) {
		switch(ch) {
		case 'm':
			use_mmio = true;
			break;
		default:
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		usage(cmdname);
		return 1;
	}

	if (use_mmio)
		intel_register_access_init(intel_get_pci_device(), 0);
	else
		assert(iopl(3) == 0);

	for (i = 0; i < argc; i++) {
		sscanf(argv[i], "0x%x", &reg);
		printf("0x%X : 0x%X\n", reg, read_reg(reg, use_mmio));
	}

	if (use_mmio)
		intel_register_access_fini();
	else
		iopl(0);

	return ret;
}

