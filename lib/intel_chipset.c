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
#include "i915_drm.h"

#include "intel_chipset.h"
#include "igt_core.h"

/**
 * SECTION:intel_chipset
 * @short_description: Feature macros and chipset helpers
 * @title: intel chipset
 * @include: intel_chipset.h
 *
 * This library mostly provides feature macros which use raw pci device ids. It
 * also provides a few more helper functions to handle pci devices, chipset
 * detection and related issues.
 */

/**
 * intel_pch:
 *
 * Global variable to keep track of the pch type. Can either be set manually or
 * detected at runtime with intel_check_pch().
 */
enum pch_type intel_pch;

/**
 * intel_get_pci_device:
 *
 * Looks up the main graphics pci device using libpciaccess.
 *
 * Returns:
 * The pci_device, exits the program on any failures.
 */
struct pci_device *
intel_get_pci_device(void)
{
	struct pci_device *pci_dev;
	int error;

	error = pci_system_init();
	igt_fail_on_f(error != 0,
		      "Couldn't initialize PCI system\n");

	/* Grab the graphics card. Try the canonical slot first, then
	 * walk the entire PCI bus for a matching device. */
	pci_dev = pci_device_find_by_slot(0, 0, 2, 0);
	if (pci_dev == NULL || pci_dev->vendor_id != 0x8086) {
		struct pci_device_iterator *iter;
		struct pci_id_match match;

		match.vendor_id = 0x8086; /* Intel */
		match.device_id = PCI_MATCH_ANY;
		match.subvendor_id = PCI_MATCH_ANY;
		match.subdevice_id = PCI_MATCH_ANY;

		match.device_class = 0x3 << 16;
		match.device_class_mask = 0xff << 16;

		match.match_data = 0;

		iter = pci_id_match_iterator_create(&match);
		pci_dev = pci_device_next(iter);
		pci_iterator_destroy(iter);
	}
	if (pci_dev == NULL)
		errx(1, "Couldn't find graphics card");

	error = pci_device_probe(pci_dev);
	igt_fail_on_f(error != 0,
		      "Couldn't probe graphics card\n");

	if (pci_dev->vendor_id != 0x8086)
		errx(1, "Graphics card is non-intel");

	return pci_dev;
}

/**
 * intel_get_drm_devid:
 * @fd: open i915 drm file descriptor
 *
 * Queries the kernel for the pci device id corresponding to the drm file
 * descriptor.
 *
 * Returns:
 * The devid, exits the program on any failures.
 */
uint32_t
intel_get_drm_devid(int fd)
{
	uint32_t devid = 0;
	const char *override;

	override = getenv("INTEL_DEVID_OVERRIDE");
	if (override) {
		devid = strtod(override, NULL);
	} else {
		struct drm_i915_getparam gp;
		int ret;

		memset(&gp, 0, sizeof(gp));
		gp.param = I915_PARAM_CHIPSET_ID;
		gp.value = (int *)&devid;

		ret = ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp));
		igt_assert(ret == 0);
		errno = 0;
	}

	return devid;
}

/**
 * intel_gen:
 * @devid: pci device id
 *
 * Computes the Intel GFX generation for the give device id.
 *
 * Returns:
 * The GFX generation on successful lookup, -1 on failure.
 */
int intel_gen(uint32_t devid)
{
	if (IS_GEN2(devid))
		return 2;
	if (IS_GEN3(devid))
		return 3;
	if (IS_GEN4(devid))
		return 4;
	if (IS_GEN5(devid))
		return 5;
	if (IS_GEN6(devid))
		return 6;
	if (IS_GEN7(devid))
		return 7;
	if (IS_GEN8(devid))
		return 8;

	return -1;
}

/**
 * intel_check_pch:
 *
 * Detects the PCH chipset type of the running systems and fills in the results
 * into the global #intel_pch varaible.
 */
void
intel_check_pch(void)
{
	struct pci_device *pch_dev;

	pch_dev = pci_device_find_by_slot(0, 0, 31, 0);
	if (pch_dev == NULL)
		return;

	if (pch_dev->vendor_id != 0x8086)
		return;

	switch (pch_dev->device_id & 0xff00) {
	case 0x3b00:
		intel_pch = PCH_IBX;
		break;
	case 0x1c00:
	case 0x1e00:
		intel_pch = PCH_CPT;
		break;
	case 0x8c00:
	case 0x9c00:
		intel_pch = PCH_LPT;
		break;
	default:
		intel_pch = PCH_NONE;
		return;
	}
}
