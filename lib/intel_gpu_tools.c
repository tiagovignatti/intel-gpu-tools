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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <assert.h>
#include <sys/ioctl.h>
#include "intel_gpu_tools.h"
#include "intel_chipset.h"
#include "i915_drm.h"

struct pci_device *pci_dev;
uint32_t devid;
void *mmio;

void
intel_get_drm_devid(int fd)
{
	int ret;
	struct drm_i915_getparam gp;

	gp.param = I915_PARAM_CHIPSET_ID;
	gp.value = (int *)&devid;

	ret = ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp));
	assert(ret == 0);
}

void
intel_get_mmio(void)
{
	int err;
	int mmio_bar;

	err = pci_system_init();
	if (err != 0) {
		fprintf(stderr, "Couldn't initialize PCI system: %s\n",
			strerror(err));
		exit(1);
	}

	/* Grab the graphics card */
	pci_dev = pci_device_find_by_slot(0, 0, 2, 0);
	if (pci_dev == NULL)
		errx(1, "Couldn't find graphics card");

	err = pci_device_probe(pci_dev);
	if (err != 0) {
		fprintf(stderr, "Couldn't probe graphics card: %s\n",
			strerror(err));
		exit(1);
	}

	if (pci_dev->vendor_id != 0x8086)
		errx(1, "Graphics card is non-intel");
	devid = pci_dev->device_id;

	if (IS_9XX(devid))
		mmio_bar = 0;
	else
		mmio_bar = 1;

	err = pci_device_map_range (pci_dev,
				    pci_dev->regions[mmio_bar].base_addr,
				    pci_dev->regions[mmio_bar].size,
				    PCI_DEV_MAP_FLAG_WRITABLE,
				    &mmio);

	if (err != 0) {
		fprintf(stderr, "Couldn't map MMIO region: %s\n",
			strerror(err));
		exit(1);
	}
}
