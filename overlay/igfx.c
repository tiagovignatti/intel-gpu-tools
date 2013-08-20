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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <pciaccess.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include "igfx.h"
#include "i915_pciids.h"

static const struct igfx_info generic_info = {
	.gen = -1,
};

static const struct igfx_info i81x_info = {
	.gen = 010,
};

static const struct igfx_info i830_info = {
	.gen = 020,
};
static const struct igfx_info i845_info = {
	.gen = 020,
};
static const struct igfx_info i855_info = {
	.gen = 021,
};
static const struct igfx_info i865_info = {
	.gen = 022,
};

static const struct igfx_info i915_info = {
	.gen = 030,
};
static const struct igfx_info i945_info = {
	.gen = 031,
};

static const struct igfx_info g33_info = {
	.gen = 033,
};

static const struct igfx_info i965_info = {
	.gen = 040,
};

static const struct igfx_info g4x_info = {
	.gen = 045,
};

static const struct igfx_info ironlake_info = {
	.gen = 050,
};

static const struct igfx_info sandybridge_info = {
	.gen = 060,
};

static const struct igfx_info ivybridge_info = {
	.gen = 070,
};

static const struct igfx_info valleyview_info = {
	.gen = 071,
};

static const struct igfx_info haswell_info = {
	.gen = 075,
};

static const struct pci_id_match match[] = {
#if 0
	INTEL_VGA_DEVICE(PCI_CHIP_I810, &i81x_info),
	INTEL_VGA_DEVICE(PCI_CHIP_I810_DC100, &i81x_info),
	INTEL_VGA_DEVICE(PCI_CHIP_I810_E, &i81x_info),
	INTEL_VGA_DEVICE(PCI_CHIP_I815, &i81x_info),
#endif

	INTEL_I830_IDS(&i830_info),
	INTEL_I845G_IDS(&i830_info),
	INTEL_I85X_IDS(&i855_info),
	INTEL_I865G_IDS(&i865_info),

	INTEL_I915G_IDS(&i915_info),
	INTEL_I915GM_IDS(&i915_info),
	INTEL_I945G_IDS(&i945_info),
	INTEL_I945GM_IDS(&i945_info),

	INTEL_G33_IDS(&g33_info),
	INTEL_PINEVIEW_IDS(&g33_info),

	INTEL_I965G_IDS(&i965_info),
	INTEL_I965GM_IDS(&i965_info),

	INTEL_G45_IDS(&g4x_info),
	INTEL_GM45_IDS(&g4x_info),

	INTEL_IRONLAKE_D_IDS(&ironlake_info),
	INTEL_IRONLAKE_M_IDS(&ironlake_info),

	INTEL_SNB_D_IDS(&sandybridge_info),
	INTEL_SNB_M_IDS(&sandybridge_info),

	INTEL_IVB_D_IDS(&ivybridge_info),
	INTEL_IVB_M_IDS(&ivybridge_info),

	INTEL_HSW_D_IDS(&haswell_info),
	INTEL_HSW_M_IDS(&haswell_info),

	INTEL_VLV_D_IDS(&valleyview_info),
	INTEL_VLV_M_IDS(&valleyview_info),

	INTEL_VGA_DEVICE(PCI_MATCH_ANY, &generic_info),

	{ 0, 0, 0 },
};

struct pci_device *igfx_get(void)
{
	struct pci_device *dev;

	if (pci_system_init())
		return 0;

	dev = pci_device_find_by_slot(0, 0, 2, 0);
	if (dev == NULL || dev->vendor_id != 0x8086) {
		struct pci_device_iterator *iter;

		iter = pci_id_match_iterator_create(match);
		if (!iter)
			return 0;

		dev = pci_device_next(iter);
		pci_iterator_destroy(iter);
	}

	return dev;
}

const struct igfx_info *igfx_get_info(struct pci_device *dev)
{
	int i;

	if (!dev)
		return 0;

	for (i = 0; match[i].device_id != PCI_MATCH_ANY; i++)
		if (dev->device_id == match[i].device_id)
			return (const struct igfx_info *)match[i].match_data;

	return &generic_info;
}

static int forcewake = -1;

static void
igfx_forcewake(void)
{
	char buf[1024];
	const char *path[] = {
		"/sys/kernel/debug/dri/",
		"/debug/dri/",
		0,
	};
	int i, j;

	for (j = 0; path[j]; j++) {
		struct stat st;

		if (stat(path[j], &st))
			continue;

		for (i = 0; i < 16; i++) {
			snprintf(buf, sizeof(buf),
				 "%s/%i/i915_forcewake_user",
				 path[j], i);
			forcewake = open(buf, 0);
			if (forcewake != -1)
				return;
		}
	}
}

void *igfx_get_mmio(struct pci_device *dev)
{
	const struct igfx_info *info;
	int mmio_bar, mmio_size;
	void *mmio;

	info = igfx_get_info(dev);
	if (info->gen >> 3 == 2)
		mmio_bar = 1;
	else
		mmio_bar = 0;

	if (info->gen < 030)
		mmio_size = 512*1024;
	else if (info->gen < 050)
		mmio_size = 512*1024;
	else
		mmio_size = 2*1024*1024;

	if (pci_device_probe(dev))
		return 0;

	if (pci_device_map_range(dev,
				 dev->regions[mmio_bar].base_addr,
				 mmio_size,
				 PCI_DEV_MAP_FLAG_WRITABLE,
				 &mmio))
		return 0;

	if (info->gen >= 060)
		igfx_forcewake();

	return mmio;
}

