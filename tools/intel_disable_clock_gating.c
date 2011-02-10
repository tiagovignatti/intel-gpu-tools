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

int main(int argc, char** argv)
{
	struct pci_device *pci_dev;

	pci_dev = intel_get_pci_device();
	intel_get_mmio(pci_dev);

	if (IS_GEN5(pci_dev->device_id)) {
		printf("Restore method:\n");

		printf("intel_reg_write 0x%x 0x%08x\n",
		       PCH_3DCGDIS0, INREG(PCH_3DCGDIS0));
		OUTREG(PCH_3DCGDIS0, 0xffffffff);

		printf("intel_reg_write 0x%x 0x%08x\n",
		       PCH_3DCGDIS1, INREG(PCH_3DCGDIS1));
		OUTREG(PCH_3DCGDIS1, 0xffffffff);

		printf("intel_reg_write 0x%x 0x%08x\n",
		       PCH_3DRAMCGDIS0, INREG(PCH_3DRAMCGDIS0));
		OUTREG(PCH_3DRAMCGDIS0, 0xffffffff);

		printf("intel_reg_write 0x%x 0x%08x\n",
		       PCH_DSPCLK_GATE_D, INREG(PCH_DSPCLK_GATE_D));
		OUTREG(PCH_DSPCLK_GATE_D, 0xffffffff);

		printf("intel_reg_write 0x%x 0x%08x\n",
		       PCH_DSPRAMCLK_GATE_D, INREG(PCH_DSPRAMCLK_GATE_D));
		OUTREG(PCH_DSPRAMCLK_GATE_D, 0xffffffff);
	} else {
		fprintf(stderr, "unsupported chipset\n");
	}


	return 0;
}

