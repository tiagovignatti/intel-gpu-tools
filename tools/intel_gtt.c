/*
 * Copyright © 2008 Intel Corporation
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

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pciaccess.h>
#include <unistd.h>

#include "intel_io.h"
#include "intel_chipset.h"

#define KB(x) ((x) * 1024)
#define MB(x) ((x) * 1024 * 1024)
unsigned char *gtt;
uint32_t devid;

#define INGTT(offset) (*(volatile uint32_t *)(gtt + (offset) / (KB(4) / 4)))
static uint64_t get_phys(uint32_t pt_offset)
{
	uint64_t pae = 0;
	uint64_t phys = INGTT(pt_offset);

	if (intel_gen(devid) < 4 && !IS_G33(devid))
		return phys & ~0xfff;

	switch (intel_gen(devid)) {
		case 3:
		case 4:
		case 5:
			pae = (phys & 0xf0) << 28;
			break;
		case 6:
		case 7:
			if (IS_HASWELL(devid))
				pae = (phys & 0x7f0) << 28;
			else
				pae = (phys & 0xff0) << 28;
			break;
		default:
			fprintf(stderr, "Unsupported platform\n");
			exit(-1);
	}

	return (phys | pae) & ~0xfff;
}

static void pte_dump(int size, uint32_t offset) {
	int start;
	/* Want to print 4 ptes at a time (4b PTE assumed). */
	if (size % 16)
		size = (size + 16) & ~0xffff;


	printf("GTT offset |                 PTEs\n");
	printf("--------------------------------------------------------\n");
	for (start = 0; start < size; start += KB(16)) {
		printf("  0x%06x | 0x%08x 0x%08x 0x%08x 0x%08x\n",
				start,
				INGTT(start + 0x0),
				INGTT(start + 0x1000),
				INGTT(start + 0x2000),
				INGTT(start + 0x3000));
	}
}

int main(int argc, char **argv)
{
	struct pci_device *pci_dev;
	int start, gtt_size;
	int flag[] = {
		PCI_DEV_MAP_FLAG_WRITE_COMBINE,
		PCI_DEV_MAP_FLAG_WRITABLE,
		0
	}, f;

	pci_dev = intel_get_pci_device();
	devid = pci_dev->device_id;

	if (IS_GEN2(devid)) {
		printf("Unsupported chipset for gtt dumper\n");
		exit(1);
	}

	for (f = 0; flag[f] != 0; f++) {
		if (IS_GEN3(devid)) {
			/* 915/945 chips has GTT range in bar 3 */
			if (pci_device_map_range(pci_dev,
						 pci_dev->regions[3].base_addr,
						 pci_dev->regions[3].size,
						 flag[f],
						 (void **)&gtt) == 0)
				break;
		} else {
			int offset;
			if (IS_GEN4(devid))
				offset = KB(512);
			else
				offset = MB(2);
			if (pci_device_map_range(pci_dev,
						 pci_dev->regions[0].base_addr + offset,
						 offset,
						 flag[f],
						 (void **)&gtt) == 0)
				break;
		}
	}
	if (flag[f] == 0) {
		printf("Failed to map gtt\n");
		exit(1);
	}

	gtt_size = pci_dev->regions[0].size / 2;
	if (argc > 1 && !strncmp("-d", argv[1], 2)) {
		pte_dump(gtt_size, 0);
		return 0;
	}

	for (start = 0; start < gtt_size; start += KB(4)) {
		uint64_t start_phys = get_phys(start);
		uint32_t end;
		int constant_length = 0;
		int linear_length = 0;

		/* Check if it's a linear sequence */
		for (end = start + KB(4); end < gtt_size; end += KB(4)) {
			uint64_t end_phys = get_phys(end);
			if (end_phys == start_phys + (end - start))
				linear_length++;
			else
				break;
		}
		if (linear_length > 0) {
			printf("0x%08x - 0x%08x: linear from "
			       "0x%" PRIx64 " to 0x%" PRIx64 "\n",
			       start, end - KB(4),
			       start_phys, start_phys + (end - start) - KB(4));
			start = end - KB(4);
			continue;
		}

		/* Check if it's a constant sequence */
		for (end = start + KB(4); end < gtt_size; end += KB(4)) {
			uint64_t end_phys = get_phys(end);
			if (end_phys == start_phys)
				constant_length++;
			else
				break;
		}
		if (constant_length > 0) {
			printf("0x%08x - 0x%08x: constant 0x%" PRIx64 "\n",
			       start, end - KB(4), start_phys);
			start = end - KB(4);
			continue;
		}

		printf("0x%08x: 0x%" PRIx64 "\n", start, start_phys);
	}

	return 0;
}
