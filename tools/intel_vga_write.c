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

static void write_reg(uint32_t reg, uint8_t val, bool use_mmio)
{
	if (use_mmio)
		*((volatile uint8_t *)mmio + reg) = val;
	else
		outb(val, reg);
}

static void usage(const char *cmdname)
{
	printf("Usage: %s [-m] addr value\n", cmdname);
	printf("\t -m : use MMIO instead of port IO\n");
	printf("\t addr,value : in 0xXXXX format\n");
}

int main(int argc, char *argv[])
{
	bool use_mmio = false;
	int ret = 0;
	uint32_t reg, val;
	int ch;
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

	if (argc < 2) {
		usage(cmdname);
		return 1;
	}

	sscanf(argv[0], "0x%x", &reg);
	sscanf(argv[1], "0x%x", &val);

	if (use_mmio)
		intel_register_access_init(intel_get_pci_device(), 0);
	else
		assert(iopl(3) == 0);

	write_reg(reg, val, use_mmio);

	if (use_mmio)
		intel_register_access_fini();
	else
		iopl(0);

	return ret;
}

