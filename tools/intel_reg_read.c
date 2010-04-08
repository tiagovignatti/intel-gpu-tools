/*
 * Copyright © 2010 Intel Corporation
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
 *	Zhenyu Wang <zhenyuw@linux.intel.com>
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <string.h>
#include "intel_gpu_tools.h"

static void dump_range(uint32_t start, uint32_t end)
{
	int i;

	for (i = start; i < end; i += 4)
		printf("0x%X : 0x%X\n", i,
		       *(volatile uint32_t *)((volatile char*)mmio + i));
}

int main(int argc, char** argv)
{
	uint32_t reg;

	if (argc != 2) {
		printf("Usage: %s [-f | addr]\n", argv[0]);
		printf("\t -f : read back full range of registers.\n");
		printf("\t      WARNING! This could be danger to hang the machine!\n");
		printf("\t addr : in 0xXXXX format\n");
		exit(1);
	}

	intel_get_mmio(intel_get_pci_device());

	if (!strcmp(argv[1], "-f")) {
		dump_range(0x00000, 0x00fff);   /* VGA registers */
		dump_range(0x02000, 0x02fff);   /* instruction, memory, interrupt control registers */
		dump_range(0x03000, 0x031ff);   /* FENCE and PPGTT control registers */
		dump_range(0x03200, 0x03fff);   /* frame buffer compression registers */
		dump_range(0x05000, 0x05fff);   /* I/O control registers */
		dump_range(0x06000, 0x06fff);   /* clock control registers */
		dump_range(0x07000, 0x07fff);   /* 3D internal debug registers */
		dump_range(0x07400, 0x088ff);   /* GPE debug registers */
		dump_range(0x0a000, 0x0afff);   /* display palette registers */
		dump_range(0x10000, 0x13fff);   /* MMIO MCHBAR */
		dump_range(0x30000, 0x3ffff);   /* overlay registers */
		dump_range(0x60000, 0x6ffff);   /* display engine pipeline registers */
		dump_range(0x70000, 0x72fff);   /* display and cursor registers */
		dump_range(0x73000, 0x73fff);   /* performance counters */
	} else {
		sscanf(argv[1], "0x%x", &reg);
		dump_range(reg, reg + 4);
	}

	return 0;
}

