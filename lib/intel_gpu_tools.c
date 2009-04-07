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
#include "i915_drm.h"
#include "intel_batchbuffer.h"
#include "intel_chipset.h"

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

void
intel_copy_bo(struct intel_batchbuffer *batch,
	      drm_intel_bo *dst_bo, drm_intel_bo *src_bo,
	      int width, int height)
{
	uint32_t src_tiling, dst_tiling, swizzle;
	uint32_t src_pitch, dst_pitch;
	uint32_t cmd_bits = 0;

	drm_intel_bo_get_tiling(src_bo, &src_tiling, &swizzle);
	drm_intel_bo_get_tiling(dst_bo, &dst_tiling, &swizzle);

	src_pitch = width * 4;
	if (IS_965(devid) && src_tiling != I915_TILING_NONE) {
		src_pitch /= 4;
		cmd_bits |= XY_SRC_COPY_BLT_SRC_TILED;
	}

	dst_pitch = width * 4;
	if (IS_965(devid) && dst_tiling != I915_TILING_NONE) {
		dst_pitch /= 4;
		cmd_bits |= XY_SRC_COPY_BLT_DST_TILED;
	}

	BEGIN_BATCH(8);
	OUT_BATCH(XY_SRC_COPY_BLT_CMD |
		  XY_SRC_COPY_BLT_WRITE_ALPHA |
		  XY_SRC_COPY_BLT_WRITE_RGB |
		  cmd_bits);
	OUT_BATCH((3 << 24) | /* 32 bits */
		  (0xcc << 16) | /* copy ROP */
		  dst_pitch);
	OUT_BATCH(0); /* dst x1,y1 */
	OUT_BATCH((height << 16) | width); /* dst x2,y2 */
	OUT_RELOC(dst_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(0); /* src x1,y1 */
	OUT_BATCH(src_pitch);
	OUT_RELOC(src_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
	ADVANCE_BATCH();

	intel_batchbuffer_flush(batch);
}


