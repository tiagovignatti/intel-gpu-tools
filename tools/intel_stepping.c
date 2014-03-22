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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pciaccess.h>
#include <err.h>
#include "intel_chipset.h"
#include "intel_io.h"
#include "intel_reg.h"

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

int main(int argc, char **argv)
{
	struct pci_device *dev, *bridge;
	int error;
	uint8_t stepping;
	const char *step_desc = "??";

	error = pci_system_init();
	if (error != 0) {
		fprintf(stderr, "Couldn't initialize PCI system: %s\n",
			strerror(error));
		exit(1);
	}

	/* Grab the graphics card */
	dev = pci_device_find_by_slot(0, 0, 2, 0);
	if (dev == NULL)
		errx(1, "Couldn't find graphics card");

	error = pci_device_probe(dev);
	if (error != 0) {
		fprintf(stderr, "Couldn't probe graphics card: %s\n",
			strerror(error));
		exit(1);
	}

	if (dev->vendor_id != 0x8086)
		errx(1, "Graphics card is non-intel");

	bridge = pci_device_find_by_slot(0, 0, 0, 0);
	if (dev == NULL)
		errx(1, "Couldn't bridge");

	error = pci_device_cfg_read_u8(bridge, &stepping, 8);
	if (error != 0) {
		fprintf(stderr, "Couldn't read revision ID: %s\n",
			strerror(error));
		exit(1);
	}

	switch (dev->device_id) {
	case PCI_CHIP_I915_G:
		if (stepping < 0x04)
			step_desc = "<B1";
		else if (stepping == 0x04)
			step_desc = "B1";
		else if (stepping == 0x0e)
			step_desc = "C2";
		else if (stepping > 0x0e)
			step_desc = ">C2";
		else
			step_desc = ">B1 <C2";
		break;
	case PCI_CHIP_I915_GM:
		if (stepping < 0x03)
			step_desc = "<B1";
		else if (stepping == 0x03)
			step_desc = "B1/C0";
		else if (stepping == 0x04)
			step_desc = "C1/C2";
		else
			step_desc = ">C2";
		break;
	case PCI_CHIP_I945_GM:
		if (stepping < 0x03)
			step_desc = "<A3";
		else if (stepping == 0x03)
			step_desc = "A3";
		else
			step_desc = ">A3";
		break;
	case PCI_CHIP_I965_G:
	case PCI_CHIP_I965_Q:
		if (stepping < 0x02)
			step_desc = "<C1";
		else if (stepping == 0x02)
			step_desc = "C1/C2";
		else
			step_desc = ">C2";
		break;
	case PCI_CHIP_I965_GM:
		if (stepping < 0x03)
			step_desc = "<C0";
		else if (stepping == 0x03)
			step_desc = "C0";
		else
			step_desc = ">C0";
		break;
	case PCI_CHIP_I965_G_1:
		if (stepping < 0x03)
			step_desc = "<E0";
		else if (stepping == 0x03)
			step_desc = "E0";
		else
			step_desc = ">E0";
		break;
	case PCI_CHIP_GM45_GM:
		if (stepping < 0x07)
			step_desc = "<B3";
		else if (stepping == 0x03)
			step_desc = "B3";
		else
			step_desc = ">B3";
		break;
	case PCI_CHIP_G45_G:
	case PCI_CHIP_Q45_G:
	case PCI_CHIP_G41_G:
		if (stepping < 0x02)
			step_desc = "<A2";
		else if (stepping == 0x02)
			step_desc = "A2";
		else if (stepping == 0x03)
			step_desc = "A3";
		else
			step_desc = ">A3";
		break;
	}

	printf("Vendor: 0x%04x, Device: 0x%04x, Revision: 0x%02x (%s)\n",
	       dev->vendor_id,
	       dev->device_id,
	       stepping,
	       step_desc);

	print_clock_info(dev);

	return 0;
}
