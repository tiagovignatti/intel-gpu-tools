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
 *    Vijay Purushothaman <vijay.a.purushothaman@intel.com>
 *
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include "intel_gpu_tools.h"

static uint32_t intel_display_reg_read(uint32_t reg)
{
	struct pci_device *dev = intel_get_pci_device();

	if (IS_VALLEYVIEW(dev->device_id))
		reg += VLV_DISPLAY_BASE;
	return (*(volatile uint32_t*)((volatile char*)mmio + reg));
}

static void intel_display_reg_write(uint32_t reg, uint32_t val)
{
	volatile uint32_t *ptr;
	struct pci_device *dev = intel_get_pci_device();

	if (IS_VALLEYVIEW(dev->device_id))
		reg += VLV_DISPLAY_BASE;
	ptr = (volatile uint32_t*)((volatile char*)mmio + reg);
	*ptr = val;
}

static int get_dpio_port(int phy) {

	struct pci_device *dev = intel_get_pci_device();
	int dpio_port;

	if (IS_VALLEYVIEW(dev->device_id))
		dpio_port = DPIO_PORTID;

	return dpio_port;
}

/*
 * In SoCs like Valleyview some of the PLL & Lane control registers
 * can be accessed only through IO side band fabric called DPIO
 */
uint32_t
intel_dpio_reg_read(uint32_t reg, int phy)
{
	/* Check whether the side band fabric is ready to accept commands */
	do {
		usleep(1);
	} while (intel_display_reg_read(DPIO_PKT) & DPIO_BUSY);

	intel_display_reg_write(DPIO_REG, reg);
	intel_display_reg_write(DPIO_PKT, DPIO_RID |
				DPIO_OP_READ | get_dpio_port(phy) | DPIO_BYTE);
	do {
		usleep(1);
	} while (intel_display_reg_read(DPIO_PKT) & DPIO_BUSY);

	return intel_display_reg_read(DPIO_DATA);
}

/*
 * In SoCs like Valleyview some of the PLL & Lane control registers
 * can be accessed only through IO side band fabric called DPIO
 */
void
intel_dpio_reg_write(uint32_t reg, uint32_t val, int phy)
{
	/* Check whether the side band fabric is ready to accept commands */
	do {
		usleep(1);
	} while (intel_display_reg_read(DPIO_PKT) & DPIO_BUSY);

	intel_display_reg_write(DPIO_DATA, val);
	intel_display_reg_write(DPIO_REG, reg);
	intel_display_reg_write(DPIO_PKT, DPIO_RID |
				DPIO_OP_WRITE | get_dpio_port(phy) | DPIO_BYTE);
	do {
		usleep(1);
	} while (intel_display_reg_read(DPIO_PKT) & DPIO_BUSY);
}
