/*
 * Copyright © 2010 Red Hat, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *	Adam Jackson <ajax@redhat.com>
 */

#include <unistd.h>
#include <assert.h>
#include "intel_io.h"
#include "intel_chipset.h"

int main(int argc, char** argv)
{
	struct pci_device *pci_dev;
	uint32_t devid;
	int mmio_bar;
	int ret;

	pci_dev = intel_get_pci_device();
	devid = pci_dev->device_id;
	intel_mmio_use_pci_bar(pci_dev);

	if (IS_GEN2(devid))
		mmio_bar = 1;
	else
		mmio_bar = 0;

	ret = write(1, mmio, pci_dev->regions[mmio_bar].size);
	assert(ret > 0);

	return 0;
}
