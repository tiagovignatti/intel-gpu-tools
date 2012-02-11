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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "intel_gpu_tools.h"

enum pch_type pch;

struct pci_device *
intel_get_pci_device(void)
{
	struct pci_device *pci_dev;
	int error;

	error = pci_system_init();
	if (error != 0) {
		fprintf(stderr, "Couldn't initialize PCI system: %s\n",
			strerror(error));
		exit(1);
	}

	/* Grab the graphics card */
	pci_dev = pci_device_find_by_slot(0, 0, 2, 0);
	if (pci_dev == NULL)
		errx(1, "Couldn't find graphics card");

	error = pci_device_probe(pci_dev);
	if (error != 0) {
		fprintf(stderr, "Couldn't probe graphics card: %s\n",
			strerror(error));
		exit(1);
	}

	if (pci_dev->vendor_id != 0x8086)
		errx(1, "Graphics card is non-intel");

	return pci_dev;
}

void
intel_check_pch(void)
{
	struct pci_device *pch_dev;

	pch_dev = pci_device_find_by_slot(0, 0, 31, 0);
	if (pch_dev == NULL)
		return;

	if (pch_dev->vendor_id == 0x8086 &&
	    (((pch_dev->device_id & 0xff00) == 0x1c00) ||
	     (pch_dev->device_id & 0xff00) == 0x1e00))
		pch = PCH_CPT;
}

